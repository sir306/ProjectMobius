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

/**
 * Headless visual capture of the building render modes, for the translucent depth/sort-order work.
 *
 * WHY THIS EXISTS AT ALL
 * ---------------------
 * Every claim about how the building looks has, historically, ended in "please look at the screen",
 * and three separate in-editor capture routes were each proved to be an INVALID oracle for this app:
 *   - the editor level viewport renders the EDITOR world, whose ProceduralMeshComponent has zero
 *     sections, because an imported building only exists in the PIE/game world;
 *   - HighResShot re-renders, but from the editor viewport camera, so moving the game pawn does
 *     nothing;
 *   - some MCP screenshot tools return stale frames - three identical images across two camera moves.
 * A headless `-game` launch has none of those problems, so that is what this drives.
 *
 * WHY A CONSOLE COMMAND AND NOT JUST -ExecCmds
 * -------------------------------------------
 * -ExecCmds fires at startup while the building load is asynchronous (StaggeredEmit chunks section
 * creation across ticks), so a screenshot issued straight from -ExecCmds photographs an empty world.
 * Nothing in the console can wait, so the waiting lives here.
 *
 * WHY MULTI-ANGLE IS ONE LAUNCH, NOT N
 * -----------------------------------
 * The question this tool was rebuilt to answer is whether the artifact is DEPTH/SORT ORDER. A sorting
 * artifact moves with the camera; a geometry or material artifact does not. That test needs several
 * angles of the SAME loaded building - and re-launching per angle would reload the file each time,
 * which is both slow and a second uncontrolled variable. So one launch orbits and shoots N times.
 *
 * Commands:
 *   Mobius.Render.Mode <0-3>                      apply a building material style now
 *   Mobius.Render.Opacity <0..1>                  set MPC_BuildingSettings.OpacityAmount, with readback
 *   Mobius.Render.State                           log style, section count and material parents
 *   Mobius.Render.Capture <mode> <name> [opacity] [angles] [pitch] [distanceMul] [quit]
 *
 * Capture writes <project>/Saved/MobiusCaptures/<name>.png for a single angle, or
 * <name>_yaw<NNN>.png per angle when angles > 1, then exits unless quit=0.
 *
 * NOT compiled out of Shipping, for the same reason the Mobius.Load.* family is not: this is an
 * integration surface driven by -ExecCmds, and FAutoConsoleCommand still registers in Shipping.
 */

#include "BuildingGenerator/RuntimeMeshBuilder.h"

#include "Camera/CameraActor.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Subsystems/TimeDilationSubSystem.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobiusRenderCapture, Log, All);

namespace
{
	/** Where OpacityAmount actually lives. It is a COLLECTION parameter, not a material parameter. */
	static const TCHAR* MobiusBuildingSettingsCollection = TEXT("/Game/01_Dev/Widgets/Test/MPC_BuildingSettings");

