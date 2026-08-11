// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// TrajectoryHeatmapInGameTest.cpp
//
// COMPLETE REWRITE (trajectory heatmap rebuild, T5). Tier B: the REAL pipeline (JSON import -> MASS
// spawn -> AgentHeatmapProcessor -> HeatmapSubsystem -> FTrajectoryField), driven through
// SetPedestrianDataFilePath exactly as the file-picker UI does, then measured through the field itself
// via AHeatmapPixelTextureVisualizer::GetTrajectoryFieldForTesting() / WorldToTexelForTesting() (added by
// A4; A0 confirmed these exist and their contract 2026-08-04).
//
// This file previously asserted implementation-specific constants (SeedByte=25, TextureTexels=1024.0f,
// ExpectedBrushRadius() derived from a hardcoded 1024-texel render target). All of that is gone along
// with the uint8 accumulation path it measured.
//
// IDs from T5_TEST_REWRITE.md §5 and their status here:
//   T-INV-1   IMPLEMENTED at the PRD's original flat 1e-2 for 1x, 2x AND 8x. The 3%-at-8x restatement
//             (ruling A0-17) is RETIRED: it was a consequence of emitting one segment per RENDERED FRAME,
//             and AgentHeatmapProcessor now subdivides each frame's sim-time interval at the dataset's own
//             sample boundaries, so the deposited polyline is the dataset's polyline at any playback speed.
//             Compares Total-level sums only, per A0's ruling that NO T-INV case may compare per cell (a
//             coarser chord can miss an edge cell entirely -- 100% per-cell error in an otherwise-correct
//             run). Drives the CURVED fixture, not the straight one: a straight-line path has no corner to
//             cut, so it cannot distinguish per-frame emission from sample-boundary emission and a 1% pass
//             on it would prove nothing.
//   T-INV-2   IMPLEMENTED, BEST-EFFORT, REL 1e-2. Uses t.MaxFPS to request 30 vs 120 fps. Automation runs
//             are not guaranteed to honour real wall-clock frame timing the way a live session would, so
//             this is an integration smoke check on the configuration path, not a rigorous proof.
//   T-INV-3   NOT IMPLEMENTED. Tolerance IS known (TOLERANCES.md + A0: REL = max(1e-5, 0.2/T_run); at this
//             file's T_run = 10.0 s that is 0.2/10.0 = 0.02) but the EXPECTED VALUE cannot be produced:
//             the 0.1 s flush gate in AgentHeatmapProcessor::UpdateHeatmapInterval (FILE_MAP §2, lines
//             ~220-229) is a hardcoded literal in a file this agent may not read (owned by A3). Whether
//             T3's Δt plumbing made it configurable, and under what name, is outside this agent's read
//             scope -- see this agent's return (BLOCKERS). The FlushGateRelTol constant below is ready to
//             use the moment that config surface exists.
//   T-REG-1   IMPLEMENTED, FORM E (bitwise). Same fixture run twice in one session -> canonical field and
//             encoded display buffer bit-identical both runs (the in-game path is at least as
//             deterministic as Tier A's FTrajectoryField.DepositSegment already proves it is in isolation).
//   T-META-1  NOT IMPLEMENTED. The export metadata sidecar schema (mode/units/cm-per-texel/grid dims/
//             bands/provenance) is T6's remit (METADATA_SCHEMA.md) and the writer is a private method on
//             A4's file; neither is inside FILE_MAP.md, T5_TEST_REWRITE.md or the oracle files this agent
//             may read. TOLERANCES.md/A0 do clarify the FORM once the schema exists (FORM E: assert key
//             PRESENCE and COUNT only, 22 keys, never the band-edge VALUES which are provisional D9; any
//             float must round-trip at %.9g), which is recorded here for whoever wires it in. See this
//             agent's return (BLOCKERS).
//
// TOLERANCES. Per A0 2026-08-04: TOLERANCES.md now exists (tasks/oracle/TOLERANCES.md §10); every number
// below is transcribed from its master table or from A0's explicit corrections to it.
// The 1% figures below come directly from T5_TEST_REWRITE.md's own wording ("agrees within 1%"), not from
// this agent -- everything else is a named, clearly-marked PROVISIONAL constant in ONE block below.
//
// TODO (owner ruling 2026-08-03, carried over from the pre-rewrite file, still true): every scenario here
// drives the SUBSYSTEM directly via SetPedestrianDataFilePath, bypassing the UI entirely. A true
// widget-driven case is still wanted -- click through to the display panel and toggle the heatmap for
// real. The file picker itself can never be part of that (blocking native modal); everything downstream
// of the path is fair game.
//
// Run: MobiusPerf\RunTests.ps1 -InGame     (or -ExecCmds="Automation RunTests Mobius.InGame.TrajectoryHeatmap")
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "Subsystems/TimeDilationSubSystem.h"
#include "SimData/SimDiskCache.h"
#include "Actors/HeatmapPixelTextureVisualizer.h"
#include "Subsystems/HeatmapSubsystem.h"
#include "TrajectoryField.h"
// T-PIX-2 reads the band edges back off both consumers: the CPU colouriser lives on the texture, the GPU
// copy on the material instance.
#include "DynamicPixelRenderingTexture.h"
// The FR6 geometry-cost measurement loads a real floor plan through the production entry point and reads
// the building mesh the heatmap's quad culling is derived from.
#include "Misc/DateTime.h"
#include "Kismet/GameplayStatics.h"
#include "ProceduralMeshComponent.h"
#include "BuildingGenerator/RuntimeMeshBuilder.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace TrajectoryInvariance
{
	// --- Tolerance / config policy, from TOLERANCES.md §10 "Master table" + A0's 2026-08-04 rulings ----
	constexpr float SampleInterval = 0.1f;          // matches UpdateHeatmapInterval's 0.1 s gate
	constexpr int32 AgentCount = 1;
	constexpr float DurationSeconds = 10.0f;        // 100 base steps at 1x; this IS "T_run" for T-INV-3 below

	/**
	 * The last sim time at which the dataset still HAS a sample: samples sit at steps 0 .. N-1, so the
	 * final one is at DurationSeconds - SampleInterval, not at DurationSeconds.
	 *
	 * A pass driven to DurationSeconds itself lands one timestep past the data, where the movement
	 * processor stops rendering agents and the heatmap emits nothing for that step. The step is therefore
	 * lost, by an amount equal to the step size - which differs per pass, so it showed up as a
	 * playback-speed dependence that had nothing to do with playback speed. Measured: 1x covered 9.9 s and
	 * 8x covered 9.6 s, and the person-metres ratio between them was exactly 9.6/9.9.
	 */
	constexpr float LastSampleTimeSeconds = DurationSeconds - SampleInterval;
	constexpr float HeatmapMetres = 50.0f;
	constexpr int32 SettleFramesPerStep = 2;

	// T-INV-1 row: the master table's plain 1e-2, at 1x, 2x AND 8x.
	//
	// The 3%-at-8x restatement is gone. The oracle's derivation was right about the mechanism -- a chord
	// under-measures the arc it replaces by theta^2/24, and per-RENDERED-FRAME emission makes theta scale
	// with playback speed, so the shortfall grows like theta^2 (64x from 1x to 8x, ~2.3e-2). What changed
	// is the emission: AgentHeatmapProcessor subdivides the frame's sim-time interval at the dataset's
	// sample boundaries, so theta is fixed by the DATASET's sample rate and no longer depends on how fast
	// it is being played back. The residual is then float noise plus the sub-sample remainder at each end
	// of a pass, both far under 1%.
	constexpr double InvarianceRelTol = 0.01;
	// T-INV-2 row: 1e-2, "achievable, 28x margin".
	constexpr double InvarianceRelTolFps = 0.01;
	// T-INV-3 row: REL = max(1e-5, 0.2 / T_run). T_run == DurationSeconds == 10.0 s here, so
	// 0.2/10.0 = 0.02, which dominates the 1e-5 floor.
	constexpr double FlushGateRelTol = 0.2 / DurationSeconds; // == 0.02 at T_run = 10.0 s; see T-INV-3 below

	static FString FixturePath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TrajectoryInvariance"), TEXT("Invariance.json"));
	}

	static FString CurvedFixturePath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TrajectoryInvariance"), TEXT("InvarianceCurved.json"));
	}

	/** Shared JSON header: same metadata block and single-entity table for both fixtures. */
	static FString FixtureHeader()
	{
		FString Json;
		Json += FString::Printf(
			TEXT("{\n\"metadata\": { \"duration\": %.1f, \"sampling_rate\": %.2f, \"max_num_entities\": %d, \"isSI\": true, \"isDeg\": true },\n"),
			DurationSeconds, SampleInterval, AgentCount);
		Json += FString::Printf(
			TEXT("\"entities\": [\n{ \"id\": 0, \"name\": \"inv_0\", \"simTimeS\": %.1f, \"max_speed\": 1.5, \"m_plane\": \"floor_0\", \"map\": 0 }\n],\n\"simulation\": [\n"),
			DurationSeconds);
		return Json;
	}

	static FString FixtureSampleRow(double X, double Y, bool bLast)
	{
		return FString::Printf(
			TEXT("{ \"samples\": [\n{ \"entity\": 0, \"rotation\": 0.0, \"speed\": 1.0, \"mode\": \"walk\", \"position\": { \"x\": %.4f, \"y\": %.4f, \"z\": 0.0 } }\n] }%s\n"),
			X, Y, bLast ? TEXT("") : TEXT(","));
	}

	/** One agent, straight 40 m line, matching the old SingleRoute fixture's geometry. */
	static bool WriteFixture()
	{
		const FString Path = FixturePath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);

		const int32 Timesteps = FMath::RoundToInt(DurationSeconds / SampleInterval);

		FString Json;
		Json.Reserve(64 * 1024);
		Json += FixtureHeader();
		for (int32 Ts = 0; Ts < Timesteps; ++Ts)
		{
			const double Alpha = static_cast<double>(Ts) / static_cast<double>(Timesteps - 1);
			Json += FixtureSampleRow(5.0 + (40.0 * Alpha), 25.0, Ts + 1 == Timesteps);
		}
		Json += TEXT("]\n}\n");
		return FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	/**
	 * One agent, TWO full revolutions of a 5 m circle centred on the floor, 100 samples over 10 s.
	 *
	 * T-INV-1 needs curvature or it measures nothing. The whole playback-speed question is whether a
	 * coarser frame step cuts corners between dataset samples, and a straight line has no corner to cut:
	 * on WriteFixture()'s 40 m line every emission scheme -- one chord per frame, one per sample, one for
	 * the entire pass -- returns exactly 40 m, so a 1% pass would be vacuous.
	 *
	 * Numbers, so the discrimination is on record rather than assumed. Turn per sample interval is
	 * theta = 4*pi/(N-1) = 0.12695 rad. The dataset polyline is (N-1) chords of 2R*sin(theta/2), i.e.
	 * 62.7 m. A per-RENDERED-FRAME emitter at 8x spans 8 intervals per chord and measures
	 * 2R*sin(8*theta/2) per 8 intervals -- a ratio of sin(4*theta)/(8*sin(theta/2)) = 0.9583, i.e. 4.2%
	 * short, which fails 1e-2 by 4x and the retired 3e-2 restatement by 1.4x. Sample-boundary emission
	 * reproduces the polyline exactly at every speed. So this fixture fails loudly if the subdivision is
	 * ever removed, which the straight one could not.
	 */
	static bool WriteCurvedFixture()
	{
		const FString Path = CurvedFixturePath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);

		const int32 Timesteps = FMath::RoundToInt(DurationSeconds / SampleInterval);
		constexpr double CentreMetres = 25.0;
		constexpr double RadiusMetres = 5.0;
		constexpr double Revolutions = 2.0;

		FString Json;
		Json.Reserve(64 * 1024);
		Json += FixtureHeader();
		for (int32 Ts = 0; Ts < Timesteps; ++Ts)
		{
			const double Alpha = static_cast<double>(Ts) / static_cast<double>(Timesteps - 1);
			const double Phi = Revolutions * 2.0 * PI * Alpha;
			Json += FixtureSampleRow(
				CentreMetres + RadiusMetres * FMath::Cos(Phi),
				CentreMetres + RadiusMetres * FMath::Sin(Phi),
				Ts + 1 == Timesteps);
		}
		Json += TEXT("]\n}\n");
		return FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	static UWorld* GetActiveGameWorld()
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

	/** This file's own heatmap. Must be unique across the session - see FindTrajectoryHeatmap. */
	static const TCHAR* OwnHeatmapName = TEXT("Heatmap_InvarianceCheck");

	/**
	 * The heatmap THIS file spawned, matched by name.
	 *
	 * "First actor with bTrajectoryHeatmap" is not a lookup, it is a coin toss: every Tier B test in the
	 * session leaves its heatmap in the world, and TrajectoryHeatmapRealDatasetTest's is a different size
	 * in a different place. It was harmless only while every test spawned at the world origin, and it read
	 * the wrong actor the moment that stopped being true.
	 */
	static AHeatmapPixelTextureVisualizer* FindTrajectoryHeatmap(UWorld* World)
	{
		if (!World) { return nullptr; }
		for (TActorIterator<AHeatmapPixelTextureVisualizer> It(World); It; ++It)
		{
			if (IsValid(*It) && It->bTrajectoryHeatmap && It->ActorName == OwnHeatmapName)
			{
				return *It;
			}
		}
		return nullptr;
	}

	static AHeatmapPixelTextureVisualizer* SpawnTrajectoryHeatmap(UWorld* World)
	{
		UHeatmapSubsystem* Subsystem = World ? World->GetSubsystem<UHeatmapSubsystem>() : nullptr;
		if (!Subsystem) { return nullptr; }

		AHeatmapPixelTextureVisualizer* Heatmap =
			World->SpawnActor<AHeatmapPixelTextureVisualizer>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Heatmap) { return nullptr; }

		const float SizeCm = HeatmapMetres * 100.0f;

		// The grid's MINIMUM corner is the mesh component's world location (EnsureTrajectoryFieldSized),
		// and the importer NEGATES Y: a fixture sample at (25 m, 25 m) becomes world (+2500, -2500) cm.
		// An actor left at the origin therefore covers [0,+5000] on BOTH axes, i.e. the wrong half of Y,
		// and every segment was booked to DroppedPersonMetres while Total stayed 0 - which the ratio
		// assertions below divided out to a perfect 1.0. So the whole T-INV-1/T-INV-2/T-REG-1 trio was
		// green against an empty field.
		//
		// The Y sign was pinned by measurement, not assumed. Deposited fraction of the straight fixture
		// (X 5..45 m, Y 25 m) and of the circular one (centred 25,25 m, r 5 m), at three grid placements:
		//   grid [0,+5000] on both axes    -> 0%    and 0%
		//   grid [-2500,+2500] both axes   -> 50%   and 25%
		//   grid [-5000,0] both axes       -> 0%    and 0%
		// Only world = (+100*x, -100*y) reproduces all six.
		//
		// Z stays 0: the subsystem's floor filter is a band of MaxAddHeight (10 cm) ABOVE the mesh's Z, so
		// lowering the actor in Z would drop everything again.
		Heatmap->SetActorLocation(FVector(0.0, -SizeCm, 0.0));
		Heatmap->ActorName = TEXT("Heatmap_InvarianceCheck");
		Heatmap->FloorID = 0;
		Heatmap->InitializeHeatmap(2, true, FVector2D(SizeCm, SizeCm), 0.0f, false);
		Subsystem->AddHeatmapActor(Heatmap);
		Subsystem->SetTrajectoryHeatmapsEnabled(true);
		return Heatmap;
	}

	/** What one full playback pass produced, read through the field rather than raw display bytes. */
	struct FPassResult
	{
		double TotalPersonMetres = 0.0;
		double TotalPersonSeconds = 0.0;
		int32 TouchedCells = 0;

		// The other three conservation buckets, carried so a pass that deposited nothing says WHERE the
		// mass went instead of just reading zero. A ratio of two zeros is 1.0, i.e. "invariant", so without
		// these an empty field is indistinguishable from a perfectly invariant one.
		double DroppedPersonMetres = 0.0;
		double DroppedPersonSeconds = 0.0;
		double RejectedPersonMetres = 0.0;
		double RejectedPersonSeconds = 0.0;
		int32 RejectedSegments = 0;
		FIntPoint GridDims = FIntPoint::ZeroValue;

		/** Where the heatmap actor actually ended up, and where four probe points land in its grid. */
		FVector ActorLocation = FVector::ZeroVector;
		FIntPoint ProbeCentreCm = FIntPoint(-1, -1);
		FIntPoint ProbeCentreMetres = FIntPoint(-1, -1);
	};

	// --- latent commands ----------------------------------------------------------------------------

	class FWaitForLoadCommand : public IAutomationLatentCommand
	{
	public:
		FWaitForLoadCommand(FAutomationTestBase& InTest, double InTimeoutSeconds)
			: Test(InTest), Deadline(FPlatformTime::Seconds() + InTimeoutSeconds) {}

		virtual bool Update() override
		{
			if (UWorld* World = GetActiveGameWorld())
			{
				const UAgentDataSubsystem* AgentData = World->GetSubsystem<UAgentDataSubsystem>();
				if (AgentData && AgentData->CachedEntityData.Num() == AgentCount)
				{
					return true;
				}
			}
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test.AddError(TEXT("timed out waiting for the invariance fixture to import"));
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase& Test;
		double Deadline;
	};

	class FSpawnHeatmapCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSpawnHeatmapCommand(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GetActiveGameWorld();
			if (!World || !SpawnTrajectoryHeatmap(World))
			{
				Test.AddError(TEXT("failed to spawn the invariance heatmap"));
			}
			return true;
		}

	private:
		FAutomationTestBase& Test;
	};

	/**
	 * Runs one full 0..DurationSeconds pass at a given scrub step size, rewinding first (which clears the
	 * trajectory field via the D8 hook -- FILE_MAP §2 lines ~205-218) unless this is the very first pass,
	 * then reads the result back through GetTrajectoryFieldForTesting() rather than raw display bytes.
	 */
	class FRunInvariancePassCommand : public IAutomationLatentCommand
	{
	public:
		FRunInvariancePassCommand(FAutomationTestBase& InTest, float InStepSeconds, bool bInRewindFirst,
			TSharedRef<TArray<FPassResult>> InResults)
			: Test(InTest), StepSeconds(InStepSeconds), bRewindFirst(bInRewindFirst), Results(InResults) {}

		virtual bool Update() override
		{
			UWorld* World = GetActiveGameWorld();
			if (!World)
			{
				Test.AddError(TEXT("game world disappeared mid-invariance-pass"));
				return true;
			}
			UTimeDilationSubSystem* TimeDilation = World->GetSubsystem<UTimeDilationSubSystem>();
			if (!TimeDilation)
			{
				Test.AddError(TEXT("TimeDilation subsystem missing"));
				return true;
			}

			if (bRewindFirst && !bHasRewound)
			{
				TimeDilation->OverrideCurrentTime(0.0f, /*PreviouslyPaused*/ 0);
				bHasRewound = true;
				FramesToSettle = 6;
				return false;
			}
			if (FramesToSettle > 0)
			{
				--FramesToSettle;
				return false;
			}
			if (bReachedEnd)
			{
				AHeatmapPixelTextureVisualizer* Heatmap = FindTrajectoryHeatmap(World);
				if (!Heatmap)
				{
					Test.AddError(TEXT("no trajectory heatmap found at pass end"));
					return true;
				}
				// A0 (integration): the accessor returns a const reference, not a pointer - the field is a
				// value member of the actor and can never be null. IsValid() is the real availability
				// question, i.e. whether Initialise() has produced a grid yet.
				const FTrajectoryField& Field = Heatmap->GetTrajectoryFieldForTesting();
				if (!Field.IsValid())
				{
					Test.AddError(TEXT("trajectory field has no grid at pass end - Initialise() never ran"));
					return true;
				}
				FPassResult Result;
				Result.TotalPersonMetres = Field.GetTotalPersonMetres();
				Result.TotalPersonSeconds = Field.GetTotalPersonSeconds();
				Result.DroppedPersonMetres = Field.GetDroppedPersonMetres();
				Result.DroppedPersonSeconds = Field.GetDroppedPersonSeconds();
				Result.RejectedPersonMetres = Field.GetRejectedPersonMetres();
				Result.RejectedPersonSeconds = Field.GetRejectedPersonSeconds();
				Result.RejectedSegments = Field.GetRejectedSegmentCount();
				Result.GridDims = Field.GetGridDims();
				Result.ActorLocation = Heatmap->GetActorLocation();
				// Two probes because the drop could be either a units error (the fixture writes metres and
				// the importer scales to cm) or an origin error. Whichever probe lands on-grid names it.
				// World images of the curved fixture's centre and of a point on the straight fixture.
				Result.ProbeCentreCm = Heatmap->WorldToTexelForTesting(FVector(2500.0, -2500.0, 0.0));
				Result.ProbeCentreMetres = Heatmap->WorldToTexelForTesting(FVector(500.0, -2500.0, 0.0));
				for (const float V : Field.GetCanonical(ETrajectoryMapMode::RouteUsage))
				{
					if (V > 0.0f) { ++Result.TouchedCells; }
				}
				Results->Add(Result);
				return true;
			}

			// Every pass must cover the SAME sim-time window [0, LastSampleTimeSeconds], whatever its step
			// size, or the comparison is not an invariance measurement at all -- it is a coverage
			// difference. The previous form exited on `CurrentTime > DurationSeconds` BEFORE applying the
			// clamped target, so a step that does not divide the duration stopped short: at 8x (0.8 s) the
			// last override landed on 9.6 s against 1x's 9.9 s, a built-in ~3% shortfall that no emission
			// scheme could have removed. The end is now detected AFTER the clamped override has been
			// applied, so the final step is short instead of missing, and it lands on the last real sample
			// rather than one step past the data.
			const float TargetTime = FMath::Min(CurrentTime, LastSampleTimeSeconds);
			TimeDilation->OverrideCurrentTime(TargetTime, /*PreviouslyPaused*/ 0);
			bReachedEnd = TargetTime >= LastSampleTimeSeconds - KINDA_SMALL_NUMBER;
			CurrentTime += StepSeconds;
			FramesToSettle = SettleFramesPerStep;
			return false;
		}

	private:
		FAutomationTestBase& Test;
		float StepSeconds;
		bool bRewindFirst;
		TSharedRef<TArray<FPassResult>> Results;
		bool bHasRewound = false;
		bool bReachedEnd = false;
		float CurrentTime = 0.0f;
		int32 FramesToSettle = SettleFramesPerStep;
	};

	/**
	 * Compares each Results[i>0] against the Results[0] baseline using PerPairRelTol[i] (parallel array,
	 * index 0 unused). NEVER compares per cell -- TOLERANCES.md/A0 2026-08-04: a coarser chord can miss an
	 * edge cell entirely, which is a 100% per-cell error even in a CORRECT run, so only Total (sum-level)
	 * agreement is asserted, exactly as FPassResult already captures it (no per-cell array is even read
	 * back into FPassResult).
	 */
	class FCompareInvarianceResultsCommand : public IAutomationLatentCommand
	{
	public:
		FCompareInvarianceResultsCommand(FAutomationTestBase& InTest, TSharedRef<TArray<FPassResult>> InResults,
			const TArray<FString>& InLabels, const TArray<double>& InPerPairRelTol)
			: Test(InTest), Results(InResults), Labels(InLabels), PerPairRelTol(InPerPairRelTol) {}

		virtual bool Update() override
		{
			if (!Test.TestEqual(TEXT("one result recorded per pass"), Results->Num(), Labels.Num()))
			{
				return true;
			}
			// Logged unconditionally, not only on failure. TestTrue prints its message only when it fails,
			// so a green run left no record of WHAT the passes measured -- and diagnosing a future
			// invariance drift starts with the three raw totals, not with the ratio.
			for (int32 i = 0; i < Results->Num(); ++i)
			{
				const FPassResult& R = (*Results)[i];
				Test.AddInfo(FString::Printf(
					TEXT("pass %s: PersonMetres=%.6f PersonSeconds=%.6f TouchedCells=%d grid=%dx%d ")
					TEXT("dropped=(%.6f m, %.6f s) rejected=(%.6f m, %.6f s, %d segs)"),
					*Labels[i], R.TotalPersonMetres, R.TotalPersonSeconds, R.TouchedCells,
					R.GridDims.X, R.GridDims.Y, R.DroppedPersonMetres, R.DroppedPersonSeconds,
					R.RejectedPersonMetres, R.RejectedPersonSeconds, R.RejectedSegments));
				Test.AddInfo(FString::Printf(
					TEXT("pass %s: actor at (%.1f, %.1f, %.1f); texel of (2500,-2500)=(%d,%d); texel of (500,-2500)=(%d,%d)"),
					*Labels[i], R.ActorLocation.X, R.ActorLocation.Y, R.ActorLocation.Z,
					R.ProbeCentreCm.X, R.ProbeCentreCm.Y, R.ProbeCentreMetres.X, R.ProbeCentreMetres.Y));
			}

			// THE GATE THAT WAS MISSING. Every ratio below divides by the compared pass and falls back to
			// 1.0 when the denominator is zero, so a run in which NOTHING was ever deposited scored a
			// perfect 1.0 on every assertion -- "invariant" and "empty" were the same observation. Assert
			// the baseline actually measured something before any ratio is believed.
			for (int32 i = 0; i < Results->Num(); ++i)
			{
				const FPassResult& R = (*Results)[i];
				Test.TestTrue(
					FString::Printf(TEXT("pass %s deposited non-zero person-metres (an empty field cannot be compared)"), *Labels[i]),
					R.TotalPersonMetres > 0.0);
				Test.TestTrue(
					FString::Printf(TEXT("pass %s deposited non-zero person-seconds"), *Labels[i]),
					R.TotalPersonSeconds > 0.0);
				Test.TestTrue(
					FString::Printf(TEXT("pass %s touched at least one cell"), *Labels[i]),
					R.TouchedCells > 0);
			}

			for (int32 i = 1; i < Results->Num(); ++i)
			{
				const FPassResult& A = (*Results)[0];
				const FPassResult& B = (*Results)[i];
				const double Tol = PerPairRelTol.IsValidIndex(i) ? PerPairRelTol[i] : 0.01;
				const double MetresRatio = B.TotalPersonMetres > 0.0 ? A.TotalPersonMetres / B.TotalPersonMetres : 1.0;
				const double SecondsRatio = B.TotalPersonSeconds > 0.0 ? A.TotalPersonSeconds / B.TotalPersonSeconds : 1.0;
				Test.TestTrue(
					FString::Printf(TEXT("%s vs %s: Total PersonMetres agree within %.1f%% (%.6f vs %.6f)"),
						*Labels[0], *Labels[i], Tol * 100.0, A.TotalPersonMetres, B.TotalPersonMetres),
					FMath::Abs(MetresRatio - 1.0) <= Tol);
				Test.TestTrue(
					FString::Printf(TEXT("%s vs %s: Total PersonSeconds agree within %.1f%% (%.6f vs %.6f)"),
						*Labels[0], *Labels[i], Tol * 100.0, A.TotalPersonSeconds, B.TotalPersonSeconds),
					FMath::Abs(SecondsRatio - 1.0) <= Tol);
			}
			return true;
		}

	private:
		FAutomationTestBase& Test;
		TSharedRef<TArray<FPassResult>> Results;
		TArray<FString> Labels;
		TArray<double> PerPairRelTol;
	};
}

