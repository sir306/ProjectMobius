// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2026 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "Subsystems/MobiusStartupPreloadSubsystem.h"

#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "UserConfig/UserProjectSettings.h"

#define LOCTEXT_NAMESPACE "MobiusStartupPreload"

DEFINE_LOG_CATEGORY_STATIC(LogMobiusPreload, Log, All);

namespace
{
	/**
	 * The placeholder UProjectMobiusGameInstance constructs its three path members with. A launcher
	 * echoing it back must be treated as "nothing supplied", not as a filename.
	 */
	static const TCHAR* MobiusFilePathPlaceholder = TEXT("Click Browse to choose file");

	/**
	 * PROCESS-scope, not subsystem-scope, and that is the point.
	 *
	 * "Consume the launch arguments once per process" cannot be expressed with a member: the editor
	 * builds a fresh UGameInstance (and therefore a fresh subsystem) for every PIE session, while
	 * the command line stays visible to FParse the whole time, so a member flag would re-preload on
	 * every Play. Set the instant arguments are found - before any validation - so a launch whose
	 * paths are all bad still counts as consumed instead of repeating its errors next session.
	 */
	static bool GbMobiusCommandLinePreloadConsumed = false;

	/**
	 * Console requests that arrived before any subsystem existed, drained by Initialize().
	 *
	 * -ExecCmds runs its commands from UEngine's deferred-command queue, which in practice is after
	 * the game instance is up, so this is belt-and-braces rather than the normal path. It exists
	 * because the alternative - caching a raw UMobiusStartupPreloadSubsystem* in a global - is a GC
	 * hazard for no benefit.
	 */
	static TArray<TPair<EMobiusPreloadSlot, FString>> GMobiusEarlyConsoleRequests;

	static TAutoConsoleVariable<float> CVarPreloadReadinessTimeout(
		TEXT("Mobius.Preload.TimeoutSeconds"),
		30.0f,
		TEXT("Seconds a queued -Mobius* / Mobius.Load.* file waits for its loader to come online\n")
		TEXT("AFTER the legal notice has been accepted. Time spent on the notice itself is not\n")
		TEXT("counted. Overridden per launch by -MobiusPreloadTimeout=<seconds>."),
		ECVF_Default);

