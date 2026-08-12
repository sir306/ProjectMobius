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

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Interfaces/ProjectMobiusInterface.h"
#include "MobiusErrorTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MobiusStartupPreloadSubsystem.generated.h"

/**
 * The three independent data inputs a third-party launcher can preload. Independent by design:
 * any subset may be supplied, and one slot failing never blocks the others.
 */
UENUM(BlueprintType)
enum class EMobiusPreloadSlot : uint8
{
	/** Building / geometry mesh. Drives UProjectMobiusGameInstance::SetSimulationMeshFilePath. */
	Geometry	UMETA(DisplayName = "Geometry (building mesh)"),
	/** Pedestrian trajectory data. Drives UProjectMobiusGameInstance::SetPedestrianDataFilePath. */
	Pedestrian	UMETA(DisplayName = "Pedestrian vector data"),
	/** B-Risk .smv scenario manifest. Drives UProjectMobiusGameInstance::SetBRiskSmvFilePath. */
	BRisk		UMETA(DisplayName = "B-Risk scenario (.smv)")
};

/** Why a supplied path was refused before it was ever queued. */
UENUM()
enum class EMobiusPreloadValidation : uint8
{
	Ok,
	/** Path was blank, or was the "Click Browse to choose file" placeholder. */
	EmptyPath,
	/** Nothing on disk at that path, or it resolved to a directory. */
	NotAFile,
	/** Exists, but the extension is not one this slot's Browse dialog accepts. */
	UnsupportedExtension
};

/** State of one slot, for Mobius.Load.Status. */
UENUM()
enum class EMobiusPreloadState : uint8
{
	/** Never asked for. */
	NotRequested,
	/** Queued, waiting on the legal-notice gate and/or its listener. */
	Waiting,
	/** Path pushed into the game instance; the async import owns it from here. */
	Dispatched,
	/** Refused at validation time (see EMobiusPreloadValidation). */
	Rejected,
	/** Gate never opened inside the readiness timeout. */
	TimedOut,
	/** Abandoned because the app is shutting down (e.g. legal notice declined). */
	Cancelled,
	/** The game instance already held this exact path, so its setter would no-op. */
	AlreadyLoaded
};

/**
 * Parsed -Mobius* launch arguments.
 *
 * Deliberately a plain struct, not a USTRUCT: it is produced from a raw command-line string so the
 * parser can be unit-tested without an engine command line (see StartupPreloadTest.cpp).
 */
struct FMobiusPreloadCommandLine
{
	FString GeometryPath;
	FString PedestrianPath;
	FString BRiskPath;

	/**
	 * Seconds to wait for a slot's listener AFTER the legal gate opens. <= 0 means "use the
	 * Mobius.Preload.TimeoutSeconds cvar". Human think-time on the legal notice is never counted
	 * against this (the clock starts when the gate opens), so it only has to cover engine startup.
	 */
	float ReadinessTimeoutSeconds = 0.0f;

	bool HasAnyPath() const
	{
		return !GeometryPath.IsEmpty() || !PedestrianPath.IsEmpty() || !BRiskPath.IsEmpty();
	}
};

/**
 * Preloads geometry / pedestrian / B-Risk files supplied on the command line (or by console
 * command), so a third-party application can launch Mobius straight into a prepared scenario.
 *
 * WHY THIS EXISTS AS A SEPARATE SUBSYSTEM RATHER THAN CODE IN THE GAME INSTANCE
 * ----------------------------------------------------------------------------
 * Setting a file path is trivial; deciding WHEN it is safe to set one is not, and that decision has
 * three hard constraints that all want to live in one place:
 *
 *  1. LISTENER READINESS. Each of the three paths is a fire-and-forget multicast broadcast from
 *     UProjectMobiusGameInstance. If nothing is bound yet the broadcast is silently dropped - no
 *     error, no log, no load. The geometry path is the dangerous one: OnMeshFileChanged is bound in
 *     ARuntimeMeshBuilder::BeginPlay, i.e. by a LEVEL ACTOR, so it does not exist until the world
 *     has begun play. This class therefore gates each slot on its own delegate's IsBound() rather
 *     than on any single coarse "world is ready" signal - which is also what keeps the three slots
 *     independent, since a missing ARuntimeMeshBuilder can then fail geometry alone.
 *
 *  2. THE LEGAL NOTICE. On a packaged first launch FMobiusWidgetsModule::HandlePostLoadMap opens a
 *     mandatory modal notice, and declining it quits the application. Importing third-party data
 *     behind that notice is wrong, and starting an async import that is then torn down mid-flight by
 *     FPlatformMisc::RequestExit is asking for trouble. So nothing is dispatched until acceptance is
 *     recorded, and IsEngineExitRequested() abandons the whole queue.
 *
 *  3. ONCE PER PROCESS. Command-line arguments are consumed exactly once per process (see the
 *     file-static guard in the .cpp). In the editor that means the first PIE session of an editor
 *     run preloads and later ones do not - re-trigger those with the Mobius.Load.* console commands.
 *
 * WHAT IT DOES *NOT* DO
 * ---------------------
 * It does not re-implement any loading. It ends at exactly the same game-instance setters that
 * ULoadMeshWidget / ULoadAgentDataWidget / ULoadBRiskDataWidget call from their file-dialog
 * callbacks, so every importer, cache, MASS reset and visualiser downstream runs bit-identically to
 * a human clicking Browse. The on-screen file fields update because the ULoad*Widget classes now
 * observe those same delegates (ULoadDataParentWidget::RefreshFromGameInstance) - this class never
 * touches a widget, so it does not care whether the HUD has been constructed yet.
 */