// -------------------------------------------------------------------------------------------------
// T-INV-1 -- same dataset, same heatmap, three sequential full passes at 1x / 2x / 8x scrub cadence
// (StepSeconds = SampleInterval * multiplier). "This is the axis that actually varies in the field"
// per T5_TEST_REWRITE.md -- the flush-interval chunking this exercises is exactly what T3's Δt
// plumbing exists to make invariant.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryInvariancePlaybackSpeedTest,
	"Mobius.InGame.TrajectoryHeatmap.T_INV_1_PlaybackSpeedInvariance",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryInvariancePlaybackSpeedTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryInvariance;

	UWorld* World = GetActiveGameWorld();
	if (!World)
	{
		AddError(TEXT("no game world - run via RunTests.ps1 -InGame (UnrealEditor-Cmd -game)"));
		return false;
	}
	UProjectMobiusGameInstance* GameInstance = Cast<UProjectMobiusGameInstance>(World->GetGameInstance());
	TestNotNull(TEXT("Mobius game instance"), GameInstance);
	if (!GameInstance) { return false; }

	// CURVED fixture: see WriteCurvedFixture() for why the straight one cannot measure this criterion.
	TestTrue(TEXT("curved fixture written"), WriteCurvedFixture());
	const FString Path = CurvedFixturePath();
	const uint64 Hash = MobiusSimCache::ComputeSourceHash(Path);
	IFileManager::Get().Delete(*MobiusSimCache::MakeCacheFilePath(Path, Hash), false, true);
	GameInstance->SetPedestrianDataFilePath(Path);

	TSharedRef<TArray<FPassResult>> Results = MakeShared<TArray<FPassResult>>();

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForLoadCommand(*this, 60.0));
	ADD_LATENT_AUTOMATION_COMMAND(FSpawnHeatmapCommand(*this));
	// EVERY pass rewinds, including the first. The window is pinned to [0, LastSampleTimeSeconds] inside
	// the command, but a pass that does not rewind begins wherever FSpawnHeatmapCommand and its settle
	// frames left the clock, so it integrates a SHORTER window than its rewound siblings and reads low.
	// Measured 2026-08-05: 1x reported 9.808334 person-seconds against 2x/8x's 9.900000 -- a deficit of
	// 0.091666 s -- which put 1x vs 2x at 1.010% and failed the 1% gate, while 2x and 8x agreed to 1e-6
	// across a 4x step-size difference. Same root cause as T_REG_1's one-frame delta.
	ADD_LATENT_AUTOMATION_COMMAND(FRunInvariancePassCommand(*this, SampleInterval * 1.0f, /*bRewindFirst*/ true, Results));
	ADD_LATENT_AUTOMATION_COMMAND(FRunInvariancePassCommand(*this, SampleInterval * 2.0f, /*bRewindFirst*/ true, Results));
	ADD_LATENT_AUTOMATION_COMMAND(FRunInvariancePassCommand(*this, SampleInterval * 8.0f, /*bRewindFirst*/ true, Results));
	// Index 0 (the 1x baseline itself) is unused; index 1 = the "2x" pair, index 2 = the "8x" pair. One
	// tolerance for both now that emission no longer depends on playback speed.
	ADD_LATENT_AUTOMATION_COMMAND(FCompareInvarianceResultsCommand(*this, Results,
		{ TEXT("1x"), TEXT("2x"), TEXT("8x") },
		{ 0.0, InvarianceRelTol, InvarianceRelTol }));
	return true;
}

