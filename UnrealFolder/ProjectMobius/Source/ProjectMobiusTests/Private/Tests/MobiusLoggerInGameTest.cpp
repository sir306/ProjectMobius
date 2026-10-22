// Copyright (c) 2026 ProjectMobius contributors. Licensed under MIT.
//
// MobiusLoggerInGameTest.cpp
//
// In-game gates for the custom logger path
// (UMobiusCustomLoggerSubsystem -> UMobiusUserFeedbackSubsystem -> log window).
//
//  - FrameGrabberRetryIsQuiet. UFrameGrabberHelper::Tick retries TryInitialize until the game viewport
//    is usable. Under -game -nullrhi - every headless in-game automation run - the viewport stays 0x0 for
//    the whole session, and the helper used to log "[FrameGrabber] TryInitialize failed: Invalid
//    viewport size 0x0" on every frame: 1,070,058 of the 1,259,848 lines in the dev machine's
//    MobiusCustomLog.txt, at up to ~3,000 lines/s. This counts those lines over a fixed idle window and
//    fails above a small bound. Where the viewport IS valid the count is ~0 with or without the fix, so
//    only a -nullrhi run discriminates; the test reports which viewport it saw.
//
//  - FeedbackCacheKeepsNewestOldestFirst. UMobiusUserFeedbackSubsystem caches the newest lines to seed
//    a (re)opened log window. It is a ring buffer; this pushes 1.5x its capacity of marker lines through
//    the REAL logger flush and checks GetCachedLogLines() returns exactly the newest ones, oldest first -
//    the wrap-around seam is where a ring buffer gets the order wrong.
//
// Both force logging on for their duration (it is a persisted user toggle and may be off) and restore it.
// ClientContext only: they need a game world and its GameInstance. Run:
//   UnrealEditor-Cmd <uproject> -game -nullrhi -unattended -ExecCmds="Automation RunTests Mobius.InGame.Logger.; Quit"
// Named "Mobius.InGame." so the default correctness filter never runs them.
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Subsystems/MobiusCustomLoggerSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "UnrealClient.h"

namespace MobiusLoggerInGameTest
{
	/** Idle window for the FrameGrabber count. At the ~120 fps a -nullrhi run reaches, the pre-fix code
	 *  logs well over a thousand lines in this time. */
	constexpr float IdleSeconds = 10.0f;

	/** Generous: the fixed helper logs a failure reason once, when it CHANGES - normally before this
	 *  test even starts, so 0 lines here. */
	constexpr int32 MaxFrameGrabberFailureLines = 3;

	/** UMobiusUserFeedbackSubsystem::MaxCachedLogLines (private). If that changes, so must this. */
	constexpr int32 FeedbackCacheCapacity = 1000;

	UWorld* FindGameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	FIntPoint GameViewportSize()
	{
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
		{
			return GEngine->GameViewport->Viewport->GetSizeXY();
		}
		return FIntPoint::ZeroValue;
	}

