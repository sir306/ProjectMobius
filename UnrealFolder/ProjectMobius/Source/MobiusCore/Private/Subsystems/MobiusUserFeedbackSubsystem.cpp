// Central error/log routing without a hard dependency on UI modules.

#include "Subsystems/MobiusUserFeedbackSubsystem.h"

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "HAL/PlatformTime.h"
#include "Containers/Array.h"
#include "Subsystems/MobiusCustomLoggerSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobiusUserFeedback, Log, All);

UMobiusUserFeedbackSubsystem::UMobiusUserFeedbackSubsystem() = default;

void UMobiusUserFeedbackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UMobiusCustomLoggerSubsystem* Logger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr)
	{
		LogLineHandle = Logger->OnLogLine().AddUObject(this, &UMobiusUserFeedbackSubsystem::HandleLogLine);
	}
}

void UMobiusUserFeedbackSubsystem::Deinitialize()
{
	if (UMobiusCustomLoggerSubsystem* Logger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr)
	{
		if (LogLineHandle.IsValid())
		{
			Logger->OnLogLine().Remove(LogLineHandle);
			LogLineHandle.Reset();
		}
	}

	PendingErrors.Reset();
	CachedLogLines.Reset();
	RecentPromptTimes.Reset();

	Super::Deinitialize();
}

UMobiusUserFeedbackSubsystem* UMobiusUserFeedbackSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject)
	{
		if (const UWorld* World = WorldContextObject->GetWorld())
		{
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				return GameInstance->GetSubsystem<UMobiusUserFeedbackSubsystem>();
			}
		}

		if (const UGameInstance* GameInstance = Cast<UGameInstance>(WorldContextObject))
		{
			return GameInstance->GetSubsystem<UMobiusUserFeedbackSubsystem>();
		}
	}

	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				if (UGameInstance* GameInstance = World->GetGameInstance())
				{
					return GameInstance->GetSubsystem<UMobiusUserFeedbackSubsystem>();
				}
			}
		}
	}

	return nullptr;
}

void UMobiusUserFeedbackSubsystem::ReportErrorFromAnyThread(TWeakObjectPtr<UObject> WorldContextObject,
	const FMobiusErrorMessage& Message)
{
	if (IsInGameThread())
	{
		if (UMobiusUserFeedbackSubsystem* Subsystem = Get(WorldContextObject.Get()))
		{
			Subsystem->ReportErrorInternal(Message);
		}
		return;
	}

	AsyncTask(ENamedThreads::GameThread, [WorldContextObject, Message]()
	{
		if (UMobiusUserFeedbackSubsystem* Subsystem = Get(WorldContextObject.Get()))
		{
			Subsystem->ReportErrorInternal(Message);
		}
	});
}

void UMobiusUserFeedbackSubsystem::ReportError(const FText& TitleBarText, const FText& ErrorTitle,
	const FText& ErrorMessage, const FText& ErrorLocation, EMobiusErrorSeverity Severity, bool bShowPrompt)
{
	FMobiusErrorMessage Payload;
	Payload.TitleBarText = TitleBarText;
	Payload.ErrorTitle = ErrorTitle;
	Payload.ErrorMessage = ErrorMessage;
	Payload.ErrorLocation = ErrorLocation;
	Payload.Severity = Severity;
	Payload.bShowPrompt = bShowPrompt;

	if (!IsInGameThread())
	{
		const TWeakObjectPtr<UMobiusUserFeedbackSubsystem> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Payload]()
		{
			if (UMobiusUserFeedbackSubsystem* Self = WeakThis.Get())
			{
				Self->ReportErrorInternal(Payload);
			}
		});
		return;
	}

	ReportErrorInternal(Payload);
}

void UMobiusUserFeedbackSubsystem::SetErrorPromptsEnabled(bool bEnabled)
{
	bErrorPromptsEnabled = bEnabled;
}

bool UMobiusUserFeedbackSubsystem::AreErrorPromptsEnabled() const
{
	return bErrorPromptsEnabled;
}

void UMobiusUserFeedbackSubsystem::RequestLogWindowOpen()
{
	if (!IsInGameThread())
	{
		const TWeakObjectPtr<UMobiusUserFeedbackSubsystem> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis]()
		{
			if (UMobiusUserFeedbackSubsystem* Self = WeakThis.Get())
			{
				Self->RequestLogWindowOpen();
			}
		});
		return;
	}

	bLogWindowOpen = true;
	if (bLogWindowEnabled)
	{
		BroadcastLogWindowCommand(EMobiusLogWindowCommand::Open);
	}
}

void UMobiusUserFeedbackSubsystem::RequestLogWindowClose()
{
	if (!IsInGameThread())
	{
		const TWeakObjectPtr<UMobiusUserFeedbackSubsystem> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis]()
		{
			if (UMobiusUserFeedbackSubsystem* Self = WeakThis.Get())
			{
				Self->RequestLogWindowClose();
			}
		});
		return;
	}

	bLogWindowOpen = false;
	BroadcastLogWindowCommand(EMobiusLogWindowCommand::Close);
}

void UMobiusUserFeedbackSubsystem::SetLogWindowEnabled(bool bEnabled)
{
	if (!IsInGameThread())
	{
		const TWeakObjectPtr<UMobiusUserFeedbackSubsystem> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, bEnabled]()
		{
			if (UMobiusUserFeedbackSubsystem* Self = WeakThis.Get())
			{
				Self->SetLogWindowEnabled(bEnabled);
			}
		});
		return;
	}

	if (bLogWindowEnabled == bEnabled)
	{
		return;
	}

	bLogWindowEnabled = bEnabled;
	BroadcastLogWindowCommand(bEnabled ? EMobiusLogWindowCommand::Enable : EMobiusLogWindowCommand::Disable);

	if (!bLogWindowEnabled)
	{
		BroadcastLogWindowCommand(EMobiusLogWindowCommand::Close);
	}
	else if (bLogWindowOpen)
	{
		BroadcastLogWindowCommand(EMobiusLogWindowCommand::Open);
	}
}