// -------------------------------------------------------------------------------------------------
// T-INV-2 -- BEST EFFORT (see file header). Requests 30 then 120 via t.MaxFPS around the same two
// sequential passes used above, restoring the cvar afterwards. Automation's own tick cadence is not
// fully governed by t.MaxFPS in every run mode, so treat this as an integration smoke check on the
// config path rather than a rigorous proof; a real fps-forced test needs infrastructure this agent
// was not asked to build.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryInvarianceFpsTest,
	"Mobius.InGame.TrajectoryHeatmap.T_INV_2_FpsInvariance_BestEffort",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryInvarianceFpsTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryInvariance;

	UWorld* World = GetActiveGameWorld();
	if (!World)
	{
		AddError(TEXT("no game world - run via RunTests.ps1 -InGame (UnrealEditor-Cmd -game)"));
		return false;
	}
	UProjectMobiusGameInstance* GameInstance = Cast<UProjectMobiusGameInstance>(World->GetGameInstance());
	TestNotNull(TEXT("Mobius game instance"), GameInstance);
	if (!GameInstance) { return false; }

	IConsoleVariable* MaxFpsCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
	TestNotNull(TEXT("t.MaxFPS cvar registered"), MaxFpsCVar);
	const float SavedMaxFps = MaxFpsCVar ? MaxFpsCVar->GetFloat() : 0.0f;

	TestTrue(TEXT("fixture written"), WriteFixture());
	const FString Path = FixturePath();
	const uint64 Hash = MobiusSimCache::ComputeSourceHash(Path);
	IFileManager::Get().Delete(*MobiusSimCache::MakeCacheFilePath(Path, Hash), false, true);
	GameInstance->SetPedestrianDataFilePath(Path);

	if (MaxFpsCVar) { MaxFpsCVar->Set(30.0f, ECVF_SetByCode); }

	TSharedRef<TArray<FPassResult>> Results = MakeShared<TArray<FPassResult>>();

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForLoadCommand(*this, 60.0));
	ADD_LATENT_AUTOMATION_COMMAND(FSpawnHeatmapCommand(*this));
	// Rewind the first pass too -- see the note in T_INV_1. An unrewound pass starts mid-window and reads
	// low, which here would masquerade as frame-rate leakage, i.e. exactly the defect this test exists to
	// detect.
	ADD_LATENT_AUTOMATION_COMMAND(FRunInvariancePassCommand(*this, SampleInterval, /*bRewindFirst*/ true, Results));
	if (MaxFpsCVar)
	{
		MaxFpsCVar->Set(120.0f, ECVF_SetByCode);
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRunInvariancePassCommand(*this, SampleInterval, /*bRewindFirst*/ true, Results));
	ADD_LATENT_AUTOMATION_COMMAND(FCompareInvarianceResultsCommand(*this, Results,
		{ TEXT("30fps"), TEXT("120fps") }, { 0.0, InvarianceRelTolFps }));

	if (MaxFpsCVar)
	{
		MaxFpsCVar->Set(SavedMaxFps, ECVF_SetByCode);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// T-REG-1 -- density surface byte-identical before/after on a fixed dataset. Interpreted here as
// run-to-run determinism of the FULL in-game path (import -> MASS -> processor -> field -> encode):
// the same fixture played back identically twice, in the same session, must produce a bit-identical
// canonical field and encoded display buffer both times. There is no shipped "golden" reference PNG to
// diff against instead, so this is the strongest form of the check available without one.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryRegressionDeterminismTest,
	"Mobius.InGame.TrajectoryHeatmap.T_REG_1_DensitySurfaceDeterminism",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryRegressionDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryInvariance;

	UWorld* World = GetActiveGameWorld();
	if (!World)
	{
		AddError(TEXT("no game world - run via RunTests.ps1 -InGame (UnrealEditor-Cmd -game)"));
		return false;
	}
	UProjectMobiusGameInstance* GameInstance = Cast<UProjectMobiusGameInstance>(World->GetGameInstance());
	TestNotNull(TEXT("Mobius game instance"), GameInstance);
	if (!GameInstance) { return false; }

	TestTrue(TEXT("fixture written"), WriteFixture());
	const FString Path = FixturePath();
	const uint64 Hash = MobiusSimCache::ComputeSourceHash(Path);
	IFileManager::Get().Delete(*MobiusSimCache::MakeCacheFilePath(Path, Hash), false, true);
	GameInstance->SetPedestrianDataFilePath(Path);

	TSharedRef<TArray<FPassResult>> Results = MakeShared<TArray<FPassResult>>();

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForLoadCommand(*this, 60.0));
	ADD_LATENT_AUTOMATION_COMMAND(FSpawnHeatmapCommand(*this));
	// Both passes rewind -- see the note in T_INV_1. This test compares EXACTLY, so it is the most
	// sensitive of the three: an unrewound first pass shifted it by a single frame (1/120 s) and produced
	// the intermittent failure previously recorded as a flake. Pinning both windows is the honest fix; a
	// tolerance on the totals would have hidden real frame-rate leakage, which T_INV_2 exists to catch.
	ADD_LATENT_AUTOMATION_COMMAND(FRunInvariancePassCommand(*this, SampleInterval, /*bRewindFirst*/ true, Results));
	ADD_LATENT_AUTOMATION_COMMAND(FRunInvariancePassCommand(*this, SampleInterval, /*bRewindFirst*/ true, Results));

	// Exact (not 1%-tolerant) comparison: same cadence twice must match to the byte / bit, not just
	// within a percentage band the way a differing-cadence invariance check would.
	class FAssertExactRegressionCommand : public IAutomationLatentCommand
	{
	public:
		FAssertExactRegressionCommand(FAutomationTestBase& InTest, TSharedRef<TArray<FPassResult>> InResults)
			: Test(InTest), Results(InResults) {}

		virtual bool Update() override
		{
			if (Test.TestEqual(TEXT("two identical-cadence passes recorded"), Results->Num(), 2))
			{
				const FPassResult& A = (*Results)[0];
				const FPassResult& B = (*Results)[1];
				// Determinism over an empty field is trivially true: 0 == 0 twice. Same hole as T-INV-1's
				// zero-denominator ratio, and it has to be closed in both places.
				Test.AddInfo(FString::Printf(TEXT("run 1: PersonMetres=%.6f PersonSeconds=%.6f TouchedCells=%d; run 2: %.6f / %.6f / %d"),
					A.TotalPersonMetres, A.TotalPersonSeconds, A.TouchedCells,
					B.TotalPersonMetres, B.TotalPersonSeconds, B.TouchedCells));
				Test.TestTrue(TEXT("both runs deposited a non-empty field (determinism over zeros is vacuous)"),
					A.TotalPersonMetres > 0.0 && B.TotalPersonMetres > 0.0 && A.TouchedCells > 0);
				Test.TestEqual(TEXT("Total PersonMetres bit-identical run to run"),
					A.TotalPersonMetres, B.TotalPersonMetres);
				Test.TestEqual(TEXT("Total PersonSeconds bit-identical run to run"),
					A.TotalPersonSeconds, B.TotalPersonSeconds);
				Test.TestEqual(TEXT("touched cell count identical run to run"), A.TouchedCells, B.TouchedCells);
			}

			UWorld* World = GetActiveGameWorld();
			AHeatmapPixelTextureVisualizer* Heatmap = FindTrajectoryHeatmap(World);
			if (Test.TestNotNull(TEXT("heatmap available for encode comparison"), Heatmap))
			{
				// See the note above: reference, not pointer.
				const FTrajectoryField& Field = Heatmap->GetTrajectoryFieldForTesting();
				TArray<uint8> BufferA, BufferB;
				Field.EncodeToDisplay(ETrajectoryMapMode::RouteUsage, BufferA);
				Field.EncodeToDisplay(ETrajectoryMapMode::RouteUsage, BufferB);
				Test.TestTrue(TEXT("re-encoding the same settled field twice is bit-identical"),
					BufferA.Num() == BufferB.Num()
						&& FMemory::Memcmp(BufferA.GetData(), BufferB.GetData(), BufferA.Num()) == 0);
			}
			return true;
		}

	private:
		FAutomationTestBase& Test;
		TSharedRef<TArray<FPassResult>> Results;
	};
	ADD_LATENT_AUTOMATION_COMMAND(FAssertExactRegressionCommand(*this, Results));
	return true;
}