UCLASS()
class MOBIUSCORE_API UMobiusStartupPreloadSubsystem : public UGameInstanceSubsystem, public IProjectMobiusInterface
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Queue a file for loading, then dispatch it as soon as its gate opens.
	 *
	 * Safe to call at any time, including before a world exists - it queues and the ticker drains.
	 * Validation failures are reported (deferred behind the legal notice if it is still up) and
	 * return false; a queued request returns true.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Preload")
	bool RequestLoad(EMobiusPreloadSlot Slot, const FString& FilePath);

	/** Human-readable dump of what was asked for and what happened. Backs Mobius.Load.Status. */
	FString BuildStatusReport() const;

	// ---- Pure helpers. Static + world-free so ProjectMobiusTests can exercise them directly. ----

	/**
	 * Extensions each slot accepts.
	 *
	 * Mirrors what a Browse selection actually gets through UNativeFileDialogSubsystem, with ONE
	 * deliberate tightening: the agent slot accepts json/h5 only. UNativeFileDialogSubsystem's
	 * HandleDialogResult also lets ".txt" set bAgentSuccess, but its own IsAgentFileSupported helper
	 * and the pfd filter ("*.json *.h5") both disagree, and a launch argument - unlike a dialog -
	 * can hand us an arbitrary string. Rejecting .txt here produces a clear message instead of
	 * pushing an unreadable file into the importer.
	 */
	static bool IsExtensionSupportedForSlot(EMobiusPreloadSlot Slot, const FString& FilePath);

	/** Comma-separated extension list for a slot, for error messages. */
	static FString GetSupportedExtensionsText(EMobiusPreloadSlot Slot);

	/** Full pre-queue check: placeholder/blank, exists on disk, is a file, extension allowed. */
	static EMobiusPreloadValidation ValidatePathForSlot(EMobiusPreloadSlot Slot, const FString& FilePath);

	/** Parse -MobiusGeometry= / -MobiusPedestrian= / -MobiusBRisk= / -MobiusPreloadTimeout= . */
	static FMobiusPreloadCommandLine ParseCommandLine(const TCHAR* CommandLine);

	/** Display name used in logs, errors and the status report. */
	static const TCHAR* GetSlotName(EMobiusPreloadSlot Slot);

	/** Number of entries in EMobiusPreloadSlot. */
	static constexpr int32 NumSlots = 3;

