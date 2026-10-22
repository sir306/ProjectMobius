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

	/** Per-session files are MobiusCustomLog_<yyyy.mm.dd-hh.mm.ss>[_n].txt; the stamp makes name order chronological. */
	const TCHAR* const SessionLogPrefix = TEXT("MobiusCustomLog_");

	/** Session files kept, the current one included. The pre-rotation MobiusCustomLog.txt never matches. */
	constexpr int32 MaxSessionLogFiles = 20;

	FString MakeSessionLogFilePath(const FString& LogDir, const FDateTime& SessionStart)
	{
		const FString Stamp = SessionStart.ToString(TEXT("%Y.%m.%d-%H.%M.%S"));
		FString Path = FPaths::Combine(LogDir, FString::Printf(TEXT("%s%s.txt"), SessionLogPrefix, *Stamp));

		// Two processes started in the same second (an editor and a scripted headless run, say) get a file
		// each rather than interleaving into one.
		for (int32 Suffix = 2; IFileManager::Get().FileExists(*Path); ++Suffix)
		{
			Path = FPaths::Combine(LogDir, FString::Printf(TEXT("%s%s_%d.txt"), SessionLogPrefix, *Stamp, Suffix));
		}
		return Path;
	}

	/** Delete the oldest session files beyond MaxSessionLogFiles. Returns how many went. */
	int32 PruneOldSessionLogs(const FString& LogDir, const FString& CurrentLogPath)
	{
		TArray<FString> SessionFiles;
		IFileManager::Get().FindFiles(SessionFiles, *FPaths::Combine(LogDir, FString(SessionLogPrefix) + TEXT("*.txt")),
			/*Files=*/true, /*Directories=*/false);
		SessionFiles.Remove(FPaths::GetCleanFilename(CurrentLogPath));
		SessionFiles.Sort();

		const int32 ToDelete = SessionFiles.Num() - (MaxSessionLogFiles - 1);
		int32 Deleted = 0;
		for (int32 Index = 0; Index < ToDelete; ++Index)
		{
			if (IFileManager::Get().Delete(*FPaths::Combine(LogDir, SessionFiles[Index]), /*RequireExists=*/false,
				/*EvenReadOnly=*/false, /*Quiet=*/true))
			{
				++Deleted;
			}
		}
		return Deleted;
	}
}

UMobiusCustomLoggerSubsystem::UMobiusCustomLoggerSubsystem()
	: bIsFlushing(false)
{
}

void UMobiusCustomLoggerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if WITH_EDITOR
	// Editor launches from Engine/Binaries/Win64 (often read-only under Program Files);
	// route to the project's Saved/Logs directory which is always writable.
	const FString LogDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"));
#elif PLATFORM_MAC
	// LaunchDir is the working directory BEFORE the engine moves it to the bundle: "/" when the .app is
	// opened from Finder, which is not writable, so the log was silently lost. ProjectLogDir is
	// ~/Library/Logs/<Project>/, beside the engine's own log.
	const FString LogDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir());
#else
	// Packaged builds expect the log next to the launched executable.
	const FString LogDir = FPaths::ConvertRelativePathToFull(FPaths::LaunchDir());
#endif
	// One file per session (process). A single shared MobiusCustomLog.txt grew without bound (115 MB on the
	// dev machine) and interleaved processes running side by side. The newest MaxSessionLogFiles are kept;
	// the old shared file is left alone.
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*LogDir);

	const FDateTime SessionStart = FDateTime::Now();
	LogFilePath = MakeSessionLogFilePath(LogDir, SessionStart);

	// Claim the name now, so a process starting a moment later sees it taken.
	TUniquePtr<IFileHandle> Claim(PlatformFile.OpenWrite(*LogFilePath, /*bAppend=*/true));
	Claim.Reset();

	const int32 PrunedSessionLogs = PruneOldSessionLogs(LogDir, LogFilePath);

	FlushTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMobiusCustomLoggerSubsystem::PumpLogs),
		0.25f); // flush 4x per second to keep overhead tiny

	UE_LOG(LogTemp, Display, TEXT("MobiusCustomLogger initialised. Writing to %s"), *LogFilePath);

	// First line of every file: when this session started, matching the stamp in the file name.
	EnqueueLogMessage(FString::Printf(TEXT("=== Mobius session started %s (pid %u) ==="),
		*SessionStart.ToString(TEXT("%Y-%m-%d %H:%M:%S")), FPlatformProcess::GetCurrentProcessId()));
	EnqueueLogMessage(FString::Printf(TEXT("Custom logger initialised. Writing to %s"), *LogFilePath));
	if (PrunedSessionLogs > 0)
	{
		EnqueueLogMessage(FString::Printf(TEXT("Removed %d old session log(s); keeping the newest %d"),
			PrunedSessionLogs, MaxSessionLogFiles));
	}
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

	bIsFlushing = true;
	ON_SCOPE_EXIT
	{
		bIsFlushing = false;
	};

	for (const FString& Line : LocalMessages)
	{
		LogLineDelegate.Broadcast(Line);
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> Handle(PlatformFile.OpenWrite(*LogFilePath, /*bAppend=*/true));

	if (!Handle)
	{
		UE_LOG(LogTemp, Warning, TEXT("MobiusCustomLogger: failed to open %s for writing; %d messages dropped"),
			*LogFilePath, LocalMessages.Num());
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
	UE_LOG(LogTemp, Warning, TEXT("MobiusLogger: SetLoggingEnabled(%s)"), bEnabled ? TEXT("true") : TEXT("false"));
	if (!bEnabled)
	{
		EnqueueLogMessage(TEXT("=== Logger disabled ==="));
		FlushToDisk();
	}
	bIsLoggingEnabled = bEnabled;
}

bool UMobiusCustomLoggerSubsystem::IsLoggingEnabled() const
{
	return bIsLoggingEnabled;
}