	struct FFrameGrabberSpamState
	{
		int32 FrameGrabberFailureLines = 0;
		int32 AllLines = 0;
		FDelegateHandle LogLineHandle;
		bool bLoggingWasEnabled = true;
		double StartSeconds = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMobiusLoggerFrameGrabberRetryIsQuietTest,
	"Mobius.InGame.Logger.FrameGrabberRetryIsQuiet",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FMobiusLoggerFrameGrabberRetryIsQuietTest::RunTest(const FString& Parameters)
{
	using namespace MobiusLoggerInGameTest;

	if (!FindGameWorld())
	{
		AddError(TEXT("no game world - run under UnrealEditor-Cmd -game"));
		return false;
	}
	UMobiusCustomLoggerSubsystem* Logger = GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>();
	if (!Logger)
	{
		AddError(TEXT("UMobiusCustomLoggerSubsystem is missing"));
		return false;
	}

	const TSharedRef<FFrameGrabberSpamState> State = MakeShared<FFrameGrabberSpamState>();
	// SetLoggingEnabled UE_LOGs a warning, so only touch it when logging is actually off.
	State->bLoggingWasEnabled = Logger->IsLoggingEnabled();
	if (!State->bLoggingWasEnabled)
	{
		Logger->SetLoggingEnabled(true);
	}

	// Deliver everything queued so far first, so only lines produced inside the window are counted.
	Logger->FlushQueuedMessages();
	State->LogLineHandle = Logger->OnLogLine().AddLambda([State](const FString& Line)
	{
		++State->AllLines;
		if (Line.Contains(TEXT("[FrameGrabber] TryInitialize failed")))
		{
			++State->FrameGrabberFailureLines;
		}
	});
	State->StartSeconds = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(IdleSeconds));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
	{
		UMobiusCustomLoggerSubsystem* LoggerNow = GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>();
		if (!LoggerNow)
		{
			AddError(TEXT("UMobiusCustomLoggerSubsystem went away during the idle window"));
			return true;
		}
		LoggerNow->FlushQueuedMessages();
		LoggerNow->OnLogLine().Remove(State->LogLineHandle);
		const double Elapsed = FPlatformTime::Seconds() - State->StartSeconds;
		const FIntPoint Viewport = GameViewportSize();

		AddInfo(FString::Printf(TEXT("%d '[FrameGrabber] TryInitialize failed' lines in %.1f s (%.1f/s), %d logger lines in total; game viewport %dx%d%s"),
			State->FrameGrabberFailureLines, Elapsed, Elapsed > 0.0 ? State->FrameGrabberFailureLines / Elapsed : 0.0,
			State->AllLines, Viewport.X, Viewport.Y,
			(Viewport.X > 0 && Viewport.Y > 0) ? TEXT(" (valid viewport: this run cannot reproduce the 0x0 retry spam)") : TEXT("")));

		TestTrue(FString::Printf(TEXT("FrameGrabber logged at most %d TryInitialize failures in %.0f s idle"), MaxFrameGrabberFailureLines, IdleSeconds),
			State->FrameGrabberFailureLines <= MaxFrameGrabberFailureLines);

		if (!State->bLoggingWasEnabled)
		{
			LoggerNow->SetLoggingEnabled(false);
		}
		return true;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMobiusLoggerFeedbackCacheOrderTest,
	"Mobius.InGame.Logger.FeedbackCacheKeepsNewestOldestFirst",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FMobiusLoggerFeedbackCacheOrderTest::RunTest(const FString& Parameters)
{
	using namespace MobiusLoggerInGameTest;

	UWorld* World = FindGameWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UMobiusUserFeedbackSubsystem* Feedback = GameInstance ? GameInstance->GetSubsystem<UMobiusUserFeedbackSubsystem>() : nullptr;
	UMobiusCustomLoggerSubsystem* Logger = GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>();
	if (!Feedback || !Logger)
	{
		AddError(TEXT("needs a game world with UMobiusUserFeedbackSubsystem and the custom logger - run under UnrealEditor-Cmd -game"));
		return false;
	}

	const bool bLoggingWasEnabled = Logger->IsLoggingEnabled();
	if (!bLoggingWasEnabled)
	{
		Logger->SetLoggingEnabled(true);
	}
	Logger->FlushQueuedMessages();

	// 1.5x capacity, through the real path: EnqueueLogMessage -> FlushToDisk broadcast -> HandleLogLine.
	// Unique tag so lines from any other run (or another producer mid-test) cannot be mistaken for ours.
	const FString Tag = FString::Printf(TEXT("[LoggerCacheTest %s]"), *FGuid::NewGuid().ToString(EGuidFormats::Short));
	const int32 Pushed = FeedbackCacheCapacity + FeedbackCacheCapacity / 2;
	for (int32 Index = 0; Index < Pushed; ++Index)
	{
		Logger->EnqueueLogMessage(FString::Printf(TEXT("%s marker %05d"), *Tag, Index));
	}
	Logger->FlushQueuedMessages();

	const TArray<FString> Cached = Feedback->GetCachedLogLines();
	if (!bLoggingWasEnabled)
	{
		Logger->SetLoggingEnabled(false);
	}

	TestEqual(TEXT("cache holds exactly its capacity"), Cached.Num(), FeedbackCacheCapacity);
	if (Cached.Num() == 0)
	{
		return false;
	}

	// Marker indices in cache order.
	TArray<int32> Markers;
	const FString MarkerPrefix = Tag + TEXT(" marker ");
	for (const FString& Line : Cached)
	{
		const int32 At = Line.Find(MarkerPrefix);
		if (At != INDEX_NONE)
		{
			Markers.Add(FCString::Atoi(*Line.Mid(At + MarkerPrefix.Len())));
		}
	}

	TestTrue(TEXT("the newest cached line is the newest marker"),
		Cached.Last().EndsWith(FString::Printf(TEXT("marker %05d"), Pushed - 1)));
	bool bStrictlyIncreasing = Markers.Num() > 0;
	for (int32 Index = 1; Index < Markers.Num(); ++Index)
	{
		bStrictlyIncreasing &= Markers[Index] == Markers[Index - 1] + 1;
	}
	TestTrue(TEXT("markers come back oldest first and contiguous - no seam where the ring wrapped"), bStrictlyIncreasing);
	if (Markers.Num() > 0)
	{
		TestTrue(TEXT("the oldest markers were evicted"), Markers[0] >= Pushed - FeedbackCacheCapacity);
	}
	// Nothing else is normally logged between the two flushes, in which case the cache is exactly the
	// newest FeedbackCacheCapacity markers.
	if (Markers.Num() == FeedbackCacheCapacity)
	{
		TestEqual(TEXT("first cached line is marker Pushed - capacity"), Markers[0], Pushed - FeedbackCacheCapacity);
	}
	AddInfo(FString::Printf(TEXT("cache %d lines, %d of them markers, first marker %d, last %d"),
		Cached.Num(), Markers.Num(), Markers.Num() ? Markers[0] : -1, Markers.Num() ? Markers.Last() : -1));
	return true;
}

#endif // !UE_BUILD_SHIPPING
