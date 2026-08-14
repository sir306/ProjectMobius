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
 * Measures what order-independent transparency actually costs on this machine, for this building.
 *
 * WHY A DEDICATED COMMAND
 * -----------------------
 * The visual verdict on OIT was settled by capture (it fixes the sort order and costs interior
 * legibility at 0.5 opacity). The COST side was never measured - only estimated from the allocation
 * code - and an estimate is not a number you can plan against. `stat unit` shows the numbers but only
 * to a human staring at a window, which is the oracle this whole thread has been trying to get away
 * from. So the sampling lives here and the answer comes out as a CSV.
 *
 * WHY BOTH ARMS RUN IN ONE LAUNCH, INTERLEAVED
 * --------------------------------------------
 * `r.OIT.SortedPixels` is a project setting (ECVF_ReadOnly) and costs a shader recompile, but
 * `r.OIT.SortedPixels.Enable` is a runtime permutation toggle. So ONE build can measure both arms,
 * which removes the compile, the driver state and the thermal history as variables. The arms are
 * interleaved PER ANGLE, and the order is flipped on odd angles, so a GPU that heats up or clocks
 * down during the run biases both arms equally instead of whichever ran second.
 *
 * WHY IT ORBITS
 * -------------
 * Translucent cost scales with overdraw, and overdraw depends entirely on where you stand: a corner
 * view through two glazed facades costs far more than a gable end. A single viewpoint would produce a
 * number that is true and useless. Sampling N angles and reporting the spread says what the cost is
 * AND how much it depends on the shot.
 *
 * THE GUARD THAT MATTERS
 * ----------------------
 * If `r.OIT.SortedPixels` is 0 the OIT permutation was never compiled, `Enable` silently does
 * nothing, and both arms measure the same renderer - a clean-looking table showing "OIT is free".
 * That is the single most likely way this tool lies, so it refuses to run in that state.
 *
 * Commands:
 *   Mobius.Render.Bench <mode 0-3> <label> [samplesPerArm] [angles] [pitch] [distanceMul] [opacity] [quit]
 *
 * Writes <project>/Saved/MobiusCaptures/bench_<label>.csv (one row per sampled frame) plus a summary
 * block in the log, and two confirmation screenshots so the arms can be checked visually afterwards.
 */

#include "BuildingGenerator/RuntimeMeshBuilder.h"

#include "Camera/CameraActor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "DatasmithRuntime.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "RHIGlobals.h"
#include "RenderTimer.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobiusRenderBench, Log, All);

/**
 * A NAMED namespace, not an anonymous one.
 *
 * MobiusCore builds with unity/blob compilation, so this file is concatenated into the same
 * translation unit as MobiusRenderCaptureCommands.cpp - and two anonymous namespaces in one TU merge
 * rather than isolate. That is a redefinition of every shared helper name (FindBuilder, the MPC path)
 * and it fails the build, which is exactly how this was found.
 */
namespace MobiusRenderBench
{
	static const TCHAR* MobiusBuildingSettingsCollection = TEXT("/Game/01_Dev/Widgets/Test/MPC_BuildingSettings");

	/** Arm 0 is OIT enabled, arm 1 is OIT disabled. Index into every per-arm array below. */
	static constexpr int32 GArmCount = 2;
	static const TCHAR* GArmNames[GArmCount] = { TEXT("OIT_ON"), TEXT("OIT_OFF") };

	/** Frames discarded after a camera move or an arm switch, before any sample is kept. */
	static constexpr int32 GBenchSettleFrames = 45;

	/** Consecutive stable frames before a load counts as finished. */
	static constexpr int32 GLoadStableFrames = 45;

	/**
	 * Hard ceiling so a failed load exits instead of hanging a headless run forever.
	 *
	 * Generous because enabling r.OIT.SortedPixels changes the shader permutation set, so the FIRST
	 * launch after that ini edit recompiles material shaders on demand while this ticker is already
	 * running. A tighter ceiling would abort a healthy run mid-compile and report it as a timeout.
	 */
	static constexpr double GBenchTimeoutSeconds = 5400.0;

	static UWorld* ResolveGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				return Context.World();
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