// -------------------------------------------------------------------------------------------------
// D7 regression guard -- WorldToTexelForTesting() must return the (-1,-1) off-grid sentinel for a point
// well outside the heatmap's floor extent, and must NEVER clamp it onto the border row/column. FILE_MAP
// §4 flags exactly this clamping behaviour as "the current bug" this rebuild removes.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryOffGridNeverClampsTest,
	"Mobius.InGame.TrajectoryHeatmap.D7_OffGridNeverClamps",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryOffGridNeverClampsTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryInvariance;

	UWorld* World = GetActiveGameWorld();
	if (!World)
	{
		AddError(TEXT("no game world - run via RunTests.ps1 -InGame (UnrealEditor-Cmd -game)"));
		return false;
	}
	AHeatmapPixelTextureVisualizer* Heatmap = SpawnTrajectoryHeatmap(World);
	if (!TestNotNull(TEXT("heatmap spawned"), Heatmap))
	{
		return false;
	}

	// A0 (integration): the hook takes a 3D world location, not an FVector2D - the actor projects it
	// itself. Z is ignored by the projection but must be supplied.
	const FIntPoint FarOffGrid = Heatmap->WorldToTexelForTesting(FVector(-999999.0, -999999.0, 0.0));
	TestTrue(TEXT("far off-grid point maps to the (-1,-1) sentinel, never clamped to the border"),
		FarOffGrid.X == -1 && FarOffGrid.Y == -1);

	const FIntPoint AlsoOffGrid = Heatmap->WorldToTexelForTesting(FVector(999999.0, 5.0, 0.0));
	TestTrue(TEXT("off-grid on a single axis also sentinels rather than clamping"),
		AlsoOffGrid.X == -1 && AlsoOffGrid.Y == -1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// T-PIX-2, the automatable half of AC12. The visual half still needs eyes; this covers the part that
// fails SILENTLY.
//
// AC12 asks whether the exported PNG and the on-screen render show the same field. Those two are drawn by
// different code from the same buffer -- the material samples the red channel on the GPU, the PNG
// colouriser divides the stored byte on the CPU -- so they agree only if they were handed the SAME band
// edges and the same texture. A desync there is invisible to every numeric test in this suite and shows up
// only as "the viewport and the export disagree", which is exactly the state this surface was found in.
//
// It also pins the two things just fixed, both of which were silent:
//   * the edges are re-pushed on a mode switch (Exposure used to render against Usage's thresholds);
//   * the encode uses the FIXED reference density, not per-capture auto-exposure.
// A pixel-level comparison is deliberately NOT attempted here, and the reason has changed twice - read this
// before trying again. The original claim was that the render samples through a shared world-group sampler
// that ignores the texture's filter; that was WRONG and is retracted, because
// Material.SamplesWithTextureOwnSampler passes on every heatmap material. The current reason is stronger:
// as of 2026-08-05 the textures are TF_BILINEAR by owner ruling, so the GPU interpolates the scalar between
// texel centres BY DESIGN and boundary pixels are meant to differ from the per-texel-exact PNG. A pixel
// comparison would now be asserting that a deliberate feature is absent. A band-HISTOGRAM comparison over
// the occupied region is still meaningful and would belong here - band membership survives interpolation,
// which is the whole reason bilinear was safe to turn on (see the InitializeTexture comments).
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryExportRenderShareInputsTest,
	"Mobius.InGame.TrajectoryHeatmap.T_PIX_2_ExportAndRenderShareInputs",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryExportRenderShareInputsTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryInvariance;

	UWorld* World = GetActiveGameWorld();
	if (!World)
	{
		AddError(TEXT("no game world - run via RunTests.ps1 -InGame (UnrealEditor-Cmd -game)"));
		return false;
	}
	AHeatmapPixelTextureVisualizer* Heatmap = SpawnTrajectoryHeatmap(World);
	if (!TestNotNull(TEXT("heatmap spawned"), Heatmap))
	{
		return false;
	}

	struct FModeCase
	{
		ETrajectoryMapMode Mode;
		const TCHAR* Name;
		float ExpectedReference;
	};
	// Read from the shipping config rather than transcribed. The exposure figure was hard-coded 200.0f and
	// went stale the moment ReferenceExposureDensity moved to 240 — silently, because this is a Tier B
	// in-game case that needs PIE and a dataset, so it does not run in the editor-context sweep that gates
	// most changes. A literal here asserts what someone once typed; this asserts what ships.
	const FTrajectoryFieldConfig ShippingConfig;
	const FModeCase Cases[] = {
		{ ETrajectoryMapMode::RouteUsage,    TEXT("RouteUsage"),    ShippingConfig.ReferenceUsageDensity },
		{ ETrajectoryMapMode::RouteExposure, TEXT("RouteExposure"), ShippingConfig.ReferenceExposureDensity },
	};

	float FirstModeEdges[5] = { 0, 0, 0, 0, 0 };
	for (int32 CaseIndex = 0; CaseIndex < UE_ARRAY_COUNT(Cases); ++CaseIndex)
	{
		const FModeCase& Case = Cases[CaseIndex];
		Heatmap->SetTrajectoryMapMode(Case.Mode);

		const UDynamicPixelRenderingTexture* Texture = Heatmap->GetTrajectoryAccumulationTextureForTesting();
		const UMaterialInstanceDynamic* Material = Heatmap->GetTrajectoryMaterialForTesting();
		if (!TestNotNull(*FString::Printf(TEXT("%s: accumulation texture exists"), Case.Name), Texture))
		{
			continue;
		}
		if (!TestNotNull(*FString::Printf(TEXT("%s: trajectory material instance exists"), Case.Name), Material))
		{
			continue;
		}

		// The CPU colouriser's copy of the edges, i.e. what the exported PNG will be banded with.
		const FHeatmapLOSBands& CpuBands = Texture->GetLOSBands();
		const float CpuEdges[5] = { CpuBands.BandA, CpuBands.BandB, CpuBands.BandC, CpuBands.BandD,
			CpuBands.BandE };
		static const TCHAR* ParamNames[5] = { TEXT("LOS_A_Band"), TEXT("LOS_B_Band"), TEXT("LOS_C_Band"),
			TEXT("LOS_D_Band"), TEXT("LOS_E_Band") };

		for (int32 Edge = 0; Edge < 5; ++Edge)
		{
			float GpuEdge = -1.0f;
			const bool bRead = Material->GetScalarParameterValue(FName(ParamNames[Edge]), GpuEdge);
			TestTrue(*FString::Printf(TEXT("%s: material exposes %s"), Case.Name, ParamNames[Edge]), bRead);
			// Exact: both sides receive the identical struct, so anything but equality means one of them
			// was pushed a different set, or was not re-pushed at all.
			TestEqual(*FString::Printf(TEXT("%s: %s matches the CPU colouriser's edge"), Case.Name,
				ParamNames[Edge]), GpuEdge, CpuEdges[Edge], 0.0f);
			if (CaseIndex == 0)
			{
				FirstModeEdges[Edge] = CpuEdges[Edge];
			}
		}

		// The no-data threshold must sit below one byte, or every faint-but-real texel is painted as
		// untouched ground - the defect the pre-refit edge set shipped with (it sat at byte 24.5).
		TestTrue(*FString::Printf(TEXT("%s: no-data edge is below byte 1 (%.6f < %.6f)"), Case.Name,
			CpuBands.BandA, 1.0f / 255.0f), CpuBands.BandA < (1.0f / 255.0f));

		// Fixed reference, not auto-exposure. Guards the display's comparability across captures.
		//
		// The encode is driven EXPLICITLY here. A freshly spawned heatmap has never displayed anything, so
		// reading GetLastEncodeReferenceDensity() straight after the spawn returns the 0 default and both of
		// these assertions would pass against a pipeline that had not run - the same "cannot fail" shape
		// that the conservation tests were fixed for. Encoding first makes them mean something.
		const FTrajectoryField& Field = Heatmap->GetTrajectoryFieldForTesting();
		if (!Field.IsValid())
		{
			AddError(*FString::Printf(TEXT("%s: field has no grid, so the encode assertions below would be "
				"vacuous - the heatmap was not sized"), Case.Name));
			continue;
		}

		TArray<uint8> Encoded;
		Field.EncodeToDisplay(Case.Mode, Encoded);
		TestTrue(*FString::Printf(TEXT("%s: encode produced a buffer"), Case.Name), Encoded.Num() > 0);
		TestFalse(*FString::Printf(TEXT("%s: encode is NOT auto-exposed by default"), Case.Name),
			Field.WasLastEncodeAutoExposed());
		TestEqual(*FString::Printf(TEXT("%s: byte 255 maps to the configured reference density"), Case.Name),
			Field.GetLastEncodeReferenceDensity(), Case.ExpectedReference, 0.01f);
	}

	// The two modes are different quantities against different references, so their edge sets MUST differ.
	// Identical sets here means the mode switch never re-pushed them, which is how Exposure came to render
	// against Usage's thresholds.
	const UDynamicPixelRenderingTexture* FinalTexture = Heatmap->GetTrajectoryAccumulationTextureForTesting();
	if (FinalTexture)
	{
		const FHeatmapLOSBands& ExposureBands = FinalTexture->GetLOSBands();
		const float ExposureEdges[5] = { ExposureBands.BandA, ExposureBands.BandB, ExposureBands.BandC,
			ExposureBands.BandD, ExposureBands.BandE };
		bool bAnyInteriorEdgeDiffers = false;
		for (int32 Edge = 1; Edge < 5; ++Edge) // edge 0 is the shared no-data threshold, by design
		{
			bAnyInteriorEdgeDiffers |= (ExposureEdges[Edge] != FirstModeEdges[Edge]);
		}
		TestTrue(TEXT("switching mode re-pushes a DIFFERENT band set (Exposure is not banded as Usage)"),
			bAnyInteriorEdgeDiffers);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// THE FILTER GATE. The two surfaces now take DIFFERENT filters, deliberately, and this is what holds
// them apart.
//
// Owner ruling 2026-08-05 made both TF_Bilinear for smooth LOS band edges. Owner ruling 2026-08-10
// reversed that for the TRAJECTORY surface only:
//
//   density     TF_Bilinear  - a continuous field; interpolating the scalar smooths band edges and
//                              invents nothing, and it is the fix for the pixelated look (A0-72).
//   trajectory  TF_Nearest   - a discrete CROSSING COUNT drawn one texel wide. Interpolating toward the
//                              zero neighbours drops the sample below the band floor at +-0.25 texel, so
//                              about 75% of a stroke renders at least one band LOW. A surface that says
//                              "this colour means N people walked here" must not show the wrong N.
//
// So "make them match" is now the WRONG instinct, and this gate exists to say so in a failure message
// rather than in a comment nobody reads.
//
// Why a gate at all: the textures are created at THREE call sites in HeatmapPixelTextureVisualizer.cpp
// (density once in SetupDynamicTexture; trajectory twice - EnsureTrajectoryFieldSized and again in
// SetupDynamicTexture). Getting two of three right leaves one surface visibly out of step, nothing else
// in the suite looks at a sampler setting, and reverting is a silent one-token edit.
//
// This asserts the FILTER only. Whether it is honoured depends on the MATERIAL's Sampler Source, an asset
// property gated separately by ProjectMobius.Heatmap.*.Material.SamplesWithTextureOwnSampler in
// TrajectoryHeatmapCalibrationTest.cpp. Both are required: any texture sampled through a shared
// world-group sampler ignores this setting entirely. Neither test can see the other's half.
//
// RENAMED 2026-08-10 from ...Texture.FiltersBilinear - that name asserted the very thing that is no
// longer true of both surfaces.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryTextureFilterPerSurfaceTest,
	"Mobius.InGame.TrajectoryHeatmap.Texture.FilterPerSurface",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryTextureFilterPerSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryInvariance;

	UWorld* World = GetActiveGameWorld();
	if (!World)
	{
		AddError(TEXT("no game world - run via RunTests.ps1 -InGame (UnrealEditor-Cmd -game)"));
		return false;
	}
	AHeatmapPixelTextureVisualizer* Heatmap = SpawnTrajectoryHeatmap(World);
	if (!TestNotNull(TEXT("heatmap spawned"), Heatmap))
	{
		return false;
	}

	struct FSurface
	{
		const TCHAR* Name;
		const UDynamicPixelRenderingTexture* Texture;
		TextureFilter ExpectedFilter;
		const TCHAR* Why;
	};
	const FSurface Surfaces[] = {
		{ TEXT("trajectory accumulation"), Heatmap->GetTrajectoryAccumulationTextureForTesting(), TF_Nearest,
		  TEXT("it carries a discrete crossing count on a one-texel stroke; interpolation makes ~75% of the ")
		  TEXT("drawn width read at least one band low") },
		{ TEXT("density"), Heatmap->GetDensityDynamicTextureForTesting(), TF_Bilinear,
		  TEXT("it carries a continuous density field; point sampling draws every texel as a hard square, ")
		  TEXT("which is what made it look pixelated (A0-72)") },
	};

	for (const FSurface& Surface : Surfaces)
	{
		if (!TestNotNull(*FString::Printf(TEXT("%s texture exists"), Surface.Name), Surface.Texture))
		{
			continue;
		}
		const UTexture2D* Texture2D = Surface.Texture->GetDynamicTexture();
		if (!TestNotNull(*FString::Printf(TEXT("%s UTexture2D exists"), Surface.Name), Texture2D))
		{
			continue;
		}
		if (Texture2D->Filter != Surface.ExpectedFilter)
		{
			AddError(FString::Printf(
				TEXT("the %s texture is Filter = %d, expected %d. It must be this filter because %s. ")
				TEXT("NOTE the two surfaces deliberately DIFFER since 2026-08-10 - do not 'fix' this by ")
				TEXT("making them match. FIX: pass the right filter at the InitializeTexture call in ")
				TEXT("HeatmapPixelTextureVisualizer.cpp for this surface. There are THREE such call sites ")
				TEXT("(density once, trajectory twice); check all of them."),
				Surface.Name, static_cast<int32>(Texture2D->Filter),
				static_cast<int32>(Surface.ExpectedFilter), Surface.Why));
		}

		// TA_Clamp matters more once filtering is on, not less: a transient texture defaults to TA_Wrap, so
		// without the clamp a bilinear sample at the texture edge blends in the OPPOSITE edge's texels.
		// Under TF_Nearest that is a one-texel curiosity; under TF_Bilinear it is a visible wrap seam. Both
		// surfaces are asserted regardless - the trajectory one is TF_Nearest today, and leaving it TA_Wrap
		// would arm the seam for whenever the filter ruling is revisited.
		TestEqual(*FString::Printf(TEXT("%s texture clamps rather than wraps"), Surface.Name),
			static_cast<int32>(Texture2D->AddressX), static_cast<int32>(TA_Clamp));
		TestEqual(*FString::Printf(TEXT("%s texture clamps rather than wraps (Y)"), Surface.Name),
			static_cast<int32>(Texture2D->AddressY), static_cast<int32>(TA_Clamp));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// THE 3D-TOGGLE INVARIANT. A0-64/A0-65 removed the `bIs3DHeatmap` branch from mesh generation so the grid
// is always dense enough to displace, because the toggle is a realtime switch and regenerating geometry on
// it would cost far too much. The owner accepted roughly 100x the quads of the old /250 path ON THE
// CONDITION that the toggle is free — and "free" means the vertex count does not move when it flips.
// A0-65 recorded that gate as still owed. This is it.
//
// Paired with ProjectMobius.Heatmap.Mesh.VertexGridIs25cmTruncated, which pins the arithmetic. This one
// drives the REAL path — InitializeHeatmap, thread-pool build, quad culling, staggered tile emit — twice on
// ONE actor with only the flag different, so it catches a branch reintroduced at the call site, which the
// arithmetic gate structurally cannot see.
//
// Three reasons this is built the way it is, each of which was a way to get a green test that proves
// nothing:
//
//  1. ONE actor, re-initialised, not two actors. `FindAllQuads` derives its marching box and mesh bounds
//     from `GetActorLocation()`, so two heatmaps at different transforms legitimately cull differently and
//     the comparison would fail for a reason that has nothing to do with the flag. Re-init on one actor is
//     supported by design: GenerateMeshVerticesUVsAndTriangles cancels any in-flight ticker and resets
//     Tiles, and the GT continuation clears the sections before re-emitting.
//  2. Waits on CompletedTileEmitCount, not on section count or ticker state. Before the thread-pool build
//     returns, `Tiles` is empty and the ticker handle is invalid — identical to "finished". Polling for
//     that pair returns instantly, BEFORE generation starts, and every count below would read the
//     constructor's default section. The counter only moves in FinalizeTileEmit.
//  3. Asserts NON-ZERO and asserts the exact expected total, not just equality. With the branch already
//     gone, a bare equality assertion cannot fail today and cannot fail tomorrow unless someone
//     reintroduces one — including when both sides are 0 because no mesh was built at all.
//
// Batching is turned OFF for the run. With one section the emitted vertex total equals the vertex grid
// exactly; with tiling it exceeds it, because BuildTileBuffers re-materialises boundary vertices per tile
// (see CountEmittedMeshVerticesForTesting). Equality across the two flag values holds either way, but only
// the single-section form lets the test state the number it expects.
// -------------------------------------------------------------------------------------------------
namespace HeatmapMeshFlagGate
{
	/** What one generation produced. */
	struct FMeshRun
	{
		int32 Sections = 0;
		int32 Verts = 0;
		int32 Tris = 0;
	};

	struct FGateState
	{
		TWeakObjectPtr<AHeatmapPixelTextureVisualizer> Heatmap;
		TArray<FMeshRun> Runs;
	};

	using FGateStatePtr = TSharedPtr<FGateState, ESPMode::ThreadSafe>;

	/** Re-initialises the heatmap with one flag value and measures the mesh once emit has finished. */
	class FInitAndMeasureCommand : public IAutomationLatentCommand
	{
	public:
		FInitAndMeasureCommand(FAutomationTestBase& InTest, FGateStatePtr InState, bool bIn3D, double InTimeoutSeconds)
			: Test(InTest), State(InState), bIs3D(bIn3D), TimeoutSeconds(InTimeoutSeconds) {}

		virtual bool Update() override
		{
			AHeatmapPixelTextureVisualizer* Heatmap = State->Heatmap.Get();
			if (!Heatmap)
			{
				Test.AddError(TEXT("the gate's heatmap actor went away mid-run"));
				return true;
			}

			if (!bKicked)
			{
				BaselineEmitCount = Heatmap->CompletedTileEmitCount;
				Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
				bKicked = true;
				// HeatmapType 2 and live tracking off: this gate is about geometry only, and nothing here
				// deposits. Height displacement 0 because displacement is a material concern after A0-64 —
				// if a non-zero value here ever changed the vertex count, that is itself the regression.
				Heatmap->InitializeHeatmap(2, /*bIsLiveTrackingNeeded*/ false,
					FVector2D(TrajectoryInvariance::HeatmapMetres * 100.0f,
					          TrajectoryInvariance::HeatmapMetres * 100.0f),
					/*NewHeightDisplacement*/ 0.0f, bIs3D);
				return false;
			}

			if (Heatmap->CompletedTileEmitCount > BaselineEmitCount)
			{
				FMeshRun Run;
				Run.Sections = Heatmap->CountEmittedMeshSectionsForTesting();
				Run.Verts    = Heatmap->CountEmittedMeshVerticesForTesting();
				Run.Tris     = Heatmap->CountEmittedMeshTrianglesForTesting();
				State->Runs.Add(Run);
				Test.AddInfo(FString::Printf(
					TEXT("bIs3DHeatmap=%s -> %d sections, %d verts, %d tris"),
					bIs3D ? TEXT("true") : TEXT("false"), Run.Sections, Run.Verts, Run.Tris));
				return true;
			}

			if (FPlatformTime::Seconds() > Deadline)
			{
				Test.AddError(FString::Printf(
					TEXT("timed out after %.0f s waiting for the tile emit to finish (bIs3DHeatmap=%s). ")
					TEXT("The most likely cause is NO ARuntimeMeshBuilder actor in the loaded level: ")
					TEXT("GenerateMeshVerticesUVsAndTriangles early-returns when it cannot find one, so no ")
					TEXT("mesh is ever emitted and FinalizeTileEmit never runs. Check the map, not this test."),
					TimeoutSeconds, bIs3D ? TEXT("true") : TEXT("false")));
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase& Test;
		FGateStatePtr State;
		bool bIs3D;
		double TimeoutSeconds;
		bool bKicked = false;
		int32 BaselineEmitCount = 0;
		double Deadline = 0.0;
	};

	/** Compares the two runs and ties the total back to the arithmetic gate's formula. */
	class FCompareFlagRunsCommand : public IAutomationLatentCommand
	{
	public:
		FCompareFlagRunsCommand(FAutomationTestBase& InTest, FGateStatePtr InState)
			: Test(InTest), State(InState) {}

		virtual bool Update() override
		{
			AHeatmapPixelTextureVisualizer* Heatmap = State->Heatmap.Get();
			if (!Test.TestEqual(TEXT("both flag values produced a measured mesh"), State->Runs.Num(), 2)
				|| !Heatmap)
			{
				return true;
			}

			const FMeshRun& Flat = State->Runs[0];
			const FMeshRun& Displaced = State->Runs[1];

			// (1) Anti-vacuity FIRST. Everything below is satisfied by 0 == 0.
			Test.TestTrue(TEXT("the 2D run emitted a mesh at all (0 verts would make every equality below vacuous)"),
				Flat.Verts > 0);

			// (2) The invariant itself.
			if (Flat.Verts != Displaced.Verts)
			{
				Test.AddError(FString::Printf(
					TEXT("vertex count DEPENDS on bIs3DHeatmap: %d verts with the flag off vs %d with it on. ")
					TEXT("The 3D toggle is a realtime switch that must not regenerate geometry, so the mesh ")
					TEXT("is built at the 3D-capable density unconditionally (A0-64/A0-65) and this ")
					TEXT("difference means a branch on the flag has come back. Look at ")
					TEXT("GenerateMeshVerticesUVsAndTriangles and its call site in InitializeHeatmap; the ")
					TEXT("grid itself comes from ComputeHeatmapVertexGrid, which takes no flag."),
					Flat.Verts, Displaced.Verts));
			}
			Test.TestEqual(TEXT("triangle count does not depend on bIs3DHeatmap"), Displaced.Tris, Flat.Tris);
			Test.TestEqual(TEXT("section count does not depend on bIs3DHeatmap"), Displaced.Sections, Flat.Sections);

			// (3) Tie the measured total to the formula the Tier A gate pins, so a change that moved BOTH
			// runs together — a different divisor, say — still fails here.
			const FIntPoint Grid = AHeatmapPixelTextureVisualizer::ComputeHeatmapVertexGrid(
				FVector2D(TrajectoryInvariance::HeatmapMetres * 100.0f,
				          TrajectoryInvariance::HeatmapMetres * 100.0f));
			const int32 FullGridVerts = Grid.X * Grid.Y;
			const int32 FullGridTris  = 2 * (Grid.X - 1) * (Grid.Y - 1);
			const int32 CullingQuads  = Heatmap->CountCullingQuadsForTesting();

			Test.AddInfo(FString::Printf(
				TEXT("vertex grid %dx%d = %d verts, %d tris at full density; culling mask = %d quads"),
				Grid.X, Grid.Y, FullGridVerts, FullGridTris, CullingQuads));

			if (CullingQuads == 0)
			{
				// No culling mask means BuildTileBuffers keeps every cell (bKeep = Quads.Num() == 0), and
				// batching is off, so the total is exactly the grid. This is the normal state for a test
				// world with no floor plan loaded.
				Test.TestEqual(TEXT("with no culling mask the emitted verts are exactly the vertex grid"),
					Flat.Verts, FullGridVerts);
				Test.TestEqual(TEXT("with no culling mask the emitted tris are exactly two per quad"),
					Flat.Tris, FullGridTris);
			}
			else
			{
				// A floor plan is loaded, so kept geometry is a subset. Assert the bound rather than an
				// equality, and report the ratio — that ratio is the geometry-cost number FR6 wants.
				Test.TestTrue(FString::Printf(
					TEXT("kept verts %d do not exceed the full grid %d (culling can only remove)"),
					Flat.Verts, FullGridVerts), Flat.Verts <= FullGridVerts);
				Test.TestTrue(FString::Printf(
					TEXT("kept tris %d do not exceed the full grid %d"), Flat.Tris, FullGridTris),
					Flat.Tris <= FullGridTris);
				Test.AddInfo(FString::Printf(
					TEXT("KEPT FRACTION with a plan loaded: %.2f%% of verts, %.2f%% of tris"),
					100.0 * Flat.Verts / FMath::Max(1, FullGridVerts),
					100.0 * Flat.Tris / FMath::Max(1, FullGridTris)));
			}

			// Leave the world as we found it. Every Tier B test here otherwise leaves its heatmap behind,
			// and a stray one at this actor's transform is exactly the "first actor wins" hazard the
			// FindTrajectoryHeatmap comment at the top of this file documents.
			Heatmap->Destroy();
			return true;
		}

	private:
		FAutomationTestBase& Test;
		FGateStatePtr State;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHeatmapVertexCountIndependentOf3DFlagTest,
	"Mobius.InGame.TrajectoryHeatmap.Mesh.VertexCountIndependentOf3DFlag",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHeatmapVertexCountIndependentOf3DFlagTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryInvariance;
	using namespace HeatmapMeshFlagGate;

	UWorld* World = GetActiveGameWorld();
	if (!World)
	{
		AddError(TEXT("no game world - run via RunTests.ps1 -InGame (UnrealEditor-Cmd -game)"));
		return false;
	}

	// Deliberately NOT SpawnTrajectoryHeatmap: that helper names every actor Heatmap_InvarianceCheck and
	// registers it with the subsystem. A second actor under that name is precisely the coin-toss lookup this
	// file warns about, and registering would put a heatmap into other tests' deposition path.
	AHeatmapPixelTextureVisualizer* Heatmap =
		World->SpawnActor<AHeatmapPixelTextureVisualizer>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("gate heatmap spawned"), Heatmap))
	{
		return false;
	}
	Heatmap->ActorName = TEXT("Heatmap_3DFlagInvarianceGate");
	Heatmap->FloorID = 0;
	// Single section, so the emitted vertex total equals the vertex grid with no per-tile boundary
	// duplication to reason about. Restoring this afterwards is pointless — the actor is destroyed.
	Heatmap->bEnableMultiSectionBatching = false;

	FGateStatePtr State = MakeShared<FGateState, ESPMode::ThreadSafe>();
	State->Heatmap = Heatmap;

	// Flag OFF first, then ON. Order matters only for the error messages, which name which run was which.
	ADD_LATENT_AUTOMATION_COMMAND(FInitAndMeasureCommand(*this, State, /*bIs3D*/ false, 60.0));
	ADD_LATENT_AUTOMATION_COMMAND(FInitAndMeasureCommand(*this, State, /*bIs3D*/ true, 60.0));
	ADD_LATENT_AUTOMATION_COMMAND(FCompareFlagRunsCommand(*this, State));
	return true;
}

// -------------------------------------------------------------------------------------------------
// FR6 GEOMETRY COST ON A REAL FLOOR PLAN.
//
// ⚠️ READ THIS BEFORE QUOTING ANY NUMBER THIS PRODUCES. FR6 is "30+ fps on the OLD-HARDWARE TARGET".
// This test runs on whatever box the suite runs on, which is not that target, so it CANNOT close FR6 and
// nothing here asserts a frame rate. What it does close is the geometry half: after A0-65 made the mesh
// always build at the /25 density, the open question was how many triangles a REAL plan actually keeps,
// because A0-65's own note says the quad-intersect filter and tile culling should put kept geometry far
// below the full grid -- "should" being the operative word, since nobody had measured it.
//
// Deliberately measures KEPT geometry, not grid size. The full grid is arithmetic anyone can do from the
// extent; the number that matters is what survives FindAllQuads' culling mask on a plan with real walls
// and real empty space, and that is a property of the plan, not of the hardware. So this number IS
// portable to the old box even though a frame time measured here would not be.
//
// Also measures what the pre-A0-65 /250 path would have produced, so the ~100x trade the owner accepted
// in principle has an actual figure attached rather than an estimate.
//
// Fixture: UnitTestSampleData/WKT/BUW.wkt -- a real building footprint, tracked in git (so this cannot
// silently skip the way the untracked TechSchool set does), and loaded through
// UpdateMobiusGameInstanceMeshDataFile, the exact call the Browse button makes. WKT goes through Assimp
// and lands in the RuntimeMeshBuilder's procedural mesh with bIsDatasmithAsset false, which is the branch
// of FindAllQuads that reads MobiusProceduralMeshComponent. No Datasmith is involved -- that path
// asserted on SM_CineCam render data during a runtime import on 2026-08-06 and is not worth the risk for
// a measurement that does not need it.
//
// Assertions are structural only. No timing is asserted: absolute times vary per machine and a threshold
// here would flake the gate, which is the same reason MobiusTimingTests keeps its trend rows out of the
// correctness filter. Timings are reported and written to CSV.
// -------------------------------------------------------------------------------------------------
namespace HeatmapGeometryCost
{
	/** One generation's measured geometry and wall-clock cost. */
	struct FCostSample
	{
		bool  b3D = false;
		int32 Sections = 0;
		int32 Verts = 0;
		int32 Tris = 0;
		double BuildAndEmitMs = 0.0;
	};

	struct FCostState
	{
		/** Fixture filename under UnitTestSampleData/WKT, e.g. "BUW.wkt". */
		FString PlanFile;
		TWeakObjectPtr<AHeatmapPixelTextureVisualizer> Heatmap;
		FVector2D PlanSizeCm = FVector2D::ZeroVector;
		FVector   PlanMinCm  = FVector::ZeroVector;
		int32     CullingQuads = 0;
		TArray<FCostSample> Samples;
	};

	using FCostStatePtr = TSharedPtr<FCostState, ESPMode::ThreadSafe>;

	static FString PlanFixturePath(const FString& PlanFile)
	{
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("UnitTestSampleData"), TEXT("WKT"), PlanFile);
	}

	static ARuntimeMeshBuilder* FindMeshBuilder(UWorld* World)
	{
		return World ? Cast<ARuntimeMeshBuilder>(
			UGameplayStatics::GetActorOfClass(World, ARuntimeMeshBuilder::StaticClass())) : nullptr;
	}

	/** Aggregate local bounds of the loaded building, mirroring InitializeHeatmap's own aggregation. */
	static bool GetPlanBounds(ARuntimeMeshBuilder* Builder, FBox& OutBounds)
	{
		if (!Builder || !Builder->MobiusProceduralMeshComponent)
		{
			return false;
		}
		FBox Agg(ForceInit);
		const int32 NumSections = Builder->MobiusProceduralMeshComponent->GetNumSections();
		for (int32 i = 0; i < NumSections; ++i)
		{
			if (const FProcMeshSection* Section = Builder->MobiusProceduralMeshComponent->GetProcMeshSection(i))
			{
				Agg += Section->SectionLocalBox;
			}
		}
		OutBounds = Agg;
		return Agg.IsValid != 0;
	}

	/** Loads the plan through the production path and waits for the building mesh to exist. */
	class FLoadPlanCommand : public IAutomationLatentCommand
	{
	public:
		FLoadPlanCommand(FAutomationTestBase& InTest, FCostStatePtr InState, double InTimeoutSeconds)
			: Test(InTest), State(InState), TimeoutSeconds(InTimeoutSeconds) {}

		virtual bool Update() override
		{
			UWorld* World = TrajectoryInvariance::GetActiveGameWorld();
			if (!World)
			{
				Test.AddError(TEXT("no game world"));
				return true;
			}

			if (!bKicked)
			{
				const FString Path = PlanFixturePath(State->PlanFile);
				if (!FPaths::FileExists(Path))
				{
					// Not a skip. The fixture is tracked in git, so its absence is a broken checkout and
					// saying "skipped" would report that as a pass.
					Test.AddError(FString::Printf(TEXT("floor-plan fixture missing: %s"), *Path));
					return true;
				}
				// The two calls IProjectMobiusInterface::UpdateMobiusGameInstanceMeshDataFile makes, which
				// is in turn the exact call ULoadMeshWidget makes after a Browse selection. Inlined here
				// because that interface method is NOT static — the preload subsystem can write
				// `IProjectMobiusInterface::...` only because it implements the interface itself, and a
				// test is not going to inherit an interface just to reach two setters.
				UProjectMobiusGameInstance* GameInstance = World->GetGameInstance<UProjectMobiusGameInstance>();
				if (!GameInstance)
				{
					Test.AddError(TEXT("no UProjectMobiusGameInstance - cannot load the floor plan"));
					return true;
				}
				GameInstance->SetSimulationMeshFilePath(Path);
				GameInstance->SetSimulationMeshFileName(FPaths::GetCleanFilename(Path));
				Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
				bKicked = true;
				return false;
			}

			FBox Bounds(ForceInit);
			if (GetPlanBounds(FindMeshBuilder(World), Bounds) && Bounds.GetSize().X > 1.0 && Bounds.GetSize().Y > 1.0)
			{
				State->PlanMinCm = Bounds.Min;
				// Extents x2 is how UHeatmapSubsystem::UpdateSpawnLocationAndHeatmapSize derives the
				// heatmap size in production; taking GetSize() directly is the same number.
				State->PlanSizeCm = FVector2D(Bounds.GetSize().X, Bounds.GetSize().Y);
				Test.AddInfo(FString::Printf(TEXT("plan loaded: %s  %.0f x %.0f cm (%.1f x %.1f m)"),
					*State->PlanFile, State->PlanSizeCm.X, State->PlanSizeCm.Y,
					State->PlanSizeCm.X / 100.0, State->PlanSizeCm.Y / 100.0));
				return true;
			}

			if (FPlatformTime::Seconds() > Deadline)
			{
				Test.AddError(FString::Printf(
					TEXT("timed out after %.0f s waiting for %s to import into the RuntimeMeshBuilder's ")
					TEXT("procedural mesh. Check that an ARuntimeMeshBuilder exists in the level and that ")
					TEXT("the Assimp WKT path still populates MobiusProceduralMeshComponent."),
					TimeoutSeconds, *PlanFixturePath(State->PlanFile)));
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase& Test;
		FCostStatePtr State;
		double TimeoutSeconds;
		bool bKicked = false;
		double Deadline = 0.0;
	};

	/** One generation at the plan's extent, timed from InitializeHeatmap to emit completion. */
	class FMeasureGenerationCommand : public IAutomationLatentCommand
	{
	public:
		FMeasureGenerationCommand(FAutomationTestBase& InTest, FCostStatePtr InState, bool bIn3D, double InTimeoutSeconds)
			: Test(InTest), State(InState), bIs3D(bIn3D), TimeoutSeconds(InTimeoutSeconds) {}

		virtual bool Update() override
		{
			UWorld* World = TrajectoryInvariance::GetActiveGameWorld();
			if (!World || State->PlanSizeCm.IsNearlyZero())
			{
				// The load command already reported why; don't pile on a second error for one cause.
				return true;
			}

			if (!bKicked)
			{
				AHeatmapPixelTextureVisualizer* Heatmap = State->Heatmap.Get();
				if (!Heatmap)
				{
					// Spawned at the plan's MIN corner: FindAllQuads marches from GetActorLocation() and
					// bounds the mesh at StartPos..StartPos+size, so an actor anywhere else culls against
					// the wrong region and the kept fraction would be meaningless.
					Heatmap = World->SpawnActor<AHeatmapPixelTextureVisualizer>(
						FVector(State->PlanMinCm.X, State->PlanMinCm.Y, State->PlanMinCm.Z),
						FRotator::ZeroRotator);
					if (!Heatmap)
					{
						Test.AddError(TEXT("failed to spawn the cost-measurement heatmap"));
						return true;
					}
					Heatmap->ActorName = TEXT("Heatmap_GeometryCostProbe");
					Heatmap->FloorID = 0;
					// Production configuration: tiled emit ON, default tile size. Unlike the invariance
					// gate, this measurement wants the cost the app actually pays, including the
					// per-tile boundary-vertex duplication.
					Heatmap->bEnableMultiSectionBatching = true;
					State->Heatmap = Heatmap;
				}

				BaselineEmitCount = Heatmap->CompletedTileEmitCount;
				StartSeconds = FPlatformTime::Seconds();
				Deadline = StartSeconds + TimeoutSeconds;
				bKicked = true;
				// HeatmapType 2, live tracking on, 3D per the parameter: this mirrors
				// UHeatmapSubsystem::CreateHeatmap, which passes bIs3DHeatmap = true in production.
				Heatmap->InitializeHeatmap(2, true, State->PlanSizeCm, 0.0f, bIs3D);
				return false;
			}

			AHeatmapPixelTextureVisualizer* Heatmap = State->Heatmap.Get();
			if (!Heatmap)
			{
				Test.AddError(TEXT("the cost-measurement heatmap went away mid-run"));
				return true;
			}

			if (Heatmap->CompletedTileEmitCount > BaselineEmitCount)
			{
				FCostSample Sample;
				Sample.b3D = bIs3D;
				Sample.Sections = Heatmap->CountEmittedMeshSectionsForTesting();
				Sample.Verts = Heatmap->CountEmittedMeshVerticesForTesting();
				Sample.Tris = Heatmap->CountEmittedMeshTrianglesForTesting();
				Sample.BuildAndEmitMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
				State->CullingQuads = Heatmap->CountCullingQuadsForTesting();
				State->Samples.Add(Sample);
				return true;
			}

			if (FPlatformTime::Seconds() > Deadline)
			{
				Test.AddError(FString::Printf(
					TEXT("timed out after %.0f s building the heatmap mesh at %.0f x %.0f cm (3D=%s)"),
					TimeoutSeconds, State->PlanSizeCm.X, State->PlanSizeCm.Y,
					bIs3D ? TEXT("true") : TEXT("false")));
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase& Test;
		FCostStatePtr State;
		bool bIs3D;
		double TimeoutSeconds;
		bool bKicked = false;
		int32 BaselineEmitCount = 0;
		double StartSeconds = 0.0;
		double Deadline = 0.0;
	};

	/** Reports the cost table, writes the trend row, and asserts the structural facts only. */
	class FReportCostCommand : public IAutomationLatentCommand
	{
	public:
		FReportCostCommand(FAutomationTestBase& InTest, FCostStatePtr InState)
			: Test(InTest), State(InState) {}

		virtual bool Update() override
		{
			if (State->Samples.Num() < 2)
			{
				// Errors already reported upstream; nothing measured means nothing to report.
				if (AHeatmapPixelTextureVisualizer* Dead = State->Heatmap.Get()) { Dead->Destroy(); }
				return true;
			}

			const FCostSample& Flat = State->Samples[0];
			const FCostSample& Displaced = State->Samples[1];

			const FIntPoint Grid = AHeatmapPixelTextureVisualizer::ComputeHeatmapVertexGrid(State->PlanSizeCm);
			const int64 FullGridTris = 2LL * FMath::Max(0, Grid.X - 1) * FMath::Max(0, Grid.Y - 1);

			// What the pre-A0-65 path would have built. /250 instead of /25 is 10x coarser per AXIS, so
			// ~100x fewer quads -- this is the trade the owner accepted, with a number on it at last.
			const FIntPoint LegacyGrid(static_cast<int32>(State->PlanSizeCm.X / 250.0),
			                           static_cast<int32>(State->PlanSizeCm.Y / 250.0));
			const int64 LegacyTris = 2LL * FMath::Max(0, LegacyGrid.X - 1) * FMath::Max(0, LegacyGrid.Y - 1);

			const double KeptPct = 100.0 * Flat.Tris / FMath::Max<int64>(1, FullGridTris);

			Test.AddInfo(FString::Printf(
				TEXT("--- FR6 geometry cost, real plan (%s). NOT a frame-rate verdict ---"), *State->PlanFile));
			Test.AddInfo(FString::Printf(TEXT("plan extent        : %.1f x %.1f m"),
				State->PlanSizeCm.X / 100.0, State->PlanSizeCm.Y / 100.0));
			Test.AddInfo(FString::Printf(TEXT("vertex grid (/25)  : %d x %d  -> %lld tris at full density"),
				Grid.X, Grid.Y, FullGridTris));
			Test.AddInfo(FString::Printf(TEXT("legacy grid (/250) : %d x %d  -> %lld tris (pre-A0-65)"),
				LegacyGrid.X, LegacyGrid.Y, LegacyTris));
			Test.AddInfo(FString::Printf(TEXT("culling mask       : %d quads from the building footprint"),
				State->CullingQuads));
			Test.AddInfo(FString::Printf(TEXT("KEPT (3D off)      : %d sections, %d verts, %d tris = %.1f%% of full grid"),
				Flat.Sections, Flat.Verts, Flat.Tris, KeptPct));
			Test.AddInfo(FString::Printf(TEXT("KEPT (3D on)       : %d sections, %d verts, %d tris"),
				Displaced.Sections, Displaced.Verts, Displaced.Tris));
			Test.AddInfo(FString::Printf(TEXT("build+emit         : %.1f ms (3D off), %.1f ms (3D on)"),
				Flat.BuildAndEmitMs, Displaced.BuildAndEmitMs));
			Test.AddInfo(FString::Printf(TEXT("kept vs legacy     : %.2fx the triangles the /250 path would have built"),
				LegacyTris > 0 ? static_cast<double>(Flat.Tris) / static_cast<double>(LegacyTris) : 0.0));

			WriteTrendRow(Flat, Displaced, Grid, FullGridTris, LegacyTris);

			// ---- Assertions: structural only, never timing ----
			Test.TestTrue(TEXT("the plan produced a culling mask (otherwise this is not measuring a real footprint)"),
				State->CullingQuads > 0);
			Test.TestTrue(TEXT("kept triangles are non-zero"), Flat.Tris > 0);
			Test.TestTrue(FString::Printf(TEXT("kept triangles %d do not exceed the full grid %lld"),
				Flat.Tris, FullGridTris), Flat.Tris <= FullGridTris);
			// NOT asserted: that culling removed anything. A0-65 expected kept geometry to sit far below
			// the full grid, and this measurement exists to find out whether it does — so requiring it
			// here would assert the hypothesis instead of testing it, and would redden the suite on a plan
			// that legitimately keeps everything. Reported instead, with the reason.
			//
			// FindAllQuads marches in 650 cm cells (its StepSize), which is 26x coarser than the 25 cm mesh
			// cell, and it keeps any cell containing a single building vertex. So culling can only drop
			// whole 650 cm blocks that are completely empty: a compact envelope has almost none, and a plan
			// whose bounding box spans only a few cells has nowhere to drop at all.
			if (Flat.Tris >= FullGridTris)
			{
				Test.AddInfo(FString::Printf(
					TEXT("NO CULLING on this plan: kept %d of %lld triangles. Its bounding box is %.1f x %.1f ")
					TEXT("of the 650 cm mask cell, so there is no fully-empty cell to drop."),
					Flat.Tris, FullGridTris, State->PlanSizeCm.X / 650.0, State->PlanSizeCm.Y / 650.0));
			}
			// Confirms A0-65's invariant on a REAL plan, where culling is active, not just on an
			// unculled fixture.
			Test.TestEqual(TEXT("kept triangles are identical with 3D on and off, on a real plan too"),
				Displaced.Tris, Flat.Tris);
			Test.TestEqual(TEXT("kept vertices are identical with 3D on and off, on a real plan too"),
				Displaced.Verts, Flat.Verts);

			if (AHeatmapPixelTextureVisualizer* Probe = State->Heatmap.Get())
			{
				Probe->Destroy();
			}

			// PUT THE WORLD BACK. Tier B runs every test in ONE session and this is the only test that
			// loads building geometry, so anything left behind changes what its siblings measure. It sorts
			// alphabetically BEFORE Mesh.VertexCountIndependentOf3DFlag, whose exact vertex assertion holds
			// only while the culling mask is empty -- leaving BUW loaded would silently push that gate down
			// its "a plan is loaded" branch and cost it the one number it exists to pin. Any later heatmap
			// would also be culled against a footprint its own test never asked for.
			UWorld* World = TrajectoryInvariance::GetActiveGameWorld();
			if (ARuntimeMeshBuilder* Builder = FindMeshBuilder(World))
			{
				Builder->ClearMobiusProceduralMesh();
				Test.AddInfo(TEXT("building geometry cleared - the world is back to unculled for later tests"));
			}

			// AND reset the subsystem's retained bounds, which is the part that actually bites.
			//
			// Loading geometry makes ARuntimeMeshBuilder broadcast OnMeshBuilt, which lands in
			// UHeatmapSubsystem::UpdateSpawnLocationAndHeatmapSize and leaves HeatmapBoundingSize non-zero
			// FOR THE REST OF THE SESSION. ProcessHeatmapGeneration bails while either the bounds are zero
			// or no floor heights are known; before this test existed the bounds were always zero in Tier B
			// because nothing ever loaded a building, so the guard held for free. With them set, the next
			// test to spawn agents satisfies BOTH guards, and ProcessHeatmapGeneration responds by
			// destroying every registered heatmap and replacing it with app-owned Heatmap_<n> actors. The
			// sibling tests then fail with "no trajectory heatmap found at pass end" — measured, not
			// hypothesised: T_INV_1, T_INV_2 and T_REG_1 all failed exactly that way before this reset
			// existed. Clearing the mesh does NOT undo it; the retained bounds are the damage.
			//
			// Zeroing through the same public entry point restores the early-out. It re-arms the generation
			// timer, which then bails on IsZero() as it did before this test ran.
			if (UHeatmapSubsystem* Subsystem = World ? World->GetSubsystem<UHeatmapSubsystem>() : nullptr)
			{
				Subsystem->UpdateSpawnLocationAndHeatmapSize(FVector::ZeroVector, FVector::ZeroVector);
				Test.AddInfo(TEXT("heatmap subsystem spawn bounds reset to zero - sibling tests keep their own heatmaps"));
			}
			return true;
		}

	private:
		void WriteTrendRow(const FCostSample& Flat, const FCostSample& Displaced,
		                   const FIntPoint& Grid, int64 FullGridTris, int64 LegacyTris) const
		{
			const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusPerfTimings"));
			IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true);
			const FString CsvPath = FPaths::Combine(Dir, TEXT("heatmap_geometry_cost.csv"));

			FString Csv;
			if (!FPaths::FileExists(CsvPath))
			{
				Csv = TEXT("utc,plan,plan_x_cm,plan_y_cm,grid_x,grid_y,full_tris,legacy_tris,")
				      TEXT("culling_quads,kept_sections,kept_verts,kept_tris,kept_pct,")
				      TEXT("build_emit_ms_2d,build_emit_ms_3d\n");
			}
			Csv += FString::Printf(TEXT("%s,%s,%.0f,%.0f,%d,%d,%lld,%lld,%d,%d,%d,%d,%.3f,%.2f,%.2f\n"),
				*FDateTime::UtcNow().ToIso8601(), *State->PlanFile, State->PlanSizeCm.X, State->PlanSizeCm.Y,
				Grid.X, Grid.Y, FullGridTris, LegacyTris, State->CullingQuads,
				Flat.Sections, Flat.Verts, Flat.Tris,
				100.0 * Flat.Tris / FMath::Max<int64>(1, FullGridTris),
				Flat.BuildAndEmitMs, Displaced.BuildAndEmitMs);

			FFileHelper::SaveStringToFile(Csv, *CsvPath, FFileHelper::EEncodingOptions::AutoDetect,
				&IFileManager::Get(), FILEWRITE_Append);
			Test.AddInfo(FString::Printf(TEXT("trend row appended to %s"), *CsvPath));
		}

		FAutomationTestBase& Test;
		FCostStatePtr State;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHeatmapGeometryCostOnRealPlanTest,
	"Mobius.InGame.TrajectoryHeatmap.Mesh.GeometryCostOnRealPlan",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHeatmapGeometryCostOnRealPlanTest::RunTest(const FString& Parameters)
{
	using namespace TrajectoryInvariance;
	using namespace HeatmapGeometryCost;

	UWorld* World = GetActiveGameWorld();
	if (!World)
	{
		AddError(TEXT("no game world - run via RunTests.ps1 -InGame (UnrealEditor-Cmd -game)"));
		return false;
	}

	// TWO plans, because one footprint cannot tell "this plan keeps 83%" from "plans keep 83%", and the
	// conclusion drawn from this measurement contradicts A0-65's expectation that culling would put kept
	// geometry far below the grid. BUW is a compact building envelope -- the realistic case. t_shaped_room
	// is mostly empty bounding box, chosen as the opposite end on the theory that if culling ever works it
	// works there.
	//
	// MEASURED 2026-08-07: that theory did not hold. Both keep ~80% (82.6% and 79.7%), because the two
	// plans have something in common that matters more than their shape -- they span only 7.7x5.0 and
	// 3.1x1.5 of FindAllQuads' 650 cm mask cells, and a mask that coarse has almost no wholly-empty cell to
	// drop at either shape. So this pair does NOT bound the range for large plans; A0-65's real worry case
	// is the 200 m carrier (~30x30 mask cells) and that dataset is not on this machine. Anyone adding a
	// bigger fixture here should read A0-78 first -- the mechanism is understood, so a third small plan adds
	// nothing.
	const TCHAR* PlanFixtures[] = { TEXT("BUW.wkt"), TEXT("t_shaped_room.wkt") };

	for (const TCHAR* PlanFile : PlanFixtures)
	{
		FCostStatePtr State = MakeShared<FCostState, ESPMode::ThreadSafe>();
		State->PlanFile = PlanFile;

		// Each plan gets its own probe actor and its own restore, so a failure on one still leaves the
		// world clean for the next and for every later test in the session.
		ADD_LATENT_AUTOMATION_COMMAND(FLoadPlanCommand(*this, State, 120.0));
		ADD_LATENT_AUTOMATION_COMMAND(FMeasureGenerationCommand(*this, State, /*bIs3D*/ false, 120.0));
		ADD_LATENT_AUTOMATION_COMMAND(FMeasureGenerationCommand(*this, State, /*bIs3D*/ true, 120.0));
		ADD_LATENT_AUTOMATION_COMMAND(FReportCostCommand(*this, State));
	}
	return true;
}

#endif // !UE_BUILD_SHIPPING