	static UWorld* ResolveGameWorld(UWorld* World)
	{
		if (World && World->IsGameWorld())
		{
			return World;
		}

		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					return Context.World();
				}
			}
		}
		return nullptr;
	}

	static ARuntimeMeshBuilder* FindBuilder(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<ARuntimeMeshBuilder> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static UProceduralMeshComponent* FindBuildingMesh(ARuntimeMeshBuilder* Builder)
	{
		if (!Builder)
		{
			return nullptr;
		}
		if (Builder->MobiusProceduralMeshComponent)
		{
			return Builder->MobiusProceduralMeshComponent;
		}
		// Fall back to a component search so this keeps working if the member is ever renamed.
		return Builder->FindComponentByClass<UProceduralMeshComponent>();
	}

	/**
	 * Set OpacityAmount and read it straight back.
	 *
	 * A per-section UMaterialInstanceDynamic::SetScalarParameterValue("OpacityAmount", x) is a SILENT
	 * NO-OP - a MID cannot override a collection parameter, and MID setters never fail loudly. That
	 * mistake produced a pixel-identical "verification" image once already, so this routes through the
	 * collection and asserts the readback rather than trusting the write.
	 */
	static bool SetBuildingOpacity(UWorld* World, float Value, float& OutReadback)
	{
		OutReadback = -1.0f;
		if (!World)
		{
			return false;
		}

		UMaterialParameterCollection* Collection =
			LoadObject<UMaterialParameterCollection>(nullptr, MobiusBuildingSettingsCollection);
		if (!Collection)
		{
			UE_LOG(LogMobiusRenderCapture, Error, TEXT("MPC not found: %s"), MobiusBuildingSettingsCollection);
			return false;
		}

		UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("OpacityAmount"), Value);
		OutReadback = UKismetMaterialLibrary::GetScalarParameterValue(World, Collection, TEXT("OpacityAmount"));
		return FMath::IsNearlyEqual(OutReadback, Value, 1.e-3f);
	}

	/**
	 * Scrub the simulation to a chosen time and hold it there.
	 *
	 * Needed because B-RISK smoke grows monotonically with time: at t = 0 there is nothing to see, so a
	 * capture that does not scrub cannot demonstrate the smoke-visibility requirement at all. In the
	 * 12-room fixture the upper-layer optical density peaks at t = 600 s (ULOD_1 = 12.47 1/m) and is
	 * already thick by t = 400 s.
	 *
	 * OverrideCurrentTime pauses regardless of the flag it is passed, and passing 1 ("previously
	 * paused") is what keeps it paused - so the frame that gets photographed is the frame that was
	 * asked for, not one the clock has moved past.
	 */
	static bool SetSimulationTime(UWorld* World, float Seconds)
	{
		if (!World)
		{
			return false;
		}

		UTimeDilationSubSystem* TimeSubsystem = World->GetSubsystem<UTimeDilationSubSystem>();
		if (!TimeSubsystem)
		{
			UE_LOG(LogMobiusRenderCapture, Warning, TEXT("No UTimeDilationSubSystem in this world."));
			return false;
		}

		TimeSubsystem->OverrideCurrentTime(Seconds, 1);
		return true;
	}

	static void LogBuildingState(UWorld* World)
	{
		ARuntimeMeshBuilder* Builder = FindBuilder(World);
		if (!Builder)
		{
			UE_LOG(LogMobiusRenderCapture, Warning, TEXT("Mobius.Render.State: no ARuntimeMeshBuilder in the world."));
			return;
		}

		UProceduralMeshComponent* Mesh = FindBuildingMesh(Builder);
		const int32 Sections = Mesh ? Mesh->GetNumSections() : -1;

		// Count distinct material parents across sections. The parent is what the render style picks,
		// and reading it per section is the only way to see a style that applied to some sections only.
		TMap<FString, int32> ParentCounts;
		if (Mesh)
		{
			for (int32 i = 0; i < Sections; ++i)
			{
				UMaterialInterface* Mat = Mesh->GetMaterial(i);
				FString ParentName = TEXT("(none)");
				if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Mat))
				{
					ParentName = Mid->Parent ? Mid->Parent->GetName() : TEXT("(mid, no parent)");
				}
				else if (Mat)
				{
					ParentName = Mat->GetName();
				}
				ParentCounts.FindOrAdd(ParentName)++;
			}
		}

		FString Parents;
		for (const TPair<FString, int32>& Pair : ParentCounts)
		{
			Parents += FString::Printf(TEXT("%s x%d  "), *Pair.Key, Pair.Value);
		}

		float Opacity = -1.0f;
		if (UMaterialParameterCollection* Collection =
				LoadObject<UMaterialParameterCollection>(nullptr, MobiusBuildingSettingsCollection))
		{
			Opacity = UKismetMaterialLibrary::GetScalarParameterValue(World, Collection, TEXT("OpacityAmount"));
		}

		const FBoxSphereBounds Bounds = Mesh ? Mesh->Bounds : FBoxSphereBounds(ForceInit);
		UE_LOG(LogMobiusRenderCapture, Display,
			TEXT("Mobius.Render.State: style=%d sections=%d opacity=%.3f authoredColours=%s bounds origin=(%s) radius=%.1f | %s"),
			static_cast<int32>(Builder->GetBuildingMaterialStyle()),
			Sections,
			Opacity,
			Builder->DoesBuildingHaveAuthoredColours() ? TEXT("yes") : TEXT("no"),
			*Bounds.Origin.ToString(),
			Bounds.SphereRadius,
			*Parents);
	}

	/**
	 * One capture run, ticked once per frame.
	 *
	 * The mesh-ready gate is section-count STABILITY rather than a log line or a fixed sleep: the emit
	 * pump adds sections across ticks, and an earlier attempt read the scene mid-pump (149 of 208
	 * sections) and would have photographed a half-built building. Stability is the cheap, format
	 * agnostic version of "the load finished".
	 */
	struct FMobiusCaptureJob
	{
		int32 Mode = 3;
		FString BaseName = TEXT("capture");
		float Opacity = -1.0f;			// < 0 means "leave the collection alone"
		float SimTime = -1.0f;			// < 0 means "do not scrub"
		int32 AngleCount = 1;
		float PitchDeg = 20.0f;			// degrees BELOW horizontal, i.e. looking slightly down
		float DistanceMul = 2.2f;
		bool bQuitWhenDone = true;

		enum class EPhase : uint8 { WaitForMesh, PlaceCamera, Settle, Shoot, WaitForFile, Done };
		EPhase Phase = EPhase::WaitForMesh;

		int32 AngleIndex = 0;
		int32 LastSectionCount = -1;
		int32 StableFrames = 0;
		int32 SettleFrames = 0;
		FString PendingFile;
		int32 FileWaitFrames = 0;
		double StartSeconds = 0.0;

		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ACameraActor> Camera;

		FTSTicker::FDelegateHandle TickerHandle;
	};

	static TSharedPtr<FMobiusCaptureJob> GActiveCaptureJob;

	/** Sections must hold this many consecutive frames before the building counts as fully emitted. */
	static constexpr int32 GMeshStableFramesRequired = 45;

	/** Frames between placing the camera and taking the shot, so streaming/exposure settle. */
	static constexpr int32 GSettleFramesRequired = 12;

	/** Hard ceiling so a failed load exits instead of hanging a headless run forever. */
	static constexpr double GCaptureTimeoutSeconds = 300.0;

	static FString CaptureDirectory()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusCaptures"));
	}

	static void FinishCapture(FMobiusCaptureJob& Job, const TCHAR* Reason)
	{
		UE_LOG(LogMobiusRenderCapture, Display, TEXT("Mobius.Render.Capture finished: %s"), Reason);
		Job.Phase = FMobiusCaptureJob::EPhase::Done;

		if (Job.bQuitWhenDone)
		{
			FPlatformMisc::RequestExit(false);
		}
	}

	static bool TickCapture(float /*DeltaTime*/)
	{
		TSharedPtr<FMobiusCaptureJob> JobPtr = GActiveCaptureJob;
		if (!JobPtr.IsValid())
		{
			return false;	// unregister
		}
		FMobiusCaptureJob& Job = *JobPtr;

		UWorld* World = Job.World.Get();
		if (!World)
		{
			World = ResolveGameWorld(nullptr);
			Job.World = World;
		}
		if (!World)
		{
			return true;
		}

		if (FPlatformTime::Seconds() - Job.StartSeconds > GCaptureTimeoutSeconds)
		{
			UE_LOG(LogMobiusRenderCapture, Error,
				TEXT("Mobius.Render.Capture TIMED OUT after %.0f s in phase %d - no capture written."),
				GCaptureTimeoutSeconds, static_cast<int32>(Job.Phase));
			FinishCapture(Job, TEXT("timeout"));
			GActiveCaptureJob.Reset();
			return false;
		}

		ARuntimeMeshBuilder* Builder = FindBuilder(World);
		UProceduralMeshComponent* Mesh = FindBuildingMesh(Builder);

		switch (Job.Phase)
		{
		case FMobiusCaptureJob::EPhase::WaitForMesh:
		{
			const int32 Sections = Mesh ? Mesh->GetNumSections() : 0;
			if (Sections <= 0)
			{
				Job.StableFrames = 0;
				Job.LastSectionCount = 0;
				return true;
			}

			if (Sections == Job.LastSectionCount)
			{
				++Job.StableFrames;
			}
			else
			{
				Job.LastSectionCount = Sections;
				Job.StableFrames = 0;
			}

			if (Job.StableFrames < GMeshStableFramesRequired)
			{
				return true;
			}

			UE_LOG(LogMobiusRenderCapture, Display,
				TEXT("Mobius.Render.Capture: mesh settled at %d sections after %.1f s."),
				Sections, FPlatformTime::Seconds() - Job.StartSeconds);

			// Style first, then opacity - the style rebuilds the per-section MIDs, and doing it the
			// other way round would leave the readback describing materials that no longer exist.
			if (Builder)
			{
				Builder->SetBuildingMaterialStyle(static_cast<EMobiusBuildingMaterialStyle>(Job.Mode));
			}

			if (Job.Opacity >= 0.0f)
			{
				float Readback = -1.0f;
				const bool bApplied = SetBuildingOpacity(World, Job.Opacity, Readback);
				UE_LOG(LogMobiusRenderCapture, Display,
					TEXT("Mobius.Render.Capture: OpacityAmount requested %.3f, read back %.3f (%s)"),
					Job.Opacity, Readback, bApplied ? TEXT("applied") : TEXT("MISMATCH"));
			}

			if (Job.SimTime >= 0.0f)
			{
				const bool bScrubbed = SetSimulationTime(World, Job.SimTime);
				UE_LOG(LogMobiusRenderCapture, Display,
					TEXT("Mobius.Render.Capture: simulation time scrubbed to %.1f s (%s)"),
					Job.SimTime, bScrubbed ? TEXT("ok") : TEXT("FAILED"));
			}

			LogBuildingState(World);
			Job.Phase = FMobiusCaptureJob::EPhase::PlaceCamera;
			return true;
		}

		case FMobiusCaptureJob::EPhase::PlaceCamera:
		{
			if (!Mesh)
			{
				return true;
			}

			const FBoxSphereBounds Bounds = Mesh->Bounds;
			const float Yaw = (Job.AngleCount > 1)
				? (360.0f / static_cast<float>(Job.AngleCount)) * static_cast<float>(Job.AngleIndex)
				: 0.0f;

			// Look direction, then step BACK along it from the building's own centre. Framing from the
			// mesh bounds rather than a hardcoded transform is what makes this work for any file.
			const FRotator LookRotation(-Job.PitchDeg, Yaw, 0.0f);
			const FVector LookDirection = LookRotation.Vector();
			const float Distance = FMath::Max(Bounds.SphereRadius, 1.0f) * Job.DistanceMul;
			const FVector CameraLocation = Bounds.Origin - LookDirection * Distance;

			ACameraActor* Camera = Job.Camera.Get();
			if (!Camera)
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Camera = World->SpawnActor<ACameraActor>(CameraLocation, LookRotation, Params);
				Job.Camera = Camera;
			}
			if (!Camera)
			{
				UE_LOG(LogMobiusRenderCapture, Error, TEXT("Mobius.Render.Capture: could not spawn a camera."));
				FinishCapture(Job, TEXT("no camera"));
				GActiveCaptureJob.Reset();
				return false;
			}

			Camera->SetActorLocationAndRotation(CameraLocation, LookRotation);

			// A dedicated camera actor, not the pawn: the pawn is subject to collision and gravity, so
			// teleporting it to an arbitrary orbit point is not reliably where it ends up.
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				PC->SetViewTarget(Camera);
			}

			Job.SettleFrames = 0;
			Job.Phase = FMobiusCaptureJob::EPhase::Settle;
			return true;
		}

		case FMobiusCaptureJob::EPhase::Settle:
		{
			if (++Job.SettleFrames < GSettleFramesRequired)
			{
				return true;
			}
			Job.Phase = FMobiusCaptureJob::EPhase::Shoot;
			return true;
		}

		case FMobiusCaptureJob::EPhase::Shoot:
		{
			const float Yaw = (Job.AngleCount > 1)
				? (360.0f / static_cast<float>(Job.AngleCount)) * static_cast<float>(Job.AngleIndex)
				: 0.0f;

			const FString FileName = (Job.AngleCount > 1)
				? FString::Printf(TEXT("%s_yaw%03d.png"), *Job.BaseName, FMath::RoundToInt(Yaw))
				: FString::Printf(TEXT("%s.png"), *Job.BaseName);

			Job.PendingFile = FPaths::ConvertRelativePathToFull(FPaths::Combine(CaptureDirectory(), FileName));
			IFileManager::Get().MakeDirectory(*CaptureDirectory(), true);
			IFileManager::Get().Delete(*Job.PendingFile, false, true, true);

			// bAddFilenameSuffix = false so the exact path asked for is the path written; a suffix would
			// make the file impossible to find from the calling script.
			FScreenshotRequest::RequestScreenshot(Job.PendingFile, false, false);

			UE_LOG(LogMobiusRenderCapture, Display,
				TEXT("Mobius.Render.Capture: shooting angle %d/%d (yaw %.0f) -> %s"),
				Job.AngleIndex + 1, Job.AngleCount, Yaw, *Job.PendingFile);

			Job.FileWaitFrames = 0;
			Job.Phase = FMobiusCaptureJob::EPhase::WaitForFile;
			return true;
		}

		case FMobiusCaptureJob::EPhase::WaitForFile:
		{
			++Job.FileWaitFrames;
			const bool bWritten = IFileManager::Get().FileExists(*Job.PendingFile);
			if (!bWritten && Job.FileWaitFrames < 240)
			{
				return true;
			}

			if (!bWritten)
			{
				UE_LOG(LogMobiusRenderCapture, Error,
					TEXT("Mobius.Render.Capture: screenshot never appeared at %s"), *Job.PendingFile);
			}

			++Job.AngleIndex;
			if (Job.AngleIndex >= Job.AngleCount)
			{
				FinishCapture(Job, TEXT("all angles captured"));
				GActiveCaptureJob.Reset();
				return false;
			}

			Job.Phase = FMobiusCaptureJob::EPhase::PlaceCamera;
			return true;
		}

		default:
			GActiveCaptureJob.Reset();
			return false;
		}
	}

	static float ParseFloatArg(const TArray<FString>& Args, int32 Index, float Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : Default;
	}

	static int32 ParseIntArg(const TArray<FString>& Args, int32 Index, int32 Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atoi(*Args[Index]) : Default;
	}

	static void ExecRenderMode(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = ResolveGameWorld(InWorld);
		ARuntimeMeshBuilder* Builder = FindBuilder(World);
		if (!Builder)
		{
			UE_LOG(LogMobiusRenderCapture, Warning, TEXT("Mobius.Render.Mode: no ARuntimeMeshBuilder in the world."));
			return;
		}

		const int32 Mode = FMath::Clamp(ParseIntArg(Args, 0, 0), 0, 3);
		Builder->SetBuildingMaterialStyle(static_cast<EMobiusBuildingMaterialStyle>(Mode));
		UE_LOG(LogMobiusRenderCapture, Display, TEXT("Mobius.Render.Mode: applied style %d."), Mode);
		LogBuildingState(World);
	}

	static void ExecRenderOpacity(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = ResolveGameWorld(InWorld);
		const float Value = FMath::Clamp(ParseFloatArg(Args, 0, 0.5f), 0.0f, 1.0f);
		float Readback = -1.0f;
		const bool bApplied = SetBuildingOpacity(World, Value, Readback);
		UE_LOG(LogMobiusRenderCapture, Display,
			TEXT("Mobius.Render.Opacity: requested %.3f, read back %.3f (%s)"),
			Value, Readback, bApplied ? TEXT("applied") : TEXT("MISMATCH"));
	}

	static void ExecRenderTime(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = ResolveGameWorld(InWorld);
		const float Seconds = FMath::Max(0.0f, ParseFloatArg(Args, 0, 0.0f));
		const bool bOk = SetSimulationTime(World, Seconds);
		UE_LOG(LogMobiusRenderCapture, Display, TEXT("Mobius.Render.Time: %.1f s (%s)"),
			Seconds, bOk ? TEXT("ok") : TEXT("FAILED"));
	}

	static void ExecRenderState(const TArray<FString>& /*Args*/, UWorld* InWorld)
	{
		LogBuildingState(ResolveGameWorld(InWorld));
	}

	static void ExecRenderCapture(const TArray<FString>& Args, UWorld* InWorld)
	{
		if (GActiveCaptureJob.IsValid())
		{
			UE_LOG(LogMobiusRenderCapture, Warning, TEXT("Mobius.Render.Capture: a capture is already running."));
			return;
		}

		TSharedPtr<FMobiusCaptureJob> Job = MakeShared<FMobiusCaptureJob>();
		Job->Mode = FMath::Clamp(ParseIntArg(Args, 0, 3), 0, 3);
		Job->BaseName = Args.IsValidIndex(1) ? Args[1] : TEXT("capture");
		Job->Opacity = ParseFloatArg(Args, 2, -1.0f);
		Job->AngleCount = FMath::Clamp(ParseIntArg(Args, 3, 1), 1, 36);
		Job->PitchDeg = ParseFloatArg(Args, 4, 20.0f);
		Job->DistanceMul = ParseFloatArg(Args, 5, 2.2f);
		Job->bQuitWhenDone = ParseIntArg(Args, 6, 1) != 0;
		Job->SimTime = ParseFloatArg(Args, 7, -1.0f);
		Job->StartSeconds = FPlatformTime::Seconds();
		Job->World = ResolveGameWorld(InWorld);

		UE_LOG(LogMobiusRenderCapture, Display,
			TEXT("Mobius.Render.Capture: mode=%d name=%s opacity=%.3f angles=%d pitch=%.1f dist=%.2f quit=%d simTime=%.1f"),
			Job->Mode, *Job->BaseName, Job->Opacity, Job->AngleCount, Job->PitchDeg, Job->DistanceMul,
			Job->bQuitWhenDone ? 1 : 0, Job->SimTime);

		GActiveCaptureJob = Job;
		Job->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&TickCapture), 0.0f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GMobiusRenderModeCommand(
	TEXT("Mobius.Render.Mode"),
	TEXT("Apply a building material style: 0 OriginalColours, 1 CutOut, 2 TransparentWhite, 3 OriginalColoursTransparent.\n")
	TEXT("Usage: Mobius.Render.Mode <0-3>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecRenderMode));

static FAutoConsoleCommandWithWorldAndArgs GMobiusRenderOpacityCommand(
	TEXT("Mobius.Render.Opacity"),
	TEXT("Set MPC_BuildingSettings.OpacityAmount and log the readback. A per-section MID write cannot do this.\n")
	TEXT("Usage: Mobius.Render.Opacity <0..1>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecRenderOpacity));

static FAutoConsoleCommandWithWorldAndArgs GMobiusRenderStateCommand(
	TEXT("Mobius.Render.State"),
	TEXT("Log the building's style, section count, opacity and per-section material parents."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecRenderState));

static FAutoConsoleCommandWithWorldAndArgs GMobiusRenderCaptureCommand(
	TEXT("Mobius.Render.Capture"),
	TEXT("Wait for the async building emit, apply a style, orbit the building and screenshot each angle.\n")
	TEXT("Usage: Mobius.Render.Capture <mode 0-3> <name> [opacity 0..1, -1 = leave] [angles] [pitch deg] [distanceMul] [quit 0|1] [simTime s, -1 = leave]\n")
	TEXT("Writes Saved/MobiusCaptures/<name>.png, or <name>_yaw<NNN>.png when angles > 1."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecRenderCapture));

static FAutoConsoleCommandWithWorldAndArgs GMobiusRenderTimeCommand(
	TEXT("Mobius.Render.Time"),
	TEXT("Scrub the simulation clock to a time in seconds and hold it paused there.\n")
	TEXT("B-RISK smoke grows with time - at t=0 there is nothing to photograph.\n")
	TEXT("Usage: Mobius.Render.Time <seconds>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecRenderTime));