	/**
	 * How much geometry currently exists.
	 *
	 * Deliberately format agnostic. A procedural building reports section count; a Datasmith building
	 * reports nothing at all through that route, because it spawns components instead of sections and
	 * the procedural mesh stays empty. Summing both is what lets ONE readiness gate serve the .ifc and
	 * the .udatasmith test without the caller having to say which it launched.
	 */
	static int32 CountLoadedGeometry(UWorld* World, ARuntimeMeshBuilder* Builder, int32& OutSections, int32& OutPrimitives)
	{
		OutSections = 0;
		OutPrimitives = 0;

		if (Builder)
		{
			if (UProceduralMeshComponent* Mesh = Builder->MobiusProceduralMeshComponent)
			{
				OutSections = Mesh->GetNumSections();
			}
		}

		if (World)
		{
			for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
			{
				if (It->GetWorld() == World && It->IsRegistered())
				{
					++OutPrimitives;
				}
			}
		}

		return OutSections + OutPrimitives;
	}

	/**
	 * Bounds to frame the camera from, whichever import route produced the building.
	 *
	 * Datasmith geometry hangs off the runtime anchor actor, so its bounds come from the anchor's own
	 * component tree; the procedural mesh carries its bounds directly. Framing from the geometry rather
	 * than a hardcoded transform is what makes one command work for a 394 KB .ifc and a 25 MB
	 * .udatasmith without per-model tuning.
	 */
	static bool GetBuildingBounds(UWorld* World, ARuntimeMeshBuilder* Builder, FBoxSphereBounds& OutBounds)
	{
		if (!Builder)
		{
			return false;
		}

		if (Builder->IsDatasmithAsset())
		{
			if (ADatasmithRuntimeActor* Anchor = Builder->RuntimeDatasmithAnchor)
			{
				const FBox Box = Anchor->GetComponentsBoundingBox(true);
				if (Box.IsValid && Box.GetSize().SizeSquared() > 1.0f)
				{
					OutBounds = FBoxSphereBounds(Box);
					return true;
				}
			}

			// Fallback: the anchor is only known to be populated on the Browse path, and a
			// -MobiusGeometry= preload is a different route. Rather than fail a 25 MB load after two
			// minutes, rebuild the bounds from what is actually in the world.
			//
			// The sky sphere is the reason this is not a plain union: it is a registered static mesh
			// several kilometres across and would swallow the building whole, framing the camera on
			// nothing. Discarding anything far larger than the MEDIAN component drops it (and any other
			// backdrop) without needing to know its name.
			TArray<FBoxSphereBounds> Candidates;
			TArray<float> Radii;
			for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
			{
				if (It->GetWorld() != World || !It->IsRegistered())
				{
					continue;
				}
				const FBoxSphereBounds ComponentBounds = It->Bounds;
				if (ComponentBounds.SphereRadius <= KINDA_SMALL_NUMBER)
				{
					continue;
				}
				Candidates.Add(ComponentBounds);
				Radii.Add(ComponentBounds.SphereRadius);
			}

			if (Candidates.Num() == 0)
			{
				return false;
			}

			Radii.Sort();
			const float MedianRadius = Radii[Radii.Num() / 2];
			const float RadiusCeiling = FMath::Max(MedianRadius * 10.0f, 1.0f);

			FBox Union(ForceInit);
			int32 Kept = 0;
			for (const FBoxSphereBounds& ComponentBounds : Candidates)
			{
				if (ComponentBounds.SphereRadius > RadiusCeiling)
				{
					continue;
				}
				Union += ComponentBounds.GetBox();
				++Kept;
			}

			if (!Union.IsValid || Kept == 0)
			{
				return false;
			}

			UE_LOG(LogMobiusRenderBench, Display,
				TEXT("Mobius.Render.Bench: no Datasmith anchor bounds - framed from %d of %d static mesh components (median radius %.0f, ceiling %.0f)."),
				Kept, Candidates.Num(), MedianRadius, RadiusCeiling);

			OutBounds = FBoxSphereBounds(Union);
			return true;
		}

		if (UProceduralMeshComponent* Mesh = Builder->MobiusProceduralMeshComponent)
		{
			if (Mesh->GetNumSections() > 0)
			{
				OutBounds = Mesh->Bounds;
				return true;
			}
		}
		return false;
	}

	static IConsoleVariable* FindCVar(const TCHAR* Name)
	{
		return IConsoleManager::Get().FindConsoleVariable(Name);
	}

	static int32 ReadCVarInt(const TCHAR* Name, int32 Fallback)
	{
		IConsoleVariable* Var = FindCVar(Name);
		return Var ? Var->GetInt() : Fallback;
	}