bool UMobiusUserFeedbackSubsystem::IsLogWindowEnabled() const
{
	return bLogWindowEnabled;
}

bool UMobiusUserFeedbackSubsystem::IsLogWindowOpen() const
{
	return bLogWindowOpen;
}

TArray<FMobiusErrorMessage> UMobiusUserFeedbackSubsystem::DrainPendingErrors()
{
	TArray<FMobiusErrorMessage> Drained = MoveTemp(PendingErrors);
	PendingErrors.Reset();
	return Drained;
}

TArray<FString> UMobiusUserFeedbackSubsystem::GetCachedLogLines() const
{
	return CachedLogLines;
}

FOnMobiusErrorReported& UMobiusUserFeedbackSubsystem::OnErrorReported()
{
	return ErrorReportedDelegate;
}

FOnMobiusLogLine& UMobiusUserFeedbackSubsystem::OnLogLine()
{
	return LogLineDelegate;
}

FOnMobiusLogWindowCommand& UMobiusUserFeedbackSubsystem::OnLogWindowCommand()
{
	return LogWindowCommandDelegate;
}

void UMobiusUserFeedbackSubsystem::BroadcastLogWindowCommand(EMobiusLogWindowCommand Command)
{
	if (IsInGameThread())
	{
		LogWindowCommandDelegate.Broadcast(Command);
		return;
	}

	const TWeakObjectPtr<UMobiusUserFeedbackSubsystem> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, Command]()
	{
		if (UMobiusUserFeedbackSubsystem* Self = WeakThis.Get())
		{
			Self->LogWindowCommandDelegate.Broadcast(Command);
		}
	});
}

void UMobiusUserFeedbackSubsystem::ReportErrorInternal(const FMobiusErrorMessage& Message)
{
	const FString Title = Message.ErrorTitle.ToString();
	const FString Body = Message.ErrorMessage.ToString();
	const FString Location = Message.ErrorLocation.ToString();
	const FString FullMessage = Location.IsEmpty()
		? FString::Printf(TEXT("%s: %s"), *Title, *Body)
		: FString::Printf(TEXT("%s: %s (%s)"), *Title, *Body, *Location);

	switch (Message.Severity)
	{
	case EMobiusErrorSeverity::Info:
		UE_LOG(LogMobiusUserFeedback, Log, TEXT("%s"), *FullMessage);
		break;
	case EMobiusErrorSeverity::Warning:
		UE_LOG(LogMobiusUserFeedback, Warning, TEXT("%s"), *FullMessage);
		break;
	case EMobiusErrorSeverity::Fatal:
		UE_LOG(LogMobiusUserFeedback, Error, TEXT("%s"), *FullMessage);
		break;
	case EMobiusErrorSeverity::Error:
	default:
		UE_LOG(LogMobiusUserFeedback, Error, TEXT("%s"), *FullMessage);
		break;
	}

	if (UMobiusCustomLoggerSubsystem* Logger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr)
	{
		Logger->EnqueueLogMessage(FullMessage);
	}

	if (!ShouldPrompt(Message))
	{
		return;
	}

	if (ErrorReportedDelegate.IsBound())
	{
		ErrorReportedDelegate.Broadcast(Message);
	}
	else
	{
		EnqueuePendingError(Message);
	}
}

void UMobiusUserFeedbackSubsystem::HandleLogLine(const FString& Line)
{
	CachedLogLines.Add(Line);
	if (CachedLogLines.Num() > MaxCachedLogLines)
	{
		const int32 Overflow = CachedLogLines.Num() - MaxCachedLogLines;
		CachedLogLines.RemoveAt(0, Overflow, EAllowShrinking::No);
	}

	LogLineDelegate.Broadcast(Line);
}

bool UMobiusUserFeedbackSubsystem::ShouldPrompt(const FMobiusErrorMessage& Message)
{
	if (!bErrorPromptsEnabled || !Message.bShowPrompt)
	{
		return false;
	}

	const uint32 Hash = HashMessage(Message);
	const double Now = FPlatformTime::Seconds();
	if (const double* LastTime = RecentPromptTimes.Find(Hash))
	{
		if ((Now - *LastTime) < PromptCooldownSeconds)
		{
			return false;
		}
	}

	RecentPromptTimes.Add(Hash, Now);
	return true;
}

uint32 UMobiusUserFeedbackSubsystem::HashMessage(const FMobiusErrorMessage& Message) const
{
	uint32 Hash = GetTypeHash(Message.ErrorTitle.ToString());
	Hash = HashCombine(Hash, GetTypeHash(Message.ErrorMessage.ToString()));
	Hash = HashCombine(Hash, GetTypeHash(Message.ErrorLocation.ToString()));
	Hash = HashCombine(Hash, static_cast<uint32>(Message.Severity));
	return Hash;
}

void UMobiusUserFeedbackSubsystem::EnqueuePendingError(const FMobiusErrorMessage& Message)
{
	PendingErrors.Add(Message);
	if (PendingErrors.Num() > MaxPendingErrors)
	{
		const int32 Overflow = PendingErrors.Num() - MaxPendingErrors;
	PendingErrors.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
}