private:
	/** One queued, validated request. */
	struct FQueuedRequest
	{
		EMobiusPreloadSlot Slot = EMobiusPreloadSlot::Geometry;
		FString Path;
	};

	/**
	 * A user-facing message held back until the legal notice has been dealt with. Reporting a
	 * "file not found" popup on top of a mandatory modal notice would stack a window the user
	 * cannot reach over the one they must answer.
	 */
	struct FDeferredNotice
	{
		FText TitleBar;
		FText Title;
		FText Body;
		EMobiusErrorSeverity Severity = EMobiusErrorSeverity::Error;
		bool bShowPrompt = true;
	};

	/** Per-slot record for the status report. */
	struct FSlotOutcome
	{
		EMobiusPreloadState State = EMobiusPreloadState::NotRequested;
		FString Path;
		FString Detail;
	};

	/**
	 * The gate itself: checks exit / legal / world / listener readiness, dispatches everything now
	 * eligible, and returns true if there is still a reason to be called again.
	 *
	 * Deliberately separate from TickGate so it can also be driven synchronously from RequestLoad.
	 * The two callers must dispose of the ticker differently - see PumpGateAndUpdateTicker.
	 *
	 * bFromTicker suppresses the one-shot "holding / waiting for N request(s)" log lines on the
	 * synchronous path. Initialize() calls RequestLoad once per supplied file, so the first of those
	 * calls would emit a count of 1 and latch the one-shot flag, permanently under-reporting a launch
	 * that supplied two or three files. By the first real tick the whole queue is populated.
	 */
	bool ProcessGate(bool bFromTicker);

	/** FTSTicker body. Wraps ProcessGate; drops the handle when the ticker self-removes. */
	bool TickGate(float DeltaTime);

	/**
	 * Run ProcessGate from OUTSIDE the ticker and reconcile the ticker's existence with the result.
	 *
	 * TickGate cannot share this: returning false from a ticker delegate makes FTSTicker remove that
	 * ticker itself, so TickGate must only drop the handle, whereas this path has to call
	 * RemoveTicker explicitly. Getting that backwards either leaks a live ticker whose handle has
	 * been forgotten (so EnsureTickerRunning adds a second one) or removes a ticker mid-execution.
	 */
	void PumpGateAndUpdateTicker();

	/** Starts the ticker if it is not already running. */
	void EnsureTickerRunning();

	/** Stops the ticker. Safe to call when it is not running. Never call from inside TickGate. */
	void StopTicker();

	/**
	 * True once it is legitimate to import third-party data.
	 *
	 * Editor and PIE: always true. FMobiusWidgetsModule::HandlePostLoadMap early-returns on
	 * GIsEditor, so the notice is never shown there and acceptance can never be recorded; gating on
	 * it would deadlock every PIE session.
	 *
	 * Packaged: requires UUserProjectSettings::HasAcceptedCurrentLegalNotice(). Note this is
	 * checked WITHOUT reference to whether the notice was displayed - -unattended suppresses the
	 * notice but is deliberately NOT treated as consent (see IsLegalGateUnreachable).
	 */
	bool IsLegalGateOpen() const;

	/**
	 * True when the gate can never open, so waiting is pointless and the queue should be refused
	 * immediately rather than after the timeout.
	 *
	 * The case: a packaged build launched -unattended (or -unattendedscript) with no acceptance on
	 * record. HandlePostLoadMap early-returns on FApp::IsUnattended(), and FSlateApplication::
	 * AddModalWindow cancels modals outright under GIsRunningUnattendedScript, so no notice will
	 * ever appear and no consent will ever be recorded. Preload refuses instead of proceeding: an
	 * unattended flag must not become a route around the acceptance requirement.
	 */
	bool IsLegalGateUnreachable() const;

	/** True when this slot's game-instance delegate has a listener, so a broadcast will be seen. */
	bool IsListenerReadyForSlot(EMobiusPreloadSlot Slot) const;

	/** Pushes the path into the game instance - the same call the Browse callbacks make. */
	void Dispatch(EMobiusPreloadSlot Slot, const FString& Path);

	/** Queue a message for delivery once the legal gate is open (or send it now if it already is). */
	void ReportOrDefer(const FDeferredNotice& Notice);

	/** Flush anything ReportOrDefer held back. */
	void FlushDeferredNotices();

	/** Abandon every queued request. Used on engine exit and on timeout. */
	void AbandonQueue(EMobiusPreloadState NewState, const FString& Reason);

	FSlotOutcome& OutcomeFor(EMobiusPreloadSlot Slot) { return Outcomes[static_cast<int32>(Slot)]; }
	const FSlotOutcome& OutcomeFor(EMobiusPreloadSlot Slot) const { return Outcomes[static_cast<int32>(Slot)]; }

	/** Still-pending requests, drained by TickGate. */
	TArray<FQueuedRequest> Queue;

	/** Messages held behind the legal notice. */
	TArray<FDeferredNotice> DeferredNotices;

	/** Per-slot status, indexed by EMobiusPreloadSlot. */
	FSlotOutcome Outcomes[NumSlots];

	FTSTicker::FDelegateHandle TickerHandle;

	/**
	 * FPlatformTime::Seconds() at which the legal gate first opened, or < 0 while it is still shut.
	 * The readiness timeout is measured from here, NOT from queue time, so a user taking five
	 * minutes to read the licence notice does not consume the budget meant for engine startup.
	 */
	double GateOpenedSeconds = -1.0;

	/** Timeout from -MobiusPreloadTimeout=, or <= 0 to use the cvar. */
	float TimeoutOverrideSeconds = 0.0f;

	/** Logged-once flags, so the 10 Hz ticker never repeats a line. */
	bool bLoggedWaitingOnLegalGate = false;
	bool bLoggedWaitingOnListeners = false;
};