	/** One sampled frame. Kept raw so the CSV can be re-aggregated without re-running the test. */
	struct FBenchSample
	{
		int32 Arm = 0;
		int32 AngleIndex = 0;
		float Yaw = 0.0f;
		float GpuMs = 0.0f;
		float RenderMs = 0.0f;
		float GameMs = 0.0f;
		float FrameMs = 0.0f;
	};

	static float Mean(const TArray<float>& Values)
	{
		if (Values.Num() == 0)
		{
			return 0.0f;
		}
		double Total = 0.0;
		for (float V : Values)
		{
			Total += V;
		}
		return static_cast<float>(Total / Values.Num());
	}

	/** Percentile on a COPY - the caller's ordering is the frame ordering and must survive. */
	static float Percentile(TArray<float> Values, float Fraction)
	{
		if (Values.Num() == 0)
		{
			return 0.0f;
		}
		Values.Sort();
		const int32 Index = FMath::Clamp(FMath::RoundToInt(Fraction * (Values.Num() - 1)), 0, Values.Num() - 1);
		return Values[Index];
	}

	struct FMobiusBenchJob
	{
		int32 Mode = 3;
		FString Label = TEXT("bench");
		int32 SamplesPerArm = 180;		// per angle, per arm
		int32 AngleCount = 8;
		float PitchDeg = 20.0f;
		float DistanceMul = 2.2f;
		// Pinned rather than "leave the collection alone": overdraw is the thing being measured, and
		// overdraw depends directly on opacity. An unpinned run would measure whatever the collection
		// happened to hold and produce two models' numbers that cannot be compared with each other.
		float Opacity = 0.5f;
		bool bQuitWhenDone = true;

		enum class EPhase : uint8 { WaitForLoad, Configure, PlaceAndArm, Settle, Sample, Shots, Done };
		EPhase Phase = EPhase::WaitForLoad;

		int32 AngleIndex = 0;
		int32 ArmSlot = 0;				// 0 or 1 - position in the visit order, not the arm id
		int32 SettleFrames = 0;
		int32 SampleFrames = 0;

		int32 LastGeometryCount = -1;
		int32 StableFrames = 0;
		bool bSawChurn = false;

		int32 ShotIndex = 0;
		int32 ShotSettle = 0;

		TArray<FBenchSample> Samples;
		FBoxSphereBounds Bounds = FBoxSphereBounds(ForceInit);
		FIntPoint Resolution = FIntPoint(0, 0);
		int32 FinalSections = 0;
		int32 FinalPrimitives = 0;
		bool bGpuTimingSeen = false;

		double StartSeconds = 0.0;

		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ACameraActor> Camera;

		FTSTicker::FDelegateHandle TickerHandle;

		/**
		 * Which arm to run in this slot.
		 *
		 * Flipped on odd angles so that a GPU which heats up (or a driver that clocks down) during the
		 * run does not systematically penalise whichever arm always went second.
		 */
		int32 ArmForSlot(int32 Slot) const
		{
			return (AngleIndex % 2 == 0) ? Slot : (GArmCount - 1 - Slot);
		}

		float YawForAngle() const
		{
			return (AngleCount > 1)
				? (360.0f / static_cast<float>(AngleCount)) * static_cast<float>(AngleIndex)
				: 0.0f;
		}
	};

	static TSharedPtr<FMobiusBenchJob> GActiveBenchJob;

