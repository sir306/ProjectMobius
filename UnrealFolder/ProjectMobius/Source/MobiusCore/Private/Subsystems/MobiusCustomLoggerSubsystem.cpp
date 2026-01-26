// Lightweight startup logger implementation.

#include "Subsystems/MobiusCustomLoggerSubsystem.h"

#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Containers/StringConv.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeExit.h"

namespace
{
	UMobiusCustomLoggerSubsystem* GetStartupLoggerSubsystem()
	{
		return GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
	}
}

UMobiusCustomLoggerSubsystem::UMobiusCustomLoggerSubsystem()
	: bIsFlushing(false)
{
}

void UMobiusCustomLoggerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const FString LaunchDir = FPaths::ConvertRelativePathToFull(FPaths::LaunchDir());
	LogFilePath = FPaths::Combine(LaunchDir, TEXT("MobiusCustomLog.txt"));

	// Ensure the directory exists in case LaunchDir is relative during testing.
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*FPaths::GetPath(LogFilePath));

	FlushTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMobiusCustomLoggerSubsystem::PumpLogs),
		0.25f); // flush 4x per second to keep overhead tiny

	EnqueueLogMessage(FString::Printf(TEXT("Custom logger initialised. Writing to %s"), *LogFilePath));
}

void UMobiusCustomLoggerSubsystem::Deinitialize()
{
	if (FlushTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(FlushTickerHandle);
		FlushTickerHandle.Reset();
	}

	FlushToDisk();
	Super::Deinitialize();
}

void UMobiusCustomLoggerSubsystem::EnqueueLogMessage(const FString& Message)
{
	if (!bIsLoggingEnabled)
	{
		return;
	}

	const FString Line = BuildTimestampedLine(Message);
	FScopeLock Lock(&QueueMutex);
	PendingMessages.Enqueue(Line);
}

void UMobiusCustomLoggerSubsystem::EnqueueTimedMessage(const FString& EventName, float DurationMilliseconds)
{
	const FString Line = FString::Printf(TEXT("%s completed in %.2f ms"), *EventName, DurationMilliseconds);
	EnqueueLogMessage(Line);
}

void UMobiusCustomLoggerSubsystem::FlushQueuedMessages()
{
	if (!IsInGameThread())
	{
		const TWeakObjectPtr<UMobiusCustomLoggerSubsystem> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis]()
		{
			if (UMobiusCustomLoggerSubsystem* Self = WeakThis.Get())
			{
				Self->FlushQueuedMessages();
			}
		});
		return;
	}

	FlushToDisk();
}

UMobiusCustomLoggerSubsystem* UMobiusCustomLoggerSubsystem::Get(const UObject* WorldContextObject)
{
	return GetStartupLoggerSubsystem();
}

FOnMobiusLogLine& UMobiusCustomLoggerSubsystem::OnLogLine()
{
	return LogLineDelegate;
}

bool UMobiusCustomLoggerSubsystem::PumpLogs(float DeltaTime)
{
	FlushToDisk();
	return true; // keep ticking
}

void UMobiusCustomLoggerSubsystem::FlushToDisk()
{
	if (bIsFlushing || LogFilePath.IsEmpty())
	{
		return;
	}

	TArray<FString> LocalMessages;
	{
		FScopeLock Lock(&QueueMutex);
		FString Line;
		while (PendingMessages.Dequeue(Line))
		{
			LocalMessages.Add(MoveTemp(Line));
		}
	}

	if (LocalMessages.Num() == 0)
	{
		return;
	}

	for (const FString& Line : LocalMessages)
	{
		LogLineDelegate.Broadcast(Line);
	}

	bIsFlushing = true;
	ON_SCOPE_EXIT
	{
		bIsFlushing = false;
	};

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> Handle(PlatformFile.OpenWrite(*LogFilePath, /*bAppend=*/true));

	if (!Handle)
	{
		return;
	}

	for (FString& Line : LocalMessages)
	{
		if (!Line.EndsWith(LINE_TERMINATOR))
		{
			Line.Append(LINE_TERMINATOR);
		}

		auto Converter = FTCHARToUTF8(*Line);
		Handle->Write(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
	}

	Handle->Flush();
}

FString UMobiusCustomLoggerSubsystem::BuildTimestampedLine(const FString& Message) const
{
	const FDateTime Now = FDateTime::Now();
	const FString Timestamp = FString::Printf(TEXT("%04d-%02d-%02d %02d:%02d:%02d.%03d"),
		Now.GetYear(), Now.GetMonth(), Now.GetDay(),
		Now.GetHour(), Now.GetMinute(), Now.GetSecond(), Now.GetMillisecond());

	return FString::Printf(TEXT("[%s] %s"), *Timestamp, *Message);
}

void UMobiusCustomLoggerSubsystem::SetLoggingEnabled(bool bEnabled)
{
	bIsLoggingEnabled = bEnabled;
}

bool UMobiusCustomLoggerSubsystem::IsLoggingEnabled() const
{
	return bIsLoggingEnabled;
}
