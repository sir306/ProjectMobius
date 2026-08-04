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

	static AHeatmapPixelTextureVisualizer* FindTrajectoryHeatmap(UWorld* World)
	{
		if (!World) { return nullptr; }
		for (TActorIterator<AHeatmapPixelTextureVisualizer> It(World); It; ++It)
		{
			if (IsValid(*It) && It->bTrajectoryHeatmap)
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
	ADD_LATENT_AUTOMATION_COMMAND(FRunInvariancePassCommand(*this, SampleInterval * 1.0f, /*bRewindFirst*/ false, Results));
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
	ADD_LATENT_AUTOMATION_COMMAND(FRunInvariancePassCommand(*this, SampleInterval, /*bRewindFirst*/ false, Results));
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
	ADD_LATENT_AUTOMATION_COMMAND(FRunInvariancePassCommand(*this, SampleInterval, /*bRewindFirst*/ false, Results));
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

#endif // !UE_BUILD_SHIPPING