	static FString BenchDirectory()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusCaptures"));
	}

	/**
	 * Re-assert the render style and opacity.
	 *
	 * Called before EVERY arm, not once at Configure, because the Datasmith import is not finished when
	 * the geometry count goes stable. On the Technical School model the count settled at frame 356, the
	 * style was applied, and then at frame 699 the app's own load-completion path called
	 * SetDatasmithToOriginalMatStyle + SetDatasmithMeshToSolidMaterials and put the building back to
	 * OPAQUE - so the first run sampled a solid building and reported OIT as nearly free. Re-asserting
	 * is cheaper than trying to detect the end of someone else's async pipeline.
	 */
	static void ApplyStyleAndOpacity(UWorld* World, ARuntimeMeshBuilder* Builder, int32 Mode, float Opacity)
	{
		if (Builder)
		{
			Builder->SetBuildingMaterialStyle(static_cast<EMobiusBuildingMaterialStyle>(Mode));
		}

		if (Opacity < 0.0f || !World)
		{
			return;
		}

		// TWO different opacity channels, and using the wrong one is silent.
		//
		// The procedural building reads OpacityAmount from MPC_BuildingSettings (a COLLECTION
		// parameter). The Datasmith materials declare their OWN OpacityAmount scalar inside
		// MF_ControlDatasmithMaterial / ...Transparency, default 0.3, which the collection does not
		// touch at all. Writing only the collection left every Datasmith run rendering at its default
		// opacity while the log cheerfully reported the requested value read back.
		if (Builder && Builder->IsDatasmithAsset())
		{
			Builder->UpdateDatasmithMeshOpacity(Opacity);
			return;
		}

		if (UMaterialParameterCollection* Collection =
				LoadObject<UMaterialParameterCollection>(nullptr, MobiusBuildingSettingsCollection))
		{
			UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("OpacityAmount"), Opacity);
		}
	}

	static void SetArm(int32 Arm)
	{
		if (IConsoleVariable* Var = FindCVar(TEXT("r.OIT.SortedPixels.Enable")))
		{
			// SetByConsole rather than SetByCode: the cvar is ECVF_Scalability, and a scalability group
			// applied at startup outranks SetByCode, which would leave the arm silently unchanged.
			Var->Set(Arm == 0 ? 1 : 0, ECVF_SetByConsole);
		}
	}

	/**
	 * Predicted OIT working set, from OIT.cpp's own allocation.
	 *
	 * SampleData is a 2D array of (W*side, H*side) R32_UINT across 3 layers (depth/colour/transmittance),
	 * SampleCount is one R32_UINT per pixel, and side = floor(sqrt(MaxSampleCount)) - which is why a
	 * MaxSampleCount of 8 silently behaves as 4. Reported rather than measured because RHI allocation
	 * stats are not reliably attributable to a single subsystem.
	 */
	static double PredictedOitMegabytes(FIntPoint Resolution, int32 MaxSampleCount)
	{
		const int32 Side = FMath::Max(1, FMath::FloorToInt(FMath::Sqrt(static_cast<float>(FMath::Clamp(MaxSampleCount, 1, 16)))));
		const double Pixels = static_cast<double>(FMath::Max(Resolution.X, 1)) * static_cast<double>(FMath::Max(Resolution.Y, 1));
		const double SampleData = Pixels * Side * Side * 4.0 * 3.0;
		const double SampleCount = Pixels * 4.0;
		return (SampleData + SampleCount) / (1024.0 * 1024.0);
	}

	static void WriteReport(FMobiusBenchJob& Job)
	{
		IFileManager::Get().MakeDirectory(*BenchDirectory(), true);

		const int32 MaxSampleCount = ReadCVarInt(TEXT("r.OIT.SortedPixels.MaxSampleCount"), 4);
		const int32 PassType = ReadCVarInt(TEXT("r.OIT.SortedPixels.PassType"), 3);
		const int32 Method = ReadCVarInt(TEXT("r.OIT.SortedPixels.Method"), 1);

		FString Csv;
		Csv += FString::Printf(TEXT("# label,%s\n"), *Job.Label);
		Csv += FString::Printf(TEXT("# style,%d\n"), Job.Mode);
		Csv += FString::Printf(TEXT("# resolution,%dx%d\n"), Job.Resolution.X, Job.Resolution.Y);
		Csv += FString::Printf(TEXT("# sections,%d\n"), Job.FinalSections);
		Csv += FString::Printf(TEXT("# primitives,%d\n"), Job.FinalPrimitives);
		Csv += FString::Printf(TEXT("# angles,%d\n"), Job.AngleCount);
		Csv += FString::Printf(TEXT("# samplesPerArmPerAngle,%d\n"), Job.SamplesPerArm);
		Csv += FString::Printf(TEXT("# oit.MaxSampleCount,%d\n"), MaxSampleCount);
		Csv += FString::Printf(TEXT("# oit.PassType,%d\n"), PassType);
		Csv += FString::Printf(TEXT("# oit.Method,%d\n"), Method);
		Csv += FString::Printf(TEXT("# oit.PredictedMB,%.1f\n"), PredictedOitMegabytes(Job.Resolution, MaxSampleCount));
		Csv += FString::Printf(TEXT("# gpuTimingAvailable,%s\n"), Job.bGpuTimingSeen ? TEXT("yes") : TEXT("NO"));
		Csv += TEXT("arm,angleIndex,yaw,gpuMs,renderMs,gameMs,frameMs\n");

		for (const FBenchSample& S : Job.Samples)
		{
			Csv += FString::Printf(TEXT("%s,%d,%.0f,%.4f,%.4f,%.4f,%.4f\n"),
				GArmNames[S.Arm], S.AngleIndex, S.Yaw, S.GpuMs, S.RenderMs, S.GameMs, S.FrameMs);
		}

		const FString CsvPath = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(BenchDirectory(), FString::Printf(TEXT("bench_%s.csv"), *Job.Label)));
		FFileHelper::SaveStringToFile(Csv, *CsvPath);

		// Summary to the log as well: the CSV is the artifact, but a headless run that only writes a
		// file gives no signal at all if the file lands somewhere unexpected.
		UE_LOG(LogMobiusRenderBench, Display, TEXT("===== Mobius.Render.Bench '%s' ====="), *Job.Label);
		UE_LOG(LogMobiusRenderBench, Display,
			TEXT("style=%d res=%dx%d sections=%d primitives=%d angles=%d samples/arm/angle=%d"),
			Job.Mode, Job.Resolution.X, Job.Resolution.Y, Job.FinalSections, Job.FinalPrimitives,
			Job.AngleCount, Job.SamplesPerArm);
		UE_LOG(LogMobiusRenderBench, Display,
			TEXT("OIT MaxSampleCount=%d PassType=%d Method=%d predictedWorkingSet=%.1f MB"),
			MaxSampleCount, PassType, Method, PredictedOitMegabytes(Job.Resolution, MaxSampleCount));

		if (!Job.bGpuTimingSeen)
		{
			UE_LOG(LogMobiusRenderBench, Warning,
				TEXT("GPU timing never reported a non-zero value - treat gpuMs as unavailable and read frameMs instead."));
		}

		float ArmGpuMean[GArmCount] = { 0.0f, 0.0f };
		float ArmFrameMean[GArmCount] = { 0.0f, 0.0f };

		for (int32 Arm = 0; Arm < GArmCount; ++Arm)
		{
			TArray<float> Gpu, Render, Game, Frame;
			for (const FBenchSample& S : Job.Samples)
			{
				if (S.Arm == Arm)
				{
					Gpu.Add(S.GpuMs);
					Render.Add(S.RenderMs);
					Game.Add(S.GameMs);
					Frame.Add(S.FrameMs);
				}
			}

			ArmGpuMean[Arm] = Mean(Gpu);
			ArmFrameMean[Arm] = Mean(Frame);

			UE_LOG(LogMobiusRenderBench, Display,
				TEXT("%s  n=%d  gpu mean=%.2f med=%.2f p95=%.2f ms | render mean=%.2f | game mean=%.2f | frame mean=%.2f med=%.2f p95=%.2f ms"),
				GArmNames[Arm], Gpu.Num(),
				Mean(Gpu), Percentile(Gpu, 0.5f), Percentile(Gpu, 0.95f),
				Mean(Render), Mean(Game),
				Mean(Frame), Percentile(Frame, 0.5f), Percentile(Frame, 0.95f));
		}

		const float GpuDelta = ArmGpuMean[0] - ArmGpuMean[1];
		const float FrameDelta = ArmFrameMean[0] - ArmFrameMean[1];
		const float GpuPct = (ArmGpuMean[1] > KINDA_SMALL_NUMBER) ? (GpuDelta / ArmGpuMean[1]) * 100.0f : 0.0f;
		const float FramePct = (ArmFrameMean[1] > KINDA_SMALL_NUMBER) ? (FrameDelta / ArmFrameMean[1]) * 100.0f : 0.0f;

		UE_LOG(LogMobiusRenderBench, Display,
			TEXT("COST OF OIT: gpu %+.2f ms (%+.1f%%)  frame %+.2f ms (%+.1f%%)"),
			GpuDelta, GpuPct, FrameDelta, FramePct);
		UE_LOG(LogMobiusRenderBench, Display, TEXT("CSV -> %s"), *CsvPath);
	}

	static void FinishBench(FMobiusBenchJob& Job, const TCHAR* Reason)
	{
		UE_LOG(LogMobiusRenderBench, Display, TEXT("Mobius.Render.Bench finished: %s"), Reason);
		Job.Phase = FMobiusBenchJob::EPhase::Done;

		if (Job.bQuitWhenDone)
		{
			FPlatformMisc::RequestExit(false);
		}
	}

	static bool TickBench(float DeltaTime)
	{
		TSharedPtr<FMobiusBenchJob> JobPtr = GActiveBenchJob;
		if (!JobPtr.IsValid())
		{
			return false;
		}
		FMobiusBenchJob& Job = *JobPtr;

		UWorld* World = Job.World.Get();
		if (!World)
		{
			World = ResolveGameWorld();
			Job.World = World;
		}
		if (!World)
		{
			return true;
		}

		if (FPlatformTime::Seconds() - Job.StartSeconds > GBenchTimeoutSeconds)
		{
			UE_LOG(LogMobiusRenderBench, Error,
				TEXT("Mobius.Render.Bench TIMED OUT after %.0f s in phase %d."),
				GBenchTimeoutSeconds, static_cast<int32>(Job.Phase));
			if (Job.Samples.Num() > 0)
			{
				WriteReport(Job);	// partial data beats none
			}
			FinishBench(Job, TEXT("timeout"));
			GActiveBenchJob.Reset();
			return false;
		}

		ARuntimeMeshBuilder* Builder = FindBuilder(World);

		switch (Job.Phase)
		{
		case FMobiusBenchJob::EPhase::WaitForLoad:
		{
			int32 Sections = 0;
			int32 Primitives = 0;
			const int32 Count = CountLoadedGeometry(World, Builder, Sections, Primitives);

			if (Job.LastGeometryCount >= 0 && Count != Job.LastGeometryCount)
			{
				Job.bSawChurn = true;
				Job.StableFrames = 0;
			}
			else
			{
				++Job.StableFrames;
			}
			Job.LastGeometryCount = Count;

			// Churn THEN stability. Stability alone would pass on frame two, before the load has even
			// begun - the geometry count is perfectly stable when nothing has happened yet.
			if (!Job.bSawChurn || Job.StableFrames < GLoadStableFrames)
			{
				return true;
			}

			Job.FinalSections = Sections;
			Job.FinalPrimitives = Primitives;
			UE_LOG(LogMobiusRenderBench, Display,
				TEXT("Mobius.Render.Bench: load settled after %.1f s - %d sections, %d primitives, datasmith=%s"),
				FPlatformTime::Seconds() - Job.StartSeconds, Sections, Primitives,
				(Builder && Builder->IsDatasmithAsset()) ? TEXT("yes") : TEXT("no"));

			Job.Phase = FMobiusBenchJob::EPhase::Configure;
			return true;
		}

		case FMobiusBenchJob::EPhase::Configure:
		{
			// Refuse to produce a table that says OIT is free because it was never compiled in.
			const int32 Compiled = ReadCVarInt(TEXT("r.OIT.SortedPixels"), 0);
			if (Compiled == 0)
			{
				UE_LOG(LogMobiusRenderBench, Error,
					TEXT("r.OIT.SortedPixels is 0 - the OIT permutation is not compiled, so r.OIT.SortedPixels.Enable ")
					TEXT("does nothing and both arms would measure the SAME renderer. Add r.OIT.SortedPixels=1 to ")
					TEXT("[/Script/Engine.RendererSettings] in DefaultEngine.ini and rebuild shaders. Refusing to run."));
				FinishBench(Job, TEXT("OIT not compiled"));
				GActiveBenchJob.Reset();
				return false;
			}

			if (Builder)
			{
				Builder->SetBuildingMaterialStyle(static_cast<EMobiusBuildingMaterialStyle>(Job.Mode));
			}

			if (Job.Opacity >= 0.0f)
			{
				if (UMaterialParameterCollection* Collection =
						LoadObject<UMaterialParameterCollection>(nullptr, MobiusBuildingSettingsCollection))
				{
					UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("OpacityAmount"), Job.Opacity);
					const float Readback =
						UKismetMaterialLibrary::GetScalarParameterValue(World, Collection, TEXT("OpacityAmount"));
					UE_LOG(LogMobiusRenderBench, Display,
						TEXT("Mobius.Render.Bench: OpacityAmount requested %.3f, read back %.3f (%s)"),
						Job.Opacity, Readback,
						FMath::IsNearlyEqual(Readback, Job.Opacity, 1.e-3f) ? TEXT("applied") : TEXT("MISMATCH"));
				}
			}

			// A frame-rate cap or smoothing would clamp both arms to the same number and hide the
			// entire effect being measured.
			if (GEngine)
			{
				GEngine->bSmoothFrameRate = false;
				GEngine->bUseFixedFrameRate = false;
			}
			if (IConsoleVariable* MaxFps = FindCVar(TEXT("t.MaxFPS")))
			{
				MaxFps->Set(0, ECVF_SetByConsole);
			}
			if (IConsoleVariable* VSync = FindCVar(TEXT("r.VSync")))
			{
				VSync->Set(0, ECVF_SetByConsole);
			}

			if (!GetBuildingBounds(World, Builder, Job.Bounds))
			{
				UE_LOG(LogMobiusRenderBench, Error, TEXT("Mobius.Render.Bench: no geometry bounds - nothing to frame."));
				FinishBench(Job, TEXT("no bounds"));
				GActiveBenchJob.Reset();
				return false;
			}

			if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
			{
				Job.Resolution = GEngine->GameViewport->Viewport->GetSizeXY();
			}

			UE_LOG(LogMobiusRenderBench, Display,
				TEXT("Mobius.Render.Bench: bounds origin=(%s) radius=%.1f, viewport %dx%d"),
				*Job.Bounds.Origin.ToString(), Job.Bounds.SphereRadius, Job.Resolution.X, Job.Resolution.Y);

			Job.Phase = FMobiusBenchJob::EPhase::PlaceAndArm;
			return true;
		}

		case FMobiusBenchJob::EPhase::PlaceAndArm:
		{
			const float Yaw = Job.YawForAngle();
			const FRotator LookRotation(-Job.PitchDeg, Yaw, 0.0f);
			const FVector LookDirection = LookRotation.Vector();
			const float Distance = FMath::Max(Job.Bounds.SphereRadius, 1.0f) * Job.DistanceMul;
			const FVector CameraLocation = Job.Bounds.Origin - LookDirection * Distance;

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
				UE_LOG(LogMobiusRenderBench, Error, TEXT("Mobius.Render.Bench: could not spawn a camera."));
				FinishBench(Job, TEXT("no camera"));
				GActiveBenchJob.Reset();
				return false;
			}

			Camera->SetActorLocationAndRotation(CameraLocation, LookRotation);
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				PC->SetViewTarget(Camera);
			}

			SetArm(Job.ArmForSlot(Job.ArmSlot));
			ApplyStyleAndOpacity(World, Builder, Job.Mode, Job.Opacity);

			Job.SettleFrames = 0;
			Job.SampleFrames = 0;
			Job.Phase = FMobiusBenchJob::EPhase::Settle;
			return true;
		}

		case FMobiusBenchJob::EPhase::Settle:
		{
			if (++Job.SettleFrames < GBenchSettleFrames)
			{
				return true;
			}
			Job.Phase = FMobiusBenchJob::EPhase::Sample;
			return true;
		}

		case FMobiusBenchJob::EPhase::Sample:
		{
			FBenchSample Sample;
			Sample.Arm = Job.ArmForSlot(Job.ArmSlot);
			Sample.AngleIndex = Job.AngleIndex;
			Sample.Yaw = Job.YawForAngle();
			Sample.GpuMs = FPlatformTime::ToMilliseconds(GGPUFrameTime);
			Sample.RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
			Sample.GameMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
			Sample.FrameMs = DeltaTime * 1000.0f;

			if (Sample.GpuMs > 0.0f)
			{
				Job.bGpuTimingSeen = true;
			}
			Job.Samples.Add(Sample);

			if (++Job.SampleFrames < Job.SamplesPerArm)
			{
				return true;
			}

			// Advance: next arm within this angle, else next angle.
			if (++Job.ArmSlot < GArmCount)
			{
				Job.Phase = FMobiusBenchJob::EPhase::PlaceAndArm;
				return true;
			}

			Job.ArmSlot = 0;
			if (++Job.AngleIndex < Job.AngleCount)
			{
				Job.Phase = FMobiusBenchJob::EPhase::PlaceAndArm;
				return true;
			}

			UE_LOG(LogMobiusRenderBench, Display,
				TEXT("Mobius.Render.Bench: sampling complete (%d samples) after %.1f s."),
				Job.Samples.Num(), FPlatformTime::Seconds() - Job.StartSeconds);

			// Reset to angle 0 for the confirmation shots.
			Job.AngleIndex = 0;
			Job.ShotIndex = 0;
			Job.ShotSettle = 0;
			Job.Phase = FMobiusBenchJob::EPhase::Shots;
			return true;
		}

		case FMobiusBenchJob::EPhase::Shots:
		{
			// Screenshots AFTER all sampling, never between arms: a screenshot request stalls the frame
			// it lands on and would poison whichever arm was being measured.
			if (Job.ShotIndex >= GArmCount)
			{
				WriteReport(Job);
				FinishBench(Job, TEXT("done"));
				GActiveBenchJob.Reset();
				return false;
			}

			if (Job.ShotSettle == 0)
			{
				// Re-place the camera at yaw 0 explicitly. Sampling leaves it wherever the last angle
				// put it, so without this the confirmation shots come from a different viewpoint
				// depending on how many angles were requested - which makes two runs impossible to
				// compare by eye, and comparing by eye is the whole point of these shots.
				if (ACameraActor* ShotCamera = Job.Camera.Get())
				{
					const FRotator LookRotation(-Job.PitchDeg, 0.0f, 0.0f);
					const float Distance = FMath::Max(Job.Bounds.SphereRadius, 1.0f) * Job.DistanceMul;
					ShotCamera->SetActorLocationAndRotation(
						Job.Bounds.Origin - LookRotation.Vector() * Distance, LookRotation);
				}

				SetArm(Job.ShotIndex);
				ApplyStyleAndOpacity(World, Builder, Job.Mode, Job.Opacity);
				UE_LOG(LogMobiusRenderBench, Display,
					TEXT("Mobius.Render.Bench: confirmation shot arm=%s, style reads back as %d"),
					GArmNames[Job.ShotIndex],
					Builder ? static_cast<int32>(Builder->GetBuildingMaterialStyle()) : -1);
			}

			if (++Job.ShotSettle < GBenchSettleFrames)
			{
				return true;
			}

			const FString FileName = FString::Printf(TEXT("bench_%s_%s.png"), *Job.Label, GArmNames[Job.ShotIndex]);
			const FString FullPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(BenchDirectory(), FileName));
			IFileManager::Get().MakeDirectory(*BenchDirectory(), true);
			IFileManager::Get().Delete(*FullPath, false, true, true);
			FScreenshotRequest::RequestScreenshot(FullPath, false, false);

			UE_LOG(LogMobiusRenderBench, Display, TEXT("Mobius.Render.Bench: confirmation shot %s -> %s"),
				GArmNames[Job.ShotIndex], *FullPath);

			++Job.ShotIndex;
			Job.ShotSettle = 0;
			return true;
		}

		default:
			GActiveBenchJob.Reset();
			return false;
		}
	}

	static void StartBench(const TArray<FString>& Args)
	{
		if (GActiveBenchJob.IsValid())
		{
			UE_LOG(LogMobiusRenderBench, Warning, TEXT("Mobius.Render.Bench: a run is already in progress."));
			return;
		}

		TSharedPtr<FMobiusBenchJob> Job = MakeShared<FMobiusBenchJob>();

		if (Args.Num() > 0) { Job->Mode = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 3); }
		if (Args.Num() > 1) { Job->Label = Args[1]; }
		if (Args.Num() > 2) { Job->SamplesPerArm = FMath::Max(1, FCString::Atoi(*Args[2])); }
		if (Args.Num() > 3) { Job->AngleCount = FMath::Clamp(FCString::Atoi(*Args[3]), 1, 64); }
		if (Args.Num() > 4) { Job->PitchDeg = FCString::Atof(*Args[4]); }
		if (Args.Num() > 5) { Job->DistanceMul = FCString::Atof(*Args[5]); }
		if (Args.Num() > 6) { Job->Opacity = FCString::Atof(*Args[6]); }
		if (Args.Num() > 7) { Job->bQuitWhenDone = FCString::Atoi(*Args[7]) != 0; }

		Job->StartSeconds = FPlatformTime::Seconds();
		Job->World = ResolveGameWorld();

		UE_LOG(LogMobiusRenderBench, Display,
			TEXT("Mobius.Render.Bench: style=%d label=%s samples/arm/angle=%d angles=%d pitch=%.0f dist=%.1f opacity=%.2f"),
			Job->Mode, *Job->Label, Job->SamplesPerArm, Job->AngleCount, Job->PitchDeg, Job->DistanceMul, Job->Opacity);

		GActiveBenchJob = Job;
		Job->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&TickBench), 0.0f);
	}

	static FAutoConsoleCommand GBenchCommand(
		TEXT("Mobius.Render.Bench"),
		TEXT("Measure the cost of r.OIT.SortedPixels by sampling both arms in one launch.\n")
		TEXT("Usage: Mobius.Render.Bench <mode 0-3> <label> [samplesPerArm] [angles] [pitch] [distanceMul] [opacity] [quit]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&StartBench));
} // namespace MobiusRenderBench