	/** Resolve the live subsystem from a console command's world, falling back to any game world. */
	static UMobiusStartupPreloadSubsystem* ResolvePreloadSubsystem(UWorld* World)
	{
		if (World)
		{
			if (const UGameInstance* GameInstance = World->GetGameInstance())
			{
				if (UMobiusStartupPreloadSubsystem* Found = GameInstance->GetSubsystem<UMobiusStartupPreloadSubsystem>())
				{
					return Found;
				}
			}
		}

		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.OwningGameInstance)
				{
					if (UMobiusStartupPreloadSubsystem* Found = Context.OwningGameInstance->GetSubsystem<UMobiusStartupPreloadSubsystem>())
					{
						return Found;
					}
				}
			}
		}

		return nullptr;
	}

	/**
	 * Rebuild a path from console arguments.
	 *
	 * IConsoleManager splits on whitespace, so "D:\Sim Data\model.fbx" can arrive as three tokens
	 * whether or not the caller quoted it. Rejoining with a single space and stripping one layer of
	 * surrounding quotes handles both forms, at the cost of collapsing runs of spaces inside a
	 * filename - which Windows paths do not use.
	 */
	static FString JoinConsoleArgsAsPath(const TArray<FString>& Args)
	{
		FString Joined = FString::Join(Args, TEXT(" "));
		Joined.TrimStartAndEndInline();
		Joined.RemoveFromStart(TEXT("\""));
		Joined.RemoveFromEnd(TEXT("\""));
		return Joined;
	}

	/** Shared body of the three Mobius.Load.* commands. */
	static void HandleLoadConsoleCommand(EMobiusPreloadSlot Slot, const TArray<FString>& Args, UWorld* World)
	{
		const FString Path = JoinConsoleArgsAsPath(Args);
		if (Path.IsEmpty())
		{
			UE_LOG(LogMobiusPreload, Warning, TEXT("Mobius.Load.%s: no path supplied."),
				UMobiusStartupPreloadSubsystem::GetSlotName(Slot));
			return;
		}

		if (UMobiusStartupPreloadSubsystem* Preload = ResolvePreloadSubsystem(World))
		{
			Preload->RequestLoad(Slot, Path);
			return;
		}

		UE_LOG(LogMobiusPreload, Log,
			TEXT("Mobius.Load.%s queued before the game instance existed; it will run once startup reaches it."),
			UMobiusStartupPreloadSubsystem::GetSlotName(Slot));
		GMobiusEarlyConsoleRequests.Emplace(Slot, Path);
	}

	static void ExecLoadGeometry(const TArray<FString>& Args, UWorld* World)
	{
		HandleLoadConsoleCommand(EMobiusPreloadSlot::Geometry, Args, World);
	}

	static void ExecLoadPedestrian(const TArray<FString>& Args, UWorld* World)
	{
		HandleLoadConsoleCommand(EMobiusPreloadSlot::Pedestrian, Args, World);
	}

	static void ExecLoadBRisk(const TArray<FString>& Args, UWorld* World)
	{
		HandleLoadConsoleCommand(EMobiusPreloadSlot::BRisk, Args, World);
	}

	static void ExecLoadStatus(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (const UMobiusStartupPreloadSubsystem* Preload = ResolvePreloadSubsystem(World))
		{
			UE_LOG(LogMobiusPreload, Display, TEXT("%s"), *Preload->BuildStatusReport());
		}
		else
		{
			UE_LOG(LogMobiusPreload, Warning, TEXT("Mobius.Load.Status: no game instance yet, so nothing to report."));
		}
	}

	static const TCHAR* StateToString(EMobiusPreloadState State)
	{
		switch (State)
		{
		case EMobiusPreloadState::NotRequested:		return TEXT("not requested");
		case EMobiusPreloadState::Waiting:			return TEXT("waiting");
		case EMobiusPreloadState::Dispatched:		return TEXT("dispatched");
		case EMobiusPreloadState::Rejected:			return TEXT("rejected");
		case EMobiusPreloadState::TimedOut:			return TEXT("timed out");
		case EMobiusPreloadState::Cancelled:		return TEXT("cancelled");
		case EMobiusPreloadState::AlreadyLoaded:	return TEXT("already loaded");
		default:									return TEXT("unknown");
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Console commands
//
// NOT wrapped in #if !UE_BUILD_SHIPPING, unlike the diagnostics commands in
// Diagnostics/TrajectoryHeatmapCommands.cpp. These are a supported integration surface: they are how
// a third-party application drives an already-running instance, and how -ExecCmds="Mobius.Load..."
// works. FAutoConsoleCommand still registers in Shipping; only the in-game console UI is compiled
// out, and -ExecCmds does not go through it.
// ---------------------------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GMobiusLoadGeometryCommand(
	TEXT("Mobius.Load.Geometry"),
	TEXT("Load a building/geometry file (.fbx .obj .udatasmith .ifc .wkt .h5) exactly as the Browse button would.\n")
	TEXT("Usage: Mobius.Load.Geometry <path>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecLoadGeometry));

static FAutoConsoleCommandWithWorldAndArgs GMobiusLoadPedestrianCommand(
	TEXT("Mobius.Load.Pedestrian"),
	TEXT("Load a pedestrian trajectory file (.json .h5) exactly as the Browse button would.\n")
	TEXT("Usage: Mobius.Load.Pedestrian <path>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecLoadPedestrian));

static FAutoConsoleCommandWithWorldAndArgs GMobiusLoadBRiskCommand(
	TEXT("Mobius.Load.BRisk"),
	TEXT("Load a B-Risk scenario manifest (.smv) exactly as the Browse button would.\n")
	TEXT("Usage: Mobius.Load.BRisk <path>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecLoadBRisk));

static FAutoConsoleCommandWithWorldAndArgs GMobiusLoadStatusCommand(
	TEXT("Mobius.Load.Status"),
	TEXT("Print what the launch arguments / Mobius.Load.* commands asked for and what happened to each."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecLoadStatus));

// ---------------------------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------------------------

const TCHAR* UMobiusStartupPreloadSubsystem::GetSlotName(EMobiusPreloadSlot Slot)
{
	switch (Slot)
	{
	case EMobiusPreloadSlot::Geometry:		return TEXT("Geometry");
	case EMobiusPreloadSlot::Pedestrian:	return TEXT("Pedestrian");
	case EMobiusPreloadSlot::BRisk:			return TEXT("BRisk");
	default:								return TEXT("Unknown");
	}
}

bool UMobiusStartupPreloadSubsystem::IsExtensionSupportedForSlot(EMobiusPreloadSlot Slot, const FString& FilePath)
{
	const FString Extension = FPaths::GetExtension(FilePath).ToLower();

	switch (Slot)
	{
	case EMobiusPreloadSlot::Geometry:
		// Matches UNativeFileDialogSubsystem::HandleDialogResult's mesh branch exactly.
		return Extension == TEXT("fbx")
			|| Extension == TEXT("obj")
			|| Extension == TEXT("udatasmith")
			|| Extension == TEXT("ifc")
			|| Extension == TEXT("wkt")
			|| Extension == TEXT("h5");

	case EMobiusPreloadSlot::Pedestrian:
		// json/h5 only - see the header comment on IsExtensionSupportedForSlot for why .txt, which
		// HandleDialogResult would accept, is deliberately refused here.
		return Extension == TEXT("json") || Extension == TEXT("h5");

	case EMobiusPreloadSlot::BRisk:
		return Extension == TEXT("smv");

	default:
		return false;
	}
}

FString UMobiusStartupPreloadSubsystem::GetSupportedExtensionsText(EMobiusPreloadSlot Slot)
{
	switch (Slot)
	{
	case EMobiusPreloadSlot::Geometry:		return TEXT(".fbx, .obj, .udatasmith, .ifc, .wkt, .h5");
	case EMobiusPreloadSlot::Pedestrian:	return TEXT(".json, .h5");
	case EMobiusPreloadSlot::BRisk:			return TEXT(".smv");
	default:								return FString();
	}
}

EMobiusPreloadValidation UMobiusStartupPreloadSubsystem::ValidatePathForSlot(EMobiusPreloadSlot Slot, const FString& FilePath)
{
	FString Trimmed = FilePath;
	Trimmed.TrimStartAndEndInline();

	if (Trimmed.IsEmpty() || Trimmed.Equals(MobiusFilePathPlaceholder))
	{
		return EMobiusPreloadValidation::EmptyPath;
	}

	// Extension first: a path with the wrong extension is better explained as "wrong type" than as
	// "not found", and this ordering also keeps the check testable without touching disk.
	if (!IsExtensionSupportedForSlot(Slot, Trimmed))
	{
		return EMobiusPreloadValidation::UnsupportedExtension;
	}

	// A directory is excluded explicitly: it would sail past a naive existence check and then fail
	// deep inside an importer with a far worse message.
	if (FPaths::DirectoryExists(Trimmed) || !FPaths::FileExists(Trimmed))
	{
		return EMobiusPreloadValidation::NotAFile;
	}

	return EMobiusPreloadValidation::Ok;
}

FMobiusPreloadCommandLine UMobiusStartupPreloadSubsystem::ParseCommandLine(const TCHAR* CommandLine)
{
	FMobiusPreloadCommandLine Result;
	if (!CommandLine)
	{
		return Result;
	}

	// FParse::Value handles -Token="value with spaces" as well as -Token=value; the leading '-' is
	// not part of the search token.
	FParse::Value(CommandLine, TEXT("MobiusGeometry="), Result.GeometryPath);
	FParse::Value(CommandLine, TEXT("MobiusPedestrian="), Result.PedestrianPath);
	FParse::Value(CommandLine, TEXT("MobiusBRisk="), Result.BRiskPath);
	FParse::Value(CommandLine, TEXT("MobiusPreloadTimeout="), Result.ReadinessTimeoutSeconds);

	Result.GeometryPath.TrimStartAndEndInline();
	Result.PedestrianPath.TrimStartAndEndInline();
	Result.BRiskPath.TrimStartAndEndInline();

	return Result;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

void UMobiusStartupPreloadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Subsystem creation order within a collection is not defined, and a rejected path reports through
	// UMobiusUserFeedbackSubsystem from inside THIS function. Without this the sibling did not exist
	// yet, GetSubsystem returned null, and the "file not found" window for a bad launch argument was
	// deferred and then dropped - measured, not theorised (see the Queue.Num() == 0 branch in
	// ProcessGate for the surviving fallback).
	Collection.InitializeDependency<UMobiusUserFeedbackSubsystem>();

	// Console requests that beat the game instance into existence (see GMobiusEarlyConsoleRequests).
	if (GMobiusEarlyConsoleRequests.Num() > 0)
	{
		const TArray<TPair<EMobiusPreloadSlot, FString>> Early = MoveTemp(GMobiusEarlyConsoleRequests);
		GMobiusEarlyConsoleRequests.Reset();
		for (const TPair<EMobiusPreloadSlot, FString>& Request : Early)
		{
			RequestLoad(Request.Key, Request.Value);
		}
	}

	if (GbMobiusCommandLinePreloadConsumed)
	{
		return;
	}

	const FMobiusPreloadCommandLine Parsed = ParseCommandLine(FCommandLine::Get());
	if (!Parsed.HasAnyPath())
	{
		// No launch arguments. The consumed flag is deliberately NOT set: nothing was consumed, so a
		// later world in the same process is still free to pick arguments up. Costs one FParse.
		return;
	}

	GbMobiusCommandLinePreloadConsumed = true;
	TimeoutOverrideSeconds = Parsed.ReadinessTimeoutSeconds;

	UE_LOG(LogMobiusPreload, Log,
		TEXT("Launch preload requested. Geometry='%s' Pedestrian='%s' BRisk='%s'"),
		Parsed.GeometryPath.IsEmpty() ? TEXT("<none>") : *Parsed.GeometryPath,
		Parsed.PedestrianPath.IsEmpty() ? TEXT("<none>") : *Parsed.PedestrianPath,
		Parsed.BRiskPath.IsEmpty() ? TEXT("<none>") : *Parsed.BRiskPath);

	// Independently queued: one bad path never stops the others.
	if (!Parsed.GeometryPath.IsEmpty())
	{
		RequestLoad(EMobiusPreloadSlot::Geometry, Parsed.GeometryPath);
	}
	if (!Parsed.PedestrianPath.IsEmpty())
	{
		RequestLoad(EMobiusPreloadSlot::Pedestrian, Parsed.PedestrianPath);
	}
	if (!Parsed.BRiskPath.IsEmpty())
	{
		RequestLoad(EMobiusPreloadSlot::BRisk, Parsed.BRiskPath);
	}

	// The "holding" line is emitted HERE, not from the ticker, for the launch-argument path.
	//
	// Two reasons it cannot be left to TickGate. (1) Count: RequestLoad is called once per supplied
	// file, so a per-call log reports 1 for a launch that supplied three. (2) It might never run at
	// all: the notice is opened from a next-tick timer after PostLoadMapWithWorld via
	// FSlateApplication::AddModalWindow, whose blocking form pumps only Slate - the engine tick, and
	// with it FTSTicker, is paused for as long as the notice is up. So there is no guarantee of a tick
	// between here and the modal, nor any during it. Setting the latch suppresses the ticker's
	// duplicate if one does happen to land first.
	if (Queue.Num() > 0 && !IsLegalGateOpen() && !IsLegalGateUnreachable())
	{
		bLoggedWaitingOnLegalGate = true;
		UE_LOG(LogMobiusPreload, Log,
			TEXT("Holding %d preload request(s) until the legal notice is accepted."), Queue.Num());
	}
}

void UMobiusStartupPreloadSubsystem::Deinitialize()
{
	StopTicker();
	Queue.Reset();
	DeferredNotices.Reset();
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------

bool UMobiusStartupPreloadSubsystem::RequestLoad(EMobiusPreloadSlot Slot, const FString& FilePath)
{
	FString Path = FilePath;
	Path.TrimStartAndEndInline();
	Path.RemoveFromStart(TEXT("\""));
	Path.RemoveFromEnd(TEXT("\""));

	FSlotOutcome& Outcome = OutcomeFor(Slot);
	Outcome.Path = Path;

	const EMobiusPreloadValidation Validation = ValidatePathForSlot(Slot, Path);
	if (Validation != EMobiusPreloadValidation::Ok)
	{
		Outcome.State = EMobiusPreloadState::Rejected;

		FDeferredNotice Notice;
		Notice.TitleBar = LOCTEXT("PreloadTitleBar", "Startup File Load");
		Notice.Severity = EMobiusErrorSeverity::Error;
		Notice.bShowPrompt = true;

		const FText SlotText = FText::FromString(GetSlotName(Slot));

		switch (Validation)
		{
		case EMobiusPreloadValidation::EmptyPath:
			Outcome.Detail = TEXT("empty path");
			Notice.Title = LOCTEXT("PreloadEmptyTitle", "No file path supplied");
			Notice.Body = FText::Format(
				LOCTEXT("PreloadEmptyBody", "The {0} file could not be preloaded because no path was supplied."),
				SlotText);
			// A blank argument is a caller bug the user cannot act on - log it, do not interrupt them.
			Notice.Severity = EMobiusErrorSeverity::Warning;
			Notice.bShowPrompt = false;
			break;

		case EMobiusPreloadValidation::NotAFile:
			Outcome.Detail = TEXT("file not found");
			Notice.Title = LOCTEXT("PreloadNotFoundTitle", "File not found");
			Notice.Body = FText::Format(
				LOCTEXT("PreloadNotFoundBody", "The {0} file supplied at launch could not be opened because it does not exist, or is a folder:\n\n{1}"),
				SlotText, FText::FromString(Path));
			break;

		case EMobiusPreloadValidation::UnsupportedExtension:
		default:
			Outcome.Detail = TEXT("unsupported file type");
			Notice.Title = LOCTEXT("PreloadBadTypeTitle", "Unsupported file type");
			Notice.Body = FText::Format(
				LOCTEXT("PreloadBadTypeBody", "The {0} file supplied at launch is not a supported type.\n\nSupplied: {1}\nSupported: {2}"),
				SlotText, FText::FromString(Path), FText::FromString(GetSupportedExtensionsText(Slot)));
			break;
		}

		UE_LOG(LogMobiusPreload, Warning, TEXT("%s rejected (%s): '%s'"),
			GetSlotName(Slot), *Outcome.Detail, *Path);

		ReportOrDefer(Notice);
		PumpGateAndUpdateTicker();
		return false;
	}

	// Replace any earlier pending request for the same slot - the newest wins, which is what a
	// second Mobius.Load.<slot> issued before the first one dispatched should mean.
	Queue.RemoveAll([Slot](const FQueuedRequest& Existing) { return Existing.Slot == Slot; });

	// EMPTY -> NON-EMPTY starts a new BATCH, and the readiness clock plus the one-shot log latches
	// belong to the batch, not to the process. Without this reset a Mobius.Load.* command issued after
	// a startup preload has already opened the gate would be measured against THAT gate-open time -
	// minutes stale - so its very first timeout check fires immediately and it is abandoned with
	// "the loader never became available" having never waited at all.
	if (Queue.Num() == 0)
	{
		GateOpenedSeconds = -1.0;
		bLoggedWaitingOnLegalGate = false;
		bLoggedWaitingOnListeners = false;
	}

	FQueuedRequest Queued;
	Queued.Slot = Slot;
	Queued.Path = Path;
	Queue.Add(MoveTemp(Queued));

	Outcome.State = EMobiusPreloadState::Waiting;
	Outcome.Detail.Reset();

	// Try immediately, so the console-command case (world already up, notice already accepted)
	// dispatches inside this call rather than up to one ticker period later.
	PumpGateAndUpdateTicker();
	return true;
}

FString UMobiusStartupPreloadSubsystem::BuildStatusReport() const
{
	const UProjectMobiusGameInstance* GameInstance = Cast<UProjectMobiusGameInstance>(GetGameInstance());

	const TCHAR* GateText = IsLegalGateOpen()
		? TEXT("open")
		: (IsLegalGateUnreachable()
			? TEXT("UNREACHABLE (unattended, notice never accepted)")
			: TEXT("waiting for acceptance"));

	FString Report = TEXT("Mobius startup preload status\n");
	Report += FString::Printf(TEXT("  legal gate            : %s\n"), GateText);
	Report += FString::Printf(TEXT("  command line consumed : %s\n"),
		GbMobiusCommandLinePreloadConsumed ? TEXT("yes") : TEXT("no"));
	Report += FString::Printf(TEXT("  still queued          : %d\n"), Queue.Num());

	for (int32 Index = 0; Index < NumSlots; ++Index)
	{
		const EMobiusPreloadSlot Slot = static_cast<EMobiusPreloadSlot>(Index);
		const FSlotOutcome& Outcome = Outcomes[Index];

		Report += FString::Printf(TEXT("  %s\n"), GetSlotName(Slot));
		Report += FString::Printf(TEXT("      requested : %s\n"),
			Outcome.Path.IsEmpty() ? TEXT("<none>") : *Outcome.Path);
		const FString DetailSuffix = Outcome.Detail.IsEmpty()
			? FString()
			: FString::Printf(TEXT(" (%s)"), *Outcome.Detail);
		Report += FString::Printf(TEXT("      state     : %s%s\n"),
			StateToString(Outcome.State), *DetailSuffix);
		Report += FString::Printf(TEXT("      listener  : %s\n"),
			IsListenerReadyForSlot(Slot) ? TEXT("ready") : TEXT("not bound"));

		if (GameInstance)
		{
			FString Live;
			switch (Slot)
			{
			case EMobiusPreloadSlot::Geometry:		Live = GameInstance->GetSimulationMeshFilePath(); break;
			case EMobiusPreloadSlot::Pedestrian:	Live = GameInstance->GetPedestrianDataFilePath(); break;
			case EMobiusPreloadSlot::BRisk:			Live = GameInstance->GetBRiskSmvFilePath(); break;
			default: break;
			}
			Report += FString::Printf(TEXT("      game inst : %s\n"), *Live);
		}
	}

	return Report;
}

// ---------------------------------------------------------------------------------------------
// Gate
// ---------------------------------------------------------------------------------------------

void UMobiusStartupPreloadSubsystem::EnsureTickerRunning()
{
	if (TickerHandle.IsValid())
	{
		return;
	}

	// FTSTicker rather than a world or game-instance timer: this has to be able to run before any
	// world exists (an -ExecCmds request can land that early) and must not care which world is
	// current. 10 Hz is a handful of boolean reads per tick, and the queue is normally drained
	// within the first frames after the world begins play.
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMobiusStartupPreloadSubsystem::TickGate), 0.1f);
}

void UMobiusStartupPreloadSubsystem::StopTicker()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

bool UMobiusStartupPreloadSubsystem::TickGate(float /*DeltaTime*/)
{
	const bool bKeepTicking = ProcessGate(/*bFromTicker=*/true);
	if (!bKeepTicking)
	{
		// Returning false makes FTSTicker remove this ticker itself, so only the handle is dropped
		// here. Calling StopTicker() instead would remove a ticker that is mid-execution.
		TickerHandle.Reset();
	}
	return bKeepTicking;
}

void UMobiusStartupPreloadSubsystem::PumpGateAndUpdateTicker()
{
	if (ProcessGate(/*bFromTicker=*/false))
	{
		EnsureTickerRunning();
	}
	else
	{
		// Not inside the ticker here, so the removal has to be explicit.
		StopTicker();
	}
}

bool UMobiusStartupPreloadSubsystem::ProcessGate(const bool bFromTicker)
{
	// The legal notice's decline path calls FPlatformMisc::RequestExit. Anything queued is abandoned
	// rather than raced against teardown, and no error window is raised: a user who just declined the
	// licence does not need a file complaint on the way out.
	if (IsEngineExitRequested())
	{
		if (Queue.Num() > 0 || DeferredNotices.Num() > 0)
		{
			UE_LOG(LogMobiusPreload, Log,
				TEXT("Application is exiting - discarding %d queued preload request(s)."), Queue.Num());
		}
		AbandonQueue(EMobiusPreloadState::Cancelled, TEXT("application exited before loading"));
		DeferredNotices.Reset();
		return false;
	}

	if (!IsLegalGateOpen())
	{
		if (IsLegalGateUnreachable())
		{
			// Fail fast rather than burning the timeout on a gate that can never open.
			UE_LOG(LogMobiusPreload, Error,
				TEXT("Refusing to preload: the legal notice has never been accepted and cannot be shown in this mode ")
				TEXT("(unattended). Launch without -unattended, accept the notice once, then retry."));
			AbandonQueue(EMobiusPreloadState::Rejected, TEXT("legal notice not accepted and cannot be shown"));
			DeferredNotices.Reset();
			return false;
		}

		if (bFromTicker && !bLoggedWaitingOnLegalGate && Queue.Num() > 0)
		{
			bLoggedWaitingOnLegalGate = true;
			UE_LOG(LogMobiusPreload, Log,
				TEXT("Holding %d preload request(s) until the legal notice is accepted."), Queue.Num());
		}
		return true;
	}

	if (GateOpenedSeconds < 0.0)
	{
		GateOpenedSeconds = FPlatformTime::Seconds();
	}

	// Safe to raise UI now: nothing would be stacked over the mandatory notice.
	FlushDeferredNotices();

	const float Timeout = TimeoutOverrideSeconds > 0.0f
		? TimeoutOverrideSeconds
		: CVarPreloadReadinessTimeout.GetValueOnGameThread();
	const bool bDeadlinePassed = Timeout > 0.0f
		&& (FPlatformTime::Seconds() - GateOpenedSeconds) > static_cast<double>(Timeout);

	// A world that has begun play is the precondition for the level-actor listeners (notably
	// ARuntimeMeshBuilder) to have bound at all, and for the importers' GetWorld() calls to resolve.
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World && World->IsGameWorld() && World->HasBegunPlay())
	{
		for (int32 Index = Queue.Num() - 1; Index >= 0; --Index)
		{
			const FQueuedRequest Request = Queue[Index];
			if (!IsListenerReadyForSlot(Request.Slot))
			{
				continue;
			}

			Queue.RemoveAt(Index);
			Dispatch(Request.Slot, Request.Path);
		}
	}

	if (Queue.Num() == 0)
	{
		if (DeferredNotices.Num() == 0)
		{
			return false;
		}

		// Nothing left to load but a message still undelivered. KEEP TICKING rather than dropping it.
		//
		// This is the ordinary state for a validation failure raised from Initialize(): ReportOrDefer
		// runs while the GameInstance subsystem collection is still being built, so
		// UMobiusUserFeedbackSubsystem may not exist yet and FlushDeferredNotices has nothing to report
		// through. Dropping here is exactly how a launcher passing a stale path - the single most likely
		// real-world failure - produced a log line and no window at all. (Initialize now also asks the
		// collection for the feedback subsystem up front, so this is the belt to that braces: it still
		// covers a request that arrives before any game instance exists.)
		if (!bDeadlinePassed)
		{
			return true;
		}

		for (const FDeferredNotice& Notice : DeferredNotices)
		{
			UE_LOG(LogMobiusPreload, Warning, TEXT("Undelivered preload notice: %s - %s"),
				*Notice.Title.ToString(), *Notice.Body.ToString());
		}
		DeferredNotices.Reset();
		return false;
	}

	if (bFromTicker && !bLoggedWaitingOnListeners)
	{
		bLoggedWaitingOnListeners = true;
		UE_LOG(LogMobiusPreload, Log,
			TEXT("Waiting for the world and loaders to come online before dispatching %d preload request(s)."),
			Queue.Num());
	}

	if (bDeadlinePassed)
	{
		for (const FQueuedRequest& Request : Queue)
		{
			UE_LOG(LogMobiusPreload, Error,
				TEXT("%s preload timed out after %.1fs: nothing in the loaded level is listening for this file ('%s')."),
				GetSlotName(Request.Slot), Timeout, *Request.Path);

			FDeferredNotice Notice;
			Notice.TitleBar = LOCTEXT("PreloadTitleBar", "Startup File Load");
			Notice.Title = LOCTEXT("PreloadTimeoutTitle", "File could not be loaded at startup");
			Notice.Body = FText::Format(
				LOCTEXT("PreloadTimeoutBody", "The {0} file supplied at launch was not loaded because the loader for it never became available:\n\n{1}\n\nUse the Browse button to load it manually."),
				FText::FromString(GetSlotName(Request.Slot)), FText::FromString(Request.Path));
			Notice.Severity = EMobiusErrorSeverity::Error;
			Notice.bShowPrompt = true;
			DeferredNotices.Add(Notice);
		}

		AbandonQueue(EMobiusPreloadState::TimedOut, TEXT("loader never became available"));
		FlushDeferredNotices();
		return false;
	}

	return true;
}

bool UMobiusStartupPreloadSubsystem::IsLegalGateOpen() const
{
	// Mirrors FMobiusWidgetsModule::HandlePostLoadMap's GIsEditor early-return: the notice is a
	// packaged-only dialog, so in the editor and in PIE there is nothing to wait for and no
	// acceptance will ever be recorded. Gating on it there would deadlock every PIE session.
	if (GIsEditor)
	{
		return true;
	}

	const UUserProjectSettings* UserSettings =
		Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr);

	return UserSettings && UserSettings->HasAcceptedCurrentLegalNotice();
}

bool UMobiusStartupPreloadSubsystem::IsLegalGateUnreachable() const
{
	if (GIsEditor)
	{
		return false;
	}
	if (!FApp::IsUnattended())
	{
		return false;
	}

	const UUserProjectSettings* UserSettings =
		Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr);

	return !UserSettings || !UserSettings->HasAcceptedCurrentLegalNotice();
}

bool UMobiusStartupPreloadSubsystem::IsListenerReadyForSlot(EMobiusPreloadSlot Slot) const
{
	const UProjectMobiusGameInstance* GameInstance = Cast<UProjectMobiusGameInstance>(GetGameInstance());
	if (!GameInstance)
	{
		return false;
	}

	switch (Slot)
	{
	case EMobiusPreloadSlot::Geometry:
		// ARuntimeMeshBuilder::BeginPlay is the only binder. A level with no ARuntimeMeshBuilder
		// leaves this false forever, which is precisely the case the timeout exists to report.
		return GameInstance->OnMeshFileChanged.IsBound();

	case EMobiusPreloadSlot::Pedestrian:
		// SetPedestrianDataFilePath broadcasts BOTH: ...Updated drives the actual load
		// (UMassEntitySpawnSubsystem::CreatePedestrianTemplateData) while ...Changed carries the path
		// to AMobiusController for screenshot naming. Requiring both means the controller's BeginPlay
		// has run, so a preloaded file names screenshots the same way a browsed one does.
		return GameInstance->OnPedestrianVectorFileUpdated.IsBound()
			&& GameInstance->OnPedestrianVectorFileChanged.IsBound();

	case EMobiusPreloadSlot::BRisk:
		return GameInstance->OnBRiskFileChanged.IsBound();

	default:
		return false;
	}
}

void UMobiusStartupPreloadSubsystem::Dispatch(EMobiusPreloadSlot Slot, const FString& Path)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	UProjectMobiusGameInstance* GameInstance = Cast<UProjectMobiusGameInstance>(GetGameInstance());
	if (!World || !GameInstance)
	{
		return;
	}

	FSlotOutcome& Outcome = OutcomeFor(Slot);

	switch (Slot)
	{
	case EMobiusPreloadSlot::Geometry:
		// SetSimulationMeshFilePath guards on Old != New, so re-issuing the live path would silently
		// do nothing. Say so rather than reporting a success that never happened.
		if (GameInstance->GetSimulationMeshFilePath() == Path)
		{
			Outcome.State = EMobiusPreloadState::AlreadyLoaded;
			Outcome.Detail = TEXT("game instance already holds this path; no reload");
			UE_LOG(LogMobiusPreload, Warning,
				TEXT("Geometry '%s' is already the loaded file - no reload was triggered."), *Path);
			return;
		}
		// The exact call ULoadMeshWidget::UpdateMobiusGameInstanceData makes after a Browse selection.
		IProjectMobiusInterface::UpdateMobiusGameInstanceMeshDataFile(World, Path);
		break;

	case EMobiusPreloadSlot::Pedestrian:
		// SetPedestrianDataFilePath has no equality guard, so re-issuing the same path IS a genuine
		// reload. No already-loaded shortcut here, deliberately.
		// The exact call ULoadAgentDataWidget::UpdateMobiusGameInstanceData makes.
		IProjectMobiusInterface::UpdateMobiusGameInstancePedestrianData(World, Path);
		break;

	case EMobiusPreloadSlot::BRisk:
		if (GameInstance->GetBRiskSmvFilePath() == Path)
		{
			Outcome.State = EMobiusPreloadState::AlreadyLoaded;
			Outcome.Detail = TEXT("game instance already holds this path; no reload");
			UE_LOG(LogMobiusPreload, Warning,
				TEXT("B-Risk scenario '%s' is already the loaded file - no reload was triggered."), *Path);
			return;
		}
		// The exact call ULoadBRiskDataWidget::UpdateMobiusGameInstanceData makes.
		GameInstance->SetBRiskSmvFilePath(Path);
		break;

	default:
		return;
	}

	Outcome.State = EMobiusPreloadState::Dispatched;
	Outcome.Detail.Reset();

	UE_LOG(LogMobiusPreload, Log, TEXT("%s preload dispatched: %s"), GetSlotName(Slot), *Path);
}

void UMobiusStartupPreloadSubsystem::ReportOrDefer(const FDeferredNotice& Notice)
{
	if (IsLegalGateOpen())
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UMobiusUserFeedbackSubsystem>()
			: nullptr)
		{
			Feedback->ReportError(Notice.TitleBar, Notice.Title, Notice.Body,
				FText::FromString(TEXT("MobiusStartupPreloadSubsystem")), Notice.Severity, Notice.bShowPrompt);
			return;
		}
	}

	DeferredNotices.Add(Notice);
}

void UMobiusStartupPreloadSubsystem::FlushDeferredNotices()
{
	if (DeferredNotices.Num() == 0)
	{
		return;
	}

	UMobiusUserFeedbackSubsystem* Feedback = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMobiusUserFeedbackSubsystem>()
		: nullptr;
	if (!Feedback)
	{
		return;
	}

	const TArray<FDeferredNotice> ToSend = MoveTemp(DeferredNotices);
	DeferredNotices.Reset();
	for (const FDeferredNotice& Notice : ToSend)
	{
		Feedback->ReportError(Notice.TitleBar, Notice.Title, Notice.Body,
			FText::FromString(TEXT("MobiusStartupPreloadSubsystem")), Notice.Severity, Notice.bShowPrompt);
	}
}

void UMobiusStartupPreloadSubsystem::AbandonQueue(EMobiusPreloadState NewState, const FString& Reason)
{
	for (const FQueuedRequest& Request : Queue)
	{
		FSlotOutcome& Outcome = OutcomeFor(Request.Slot);
		Outcome.State = NewState;
		Outcome.Detail = Reason;
	}
	Queue.Reset();
}

#undef LOCTEXT_NAMESPACE
