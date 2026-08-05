// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// TrajectoryHeatmapRealDatasetTest.cpp
//
// Drives the trajectory heatmap against the REAL TechSchool datasets and produces the capture the band
// recalibration needs. Tier B's other scenarios use synthetic straight-line fixtures, which is right for
// pinning arithmetic and useless for calibration: band edges have to be fitted against real speeds,
// queuing and congestion, and a fixture has none of those.
//
// Two datasets, two different jobs:
//
//   Baseline1000        TechnicalSchool_1000.json           - the repeatable case. Sane distribution,
//                                                             most texels crossed a handful of times.
//   Oversaturation5000  TechnicalSchool5000DefaultExits.json - deliberately past the OLD uint8
//                                                             accumulator's ceiling.
//
// UPDATED FOR THE TRAJECTORY REBUILD (T5, 2026-08-04). Oversaturation5000's point used to be "clips hard
// and renders largely red" -- that was true of the old fixed-scale uint8 buffer this rebuild removed. The
// canonical field (FTrajectoryField, float32 with no ceiling) does not clip; only the presentation encode
// does, and it does so via linear auto-exposure against the current maximum cell DENSITY
// (EncodeToDisplay, see TrajectoryField.h), so a hot cell alone reaching byte 255 does not mean the map
// renders "largely red" the way the old fixed increments did. See the Oversaturation5000 case below for
// what is actually asserted now, and its comments for what is NOT verifiable at this tier (canonical
// access is via AHeatmapPixelTextureVisualizer::GetTrajectoryFieldForTesting(), added by A4 -- confirmed
// available by A0 2026-08-04 -- but proving "provably unbounded" rigorously is Tier A's T_SAT_1 job, done
// in TrajectoryHeatmapCalibrationTest.cpp against a synthetic overload; this file's job is to confirm the
// REAL pipeline agrees with that isolated finding, not to re-derive it).
//
// Each run arms FTrajectoryCaptureRecorder, so the artifacts are the full capture set under
// Saved/TrajectoryCapture/<stamp>/ -- raster.csv, texture.csv, accumulation_los.png and
// accumulation_scaled.png. Feed the exported .csv + .meta.json pair to
// MobiusPerf\analysis\field_stats.py and then band_fit.py. (The older hits_per_crossing.py and
// crossings_vs_byte.py are GONE: both measured hits-per-crossing, a property of the brush rasteriser
// this rebuild removed, and there is no brush radius left for them to self-configure from.)
//
// THE DATASET IS NOT IN GIT. .gitignore:162 excludes
// UnrealFolder/ProjectMobius/UnitTestSampleData/TechSchoolTest/ deliberately -- it is ~760 MB. These
// tests therefore SKIP (pass with a warning) when it is absent rather than failing, so the suite stays
// green on a machine that does not have it. Point them somewhere else with:
//
//     -MobiusTechSchoolDir="D:\...\Packaged\Development\Windows\ProjectMobius\UnitTestSampleData\TechSchoolTest"
//
// A SKIP IS NOT A PASS. RunTests.ps1 keys off $t.state, and AddWarning alone does not fail a test, so an
// absent dataset currently reads green exactly like a real pass. The skip path below now logs a
// maximally-distinctive "SKIPPED-NOT-A-PASS" marker for both these reasons; A0 must grep the run log for
// that literal string (or otherwise confirm the dataset was present) before treating a green run of this
// file as evidence the real-dataset scenarios actually executed. This file cannot fix that on its own --
// RunTests.ps1's pass/fail semantics are A0's file, not this agent's (T5_TEST_REWRITE.md §6 / AGENTS.md
// §4 file-ownership matrix).
//
// Run: MobiusPerf\RunTests.ps1 -InGame     (or -ExecCmds="Automation RunTests Mobius.InGame.TrajectoryRealData")
//
// ---------------------------------------------------------------------------------------------------
// TODO (owner ruling 2026-08-03, deferred for deadline): this drives the SUBSYSTEM directly -- it sets
// the data path on the game instance and calls SetTrajectoryHeatmapsEnabled, bypassing the UI entirely.
// A true widget-driven case is still wanted: click through to the display panel and toggle the ground
// floor heatmap for real, so a broken toggle or a rewired panel actually fails a test. Note that the
// file picker itself can never be part of that -- it is a blocking native modal (IDesktopPlatform /
// PortableFileDialogs), so automation has to enter at ULoadDataParentWidget::DialogClosed(), which is
// the handler the dialog invokes. Everything downstream of the path is fair game; the picker is not.
// ---------------------------------------------------------------------------------------------------
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Tests/AutomationCommon.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "MassAI/SubSystems/MassEntitySpawnSubsystem.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "SimData/ISimSampleProvider.h"
#include "Subsystems/TimeDilationSubSystem.h"
#include "Actors/HeatmapPixelTextureVisualizer.h"
#include "Subsystems/HeatmapSubsystem.h"
#include "Diagnostics/TrajectoryCaptureRecorder.h"
#include "DynamicPixelRenderingTexture.h"
#include "TrajectoryField.h"

namespace TrajectoryRealData
{
	enum class EDataset : uint8
	{
		Baseline1000,
		Oversaturation5000
	};

	/** Sim seconds to capture. Matches the 20260728_165413 capture so the two are comparable. */
	static constexpr float CaptureSeconds = 30.0f;

	/** Playhead step. Mirrors UpdateHeatmapInterval's 0.1 s gate exactly. */
	static constexpr float SampleInterval = 0.1f;

	/**
	 * Heatmap span in metres.
	 *
	 * The SPAN is not derived from the building: these runs import agent movement only, and
	 * UHeatmapSubsystem sizes its heatmaps from HeatmapBoundingSize, which is populated by a building
	 * import. Rather than pull a 27 MB fbx through the runtime mesh path just to get a number, the size is
	 * passed in (exactly as mobius.Heatmap.SpawnTrajectory does) and the run self-checks: positions
	 * outside the mesh are CLAMPED onto the texture edge by the rasteriser, so a pile-up on the border
	 * means undersized. ReportEdgePileup below turns that into a warning instead of a silently squashed
	 * capture.
	 *
	 * The PLACEMENT, unlike the span, IS derived from the data - see ComputeAgentBoundsCm. Span and
	 * placement are separable and only the span is a judgement call.
	 *
	 * Override with -MobiusHeatmapMetres=<n>.
	 */
	static constexpr float DefaultHeatmapMetres = 200.0f;

	static const TCHAR* NameFor(EDataset Dataset)
	{
		return Dataset == EDataset::Baseline1000 ? TEXT("Baseline1000") : TEXT("Oversaturation5000");
	}

	static const TCHAR* FileNameFor(EDataset Dataset)
	{
		return Dataset == EDataset::Baseline1000
			? TEXT("TechnicalSchool_1000.json")
			: TEXT("TechnicalSchool5000DefaultExits.json");
	}

	static float HeatmapMetres()
	{
		float Metres = DefaultHeatmapMetres;
		FParse::Value(FCommandLine::Get(), TEXT("MobiusHeatmapMetres="), Metres);
		return Metres;
	}

	/** How many timestep blocks to sample when locating the agents. Placement does not need all of them. */
	static constexpr int32 BoundsProbeBlocks = 64;

	/** This file's own heatmap. Must be unique across the session - see FindOwnHeatmap. */
	static const TCHAR* OwnHeatmapName = TEXT("Heatmap_RealDatasetCapture");

	/**
	 * The heatmap THIS file spawned, matched by name.
	 *
	 * "First actor with bTrajectoryHeatmap" is not a lookup, it is a coin toss: every Tier B test in the
	 * session leaves its heatmap in the world, so this file was measuring whichever one happened to come
	 * first out of TActorIterator - in practice TrajectoryHeatmapInGameTest's 50 m invariance heatmap
	 * rather than its own 200 m capture. That was invisible while both were spawned at the world origin
	 * and produced a 76% off-grid drop the moment they stopped coinciding.
	 */
	static AHeatmapPixelTextureVisualizer* FindOwnHeatmap(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AHeatmapPixelTextureVisualizer> It(World); It; ++It)
		{
			if (IsValid(*It) && It->bTrajectoryHeatmap && It->ActorName == OwnHeatmapName)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/**
	 * World-cm XY bounding box of the loaded agent data, or an invalid box if it cannot be read.
	 *
	 * The heatmap has to be PLACED, not just sized. FTrajectoryField takes the mesh component's world
	 * location as the grid's MINIMUM corner, and the importer negates Y - a source sample at (x, y) metres
	 * becomes world (+100x, -100y) cm. An actor spawned at FVector::ZeroVector therefore covers
	 * [0, +span] on both axes while every agent sits at negative Y, and the field books the whole capture
	 * to dropped mass. That is exactly what made three synthetic Tier B tests green against an empty field
	 * (STATUS 6.8, ruling A0-28); here it would have silently truncated the capture at Y = 0 instead,
	 * which is worse, because a half-full heatmap still looks like data.
	 *
	 * Reading the real extent rather than assuming the sign also means this keeps working if the importer's
	 * convention ever changes: the box is measured, not derived.
	 *
	 * Sampled at a stride rather than exhaustively - the 5000-agent file is millions of samples and this
	 * only has to be good enough to centre a 200 m span. Stand-in blocks (streaming cold miss) are skipped
	 * because their positions belong to another timestep. Undersizing is still caught downstream by
	 * ReportEdgePileup.
	 */
	static FBox2D ComputeAgentBoundsCm(UWorld* World)
	{
		FBox2D Bounds(ForceInit);

		const UMassEntitySpawnSubsystem* SpawnSubsystem = World ? World->GetSubsystem<UMassEntitySpawnSubsystem>() : nullptr;
		const FSimulationFragment* Simulation = SpawnSubsystem ? SpawnSubsystem->GetSimulationFragment() : nullptr;
		const ISimSampleProvider* const Provider = Simulation ? Simulation->Provider.Get() : nullptr;
		if (!Provider || !Provider->IsValidAndPopulated())
		{
			return Bounds;
		}

		const int32 NumTimesteps = Provider->GetNumTimesteps();
		if (NumTimesteps <= 0)
		{
			return Bounds;
		}
		const int32 Stride = FMath::Max(1, NumTimesteps / BoundsProbeBlocks);

		for (int32 Step = 0; Step < NumTimesteps; Step += Stride)
		{
			if (!Provider->HasExactSamplesForTimestep(Step))
			{
				continue;
			}
			const TArray<FSimMovementSample>* Block = Provider->GetSamplesForTimestep(Step);
			if (!Block)
			{
				continue;
			}
			for (const FSimMovementSample& Sample : *Block)
			{
				Bounds += FVector2D(Sample.Position.X, Sample.Position.Y);
			}
		}
		return Bounds;
	}

	/**
	 * Spawn location for a heatmap of the given span, centred on wherever the agents actually are.
	 * Returns the origin (grid MINIMUM corner), which is also what the rendered-view camera offsets from.
	 * Z stays 0: the subsystem's floor filter is a band of MaxAddHeight (10 cm) ABOVE the mesh's Z.
	 */
	static FVector HeatmapOriginForAgents(UWorld* World, float SizeCm, FBox2D& OutBounds)
	{
		OutBounds = ComputeAgentBoundsCm(World);
		if (!OutBounds.bIsValid)
		{
			return FVector::ZeroVector;
		}
		const FVector2D Centre = OutBounds.GetCenter();
		return FVector(Centre.X - SizeCm * 0.5f, Centre.Y - SizeCm * 0.5f, 0.0);
	}

	/**
	 * Absolute path to the dataset, or empty if it is not on this machine.
	 *
	 * Canonical location first (the path .gitignore already reserves), then the command-line override.
	 * The override exists because on a dev box the only copy is often the one under Packaged/.
	 */
	static FString ResolveDatasetPath(EDataset Dataset)
	{
		FString OverrideDir;
		if (FParse::Value(FCommandLine::Get(), TEXT("MobiusTechSchoolDir="), OverrideDir))
		{
			const FString Candidate = FPaths::Combine(OverrideDir, FileNameFor(Dataset));
			if (FPaths::FileExists(Candidate))
			{
				return Candidate;
			}
		}

		const FString Canonical = FPaths::Combine(
			FPaths::ProjectDir(), TEXT("UnitTestSampleData"), TEXT("TechSchoolTest"), FileNameFor(Dataset));
		return FPaths::FileExists(Canonical) ? Canonical : FString();
	}

	// PROVISIONAL (see file header: TOLERANCES.md does not exist yet, A1b is writing it). Named here, in
	// one block, rather than inlined at any call site below.
	constexpr float OversaturationMaxSaturatedFraction = 0.05f; // "saturates gracefully" == a FEW hot cells

	/** Summary of one accumulation buffer. Mirrors the Tier B struct so the two runs read alike. */
	struct FRealDataStats
	{
		int32 TouchedTexels = 0;
		int32 PeakByte = 0;
		int32 ModalByte = 0;
		int32 SaturatedTexels = 0;   // byte == 255, i.e. counts already lost
		int32 EdgeTexels = 0;        // touched texels on the outermost row/column
		int32 BelowFirstBandTexels = 0;  // touched texels below the provisional first band edge (reported, not gated)

		// Canonical-field companions to the byte-level stats above, read through
		// GetTrajectoryFieldForTesting() rather than the encoded display buffer. Zero/invalid if the
		// field was not available (older builds, or the accessor not yet wired for this actor).
		bool bCanonicalFieldAvailable = false;
		double CanonicalTotalPersonMetres = 0.0;
		float CanonicalPeakDensity = 0.0f; // max(PersonMetres[cell]) / CellArea, i.e. peak Route Usage

		float SaturatedFraction() const
		{
			return TouchedTexels > 0 ? static_cast<float>(SaturatedTexels) / TouchedTexels : 0.0f;
		}
	};

	static FRealDataStats Measure(const UDynamicPixelRenderingTexture& Texture)
	{
		// LOS_A edge for the trajectory surface, mirrored from FHeatmapLOSBands::Trajectory().
		constexpr float TrajectoryLOS_A = 24.5f / 255.0f;

		FRealDataStats Stats;
		const FVector2D Dimensions = Texture.GetDynamicTextureSize();
		const int32 Width = FMath::TruncToInt(Dimensions.X);
		const int32 Height = FMath::TruncToInt(Dimensions.Y);

		TMap<uint8, int32> Histogram;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const uint8 Red = Texture.GetRawPixelRed(X, Y);
				if (Red == 0)
				{
					continue;
				}
				++Stats.TouchedTexels;
				++Histogram.FindOrAdd(Red);
				Stats.PeakByte = FMath::Max(Stats.PeakByte, static_cast<int32>(Red));
				if (Red >= 255)
				{
					++Stats.SaturatedTexels;
				}
				if ((static_cast<float>(Red) / 255.0f) < TrajectoryLOS_A)
				{
					++Stats.BelowFirstBandTexels;
				}
				if (X == 0 || Y == 0 || X == Width - 1 || Y == Height - 1)
				{
					++Stats.EdgeTexels;
				}
			}
		}

		int32 Best = 0;
		for (const TPair<uint8, int32>& Entry : Histogram)
		{
			if (Entry.Value > Best)
			{
				Best = Entry.Value;
				Stats.ModalByte = Entry.Key;
			}
		}
		return Stats;
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

	// --- latent commands -------------------------------------------------------------------------

	/**
	 * Wait for the import to settle.
	 *
	 * Deliberately not "== N": a real dataset's entity count is a property of the file, not something a
	 * test should assert it already knows. Waits for a non-zero count that stops changing.
	 */
	class FWaitForRealDataLoadedCommand : public IAutomationLatentCommand
	{
	public:
		FWaitForRealDataLoadedCommand(FAutomationTestBase& InTest, double InTimeoutSeconds)
			: Test(InTest), Deadline(FPlatformTime::Seconds() + InTimeoutSeconds) {}

		virtual bool Update() override
		{
			if (const UWorld* World = GetActiveGameWorld())
			{
				if (const UAgentDataSubsystem* AgentData = World->GetSubsystem<UAgentDataSubsystem>())
				{
					const int32 Count = AgentData->CachedEntityData.Num();
					if (Count > 0 && Count == LastCount)
					{
						if (++StableChecks >= 10)
						{
							UE_LOG(LogTemp, Display,
								TEXT("[TrajectoryRealData] imported %d entities"), Count);
							return true;
						}
					}
					else
					{
						StableChecks = 0;
					}
					LastCount = Count;
				}
			}
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test.AddError(FString::Printf(
					TEXT("timed out importing the dataset (last entity count %d). A 325 MB JSON can ")
					TEXT("legitimately exceed the default budget - raise the timeout before assuming a hang."),
					LastCount));
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase& Test;
		double Deadline;
		int32 LastCount = -1;
		int32 StableChecks = 0;
	};

	/** Spawn a trajectory heatmap of an explicit size, then arm the capture recorder. */
	class FSpawnAndArmCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSpawnAndArmCommand(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GetActiveGameWorld();
			UHeatmapSubsystem* Subsystem = World ? World->GetSubsystem<UHeatmapSubsystem>() : nullptr;
			if (!Subsystem)
			{
				Test.AddError(TEXT("no HeatmapSubsystem - run via RunTests.ps1 -InGame"));
				return true;
			}

			const float Metres = HeatmapMetres();
			const float SizeCm = Metres * 100.0f;

			// Placed over the agents, not at the world origin - see ComputeAgentBoundsCm for why the origin
			// is the wrong answer and how the extent is measured rather than assumed.
			FBox2D AgentBounds(ForceInit);
			const FVector SpawnOrigin = HeatmapOriginForAgents(World, SizeCm, AgentBounds);
			if (AgentBounds.bIsValid)
			{
				const FVector2D Span = AgentBounds.GetSize();
				UE_LOG(LogTemp, Display,
					TEXT("[TrajectoryRealData] agent extent X [%.0f, %.0f] Y [%.0f, %.0f] cm (%.1f x %.1f m); ")
					TEXT("%.0f m heatmap placed with its minimum corner at (%.0f, %.0f)"),
					AgentBounds.Min.X, AgentBounds.Max.X, AgentBounds.Min.Y, AgentBounds.Max.Y,
					Span.X / 100.0f, Span.Y / 100.0f, Metres, SpawnOrigin.X, SpawnOrigin.Y);
				if (Span.X > SizeCm || Span.Y > SizeCm)
				{
					Test.AddWarning(FString::Printf(
						TEXT("agents span %.1f x %.1f m but the heatmap is %.0f m - the capture will be ")
						TEXT("clipped. Re-run with -MobiusHeatmapMetres=%.0f or larger."),
						Span.X / 100.0f, Span.Y / 100.0f, Metres,
						FMath::CeilToFloat(FMath::Max(Span.X, Span.Y) / 100.0f)));
				}
			}
			else
			{
				// Not fatal: the capture may still be usable, but nobody should fit a band edge to it
				// without knowing the grid was placed blind.
				Test.AddWarning(TEXT("could not read the agent extent from the sim provider - falling back to ")
					TEXT("the world origin, which is very likely the wrong half of Y. Treat this capture as ")
					TEXT("unplaced."));
			}

			AHeatmapPixelTextureVisualizer* Heatmap =
				World->SpawnActor<AHeatmapPixelTextureVisualizer>(SpawnOrigin, FRotator::ZeroRotator);
			if (!Heatmap)
			{
				Test.AddError(TEXT("failed to spawn the heatmap"));
				return true;
			}

			Heatmap->ActorName = TEXT("Heatmap_RealDatasetCapture");
			Heatmap->FloorID = 0;
			// Type 2, live tracking, 2D: no height displacement, so the readback is not confounded by the
			// WorldPositionOffset path that the red channel also feeds.
			Heatmap->InitializeHeatmap(2, true, FVector2D(SizeCm, SizeCm), 0.0f, false);
			Subsystem->AddHeatmapActor(Heatmap);
			Subsystem->SetTrajectoryHeatmapsEnabled(true);

			// The number that this whole exercise turns on. Brush radius, hits per crossing and therefore
			// every band edge follow from it, so it is logged whether or not anything fails.
			// Mirrors AHeatmapPixelTextureVisualizer::TrajectoryCircleRadius. Logged rather than asserted:
			// if the radius floors at 1 the route is 3 texels wide whatever the parameter says, and that
			// is worth seeing in the log before anyone fits a band edge to the resulting capture.
			constexpr float TrajectoryCircleRadiusCm = 10.0f;
			const float CmPerTexel = SizeCm / 1024.0f;
			const int32 BrushTexels = FMath::Max(1, FMath::RoundToInt(TrajectoryCircleRadiusCm / CmPerTexel));
			UE_LOG(LogTemp, Display,
				TEXT("[TrajectoryRealData] heatmap %.0f m -> %.3f cm/texel; %.0f cm radius -> %d texels, ")
				TEXT("route renders %.1f cm wide%s"),
				Metres, CmPerTexel, TrajectoryCircleRadiusCm, BrushTexels,
				(2 * BrushTexels + 1) * CmPerTexel,
				BrushTexels == 1 && CmPerTexel > TrajectoryCircleRadiusCm
					? TEXT(" (RADIUS FLOORED - texel size is in control here, not the parameter)")
					: TEXT(""));

			// Arm AFTER the heatmap exists: Arm() resets the playhead and clears the accumulation, so the
			// captured window lines up with the movement data with no time offset.
			// Explicit target. FloorID alone does not identify a heatmap here: the invariance tests leave
			// theirs in the world, also on FloorID 0, and the recorder captured that one instead - its
			// summary read "actor location X=0 Y=-5000, texture 500x500", i.e. a 50 m synthetic circle
			// where the technical school should have been.
			const FString Error = FTrajectoryCaptureRecorder::Get().Arm(World, /*FloorID*/ 0, CaptureSeconds,
				/*MaxRowsPerStream*/ 8'000'000, Heatmap);
			if (!Error.IsEmpty())
			{
				Test.AddError(FString::Printf(TEXT("could not arm the capture recorder: %s"), *Error));
			}
			return true;
		}

	private:
		FAutomationTestBase& Test;
	};

	/**
	 * Step the playhead forward in SampleInterval increments until the recorder disarms itself.
	 *
	 * Forward-only: a backward jump clears the trajectory buffer by design. The recorder ends its own
	 * window and writes the CSVs, so completion is observed rather than counted.
	 */
	class FPlayUntilCaptureCompleteCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPlayUntilCaptureCompleteCommand(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GetActiveGameWorld();
			if (!World)
			{
				Test.AddError(TEXT("game world disappeared during playback"));
				return true;
			}
			if (!FTrajectoryCaptureRecorder::Get().IsArmed())
			{
				UE_LOG(LogTemp, Display,
					TEXT("[TrajectoryRealData] capture finished after %d steps (%.1f sim seconds)"),
					Step, Step * SampleInterval);
				return true;
			}
			if (FramesToSettle > 0)
			{
				--FramesToSettle;
				return false;
			}

			UTimeDilationSubSystem* TimeDilation = World->GetSubsystem<UTimeDilationSubSystem>();
			if (!TimeDilation)
			{
				Test.AddError(TEXT("TimeDilation subsystem missing"));
				return true;
			}

			// Guard against driving forever if the recorder never disarms - it ticks off the heatmap
			// processor, so no agents processing means no heartbeat.
			if (Step * SampleInterval > CaptureSeconds * 2.0f)
			{
				Test.AddError(TEXT("played well past the capture window and the recorder never finished; ")
					TEXT("the heatmap processor is probably not running (no agents on floor 0?)"));
				FTrajectoryCaptureRecorder::Get().Abort();
				return true;
			}

			TimeDilation->OverrideCurrentTime(Step * SampleInterval, /*PreviouslyPaused*/ 0);
			++Step;
			FramesToSettle = 2;
			return false;
		}

	private:
		FAutomationTestBase& Test;
		int32 Step = 0;
		int32 FramesToSettle = 0;
	};

	/** Read the accumulation back, report it, and assert what each dataset is actually for. */
	class FAssertRealDataCommand : public IAutomationLatentCommand
	{
	public:
		FAssertRealDataCommand(FAutomationTestBase& InTest, EDataset InDataset)
			: Test(InTest), Dataset(InDataset) {}

		virtual bool Update() override
		{
			UWorld* World = GetActiveGameWorld();
			AHeatmapPixelTextureVisualizer* Heatmap = FindOwnHeatmap(World);
			if (!Heatmap)
			{
				Test.AddError(TEXT("no trajectory heatmap found at assertion time"));
				return true;
			}

			const UDynamicPixelRenderingTexture* Texture = Heatmap->GetTrajectoryAccumulationTextureForTesting();
			if (!Texture)
			{
				Test.AddError(TEXT("trajectory accumulation texture was never created"));
				return true;
			}

			FRealDataStats Stats = Measure(*Texture);

			// Canonical companions to the byte stats, read through the field itself rather than the
			// encoded display buffer -- GetTrajectoryFieldForTesting() (A4, confirmed available by A0
			// 2026-08-04). Kept best-effort: if the accessor or field is unavailable for any reason, the
			// byte-level measurement above still stands on its own and this file does not fail on that
			// account, it just logs/asserts less.
			// A0 (integration): the accessor returns a const reference, not a pointer. The field is a value
			// member and cannot be null, so "unavailable" means IsValid() is false - no grid sized yet.
			const FTrajectoryField& Field = Heatmap->GetTrajectoryFieldForTesting();
			if (Field.IsValid())
			{
				Stats.bCanonicalFieldAvailable = true;
				Stats.CanonicalTotalPersonMetres = Field.GetTotalPersonMetres();
				const TArray<float>& Metres = Field.GetCanonical(ETrajectoryMapMode::RouteUsage);
				float MaxCellMetres = 0.0f;
				for (const float V : Metres)
				{
					MaxCellMetres = FMath::Max(MaxCellMetres, V);
				}
				const float CellArea = Field.GetCellAreaSquareMetres();
				Stats.CanonicalPeakDensity = (CellArea > 0.0f) ? (MaxCellMetres / CellArea) : 0.0f;

				// The placement check that has teeth. TouchedTexels > 0 passes on a heatmap covering a
				// corner of the building; the dropped fraction does not. A grid parked on the wrong half
				// of Y reads 100% here, which is the state this file shipped in until 2026-08-04.
				const double Deposited = Field.GetTotalPersonMetres();
				const double Offered = Deposited + Field.GetDroppedPersonMetres()
					+ Field.GetRejectedPersonMetres() + Field.GetNegligiblePersonMetres();
				const double DroppedFraction = Offered > 0.0 ? (Field.GetDroppedPersonMetres() / Offered) : 0.0;

				// Where the grid actually is, versus where the agents actually are. Logged as texels so a
				// (-1,-1) names an off-grid corner directly instead of leaving it to be inferred.
				const FBox2D Bounds = ComputeAgentBoundsCm(World);
				const FIntPoint MinTexel = Heatmap->WorldToTexelForTesting(FVector(Bounds.Min.X, Bounds.Min.Y, 0.0));
				const FIntPoint MaxTexel = Heatmap->WorldToTexelForTesting(FVector(Bounds.Max.X, Bounds.Max.Y, 0.0));
				UE_LOG(LogTemp, Display,
					TEXT("[TrajectoryRealData] grid %dx%d at %.4f cm/texel, actor at (%.0f, %.0f, %.0f); ")
					TEXT("agent box min (%.0f, %.0f) -> texel (%d, %d), max (%.0f, %.0f) -> texel (%d, %d)"),
					Field.GetGridDims().X, Field.GetGridDims().Y, Field.GetEffectiveCmPerTexel(),
					Heatmap->GetActorLocation().X, Heatmap->GetActorLocation().Y, Heatmap->GetActorLocation().Z,
					Bounds.Min.X, Bounds.Min.Y, MinTexel.X, MinTexel.Y,
					Bounds.Max.X, Bounds.Max.Y, MaxTexel.X, MaxTexel.Y);
				UE_LOG(LogTemp, Display,
					TEXT("[TrajectoryRealData] %s canonical mass: %.1f of %.1f person-metres deposited, ")
					TEXT("%.1f%% dropped off-grid, %.1f rejected by a gate"),
					NameFor(Dataset), Deposited, Offered, DroppedFraction * 100.0,
					Field.GetRejectedPersonMetres());
				Test.TestTrue(
					TEXT("most of the offered path length landed on the grid (a high drop fraction means the ")
					TEXT("heatmap is mis-placed or undersized, not that the data is bad)"),
					DroppedFraction < 0.5);
			}

			// The measurement is the product. Reported unconditionally, pass or fail.
			UE_LOG(LogTemp, Display,
				TEXT("[TrajectoryRealData] %s: touched=%d peak=%d modal=%d saturated=%d (%.1f%%) edge=%d"),
				NameFor(Dataset), Stats.TouchedTexels, Stats.PeakByte, Stats.ModalByte,
				Stats.SaturatedTexels, Stats.SaturatedFraction() * 100.0f, Stats.EdgeTexels);
			if (Stats.bCanonicalFieldAvailable)
			{
				UE_LOG(LogTemp, Display,
					TEXT("[TrajectoryRealData] %s canonical: TotalPersonMetres=%.3f peakDensity=%.3f person/m"),
					NameFor(Dataset), Stats.CanonicalTotalPersonMetres, Stats.CanonicalPeakDensity);
			}
			else
			{
				UE_LOG(LogTemp, Display,
					TEXT("[TrajectoryRealData] %s: field has no grid (IsValid() false), byte-level stats only"),
					NameFor(Dataset));
			}
			// A0: actually WRITE the canonical export, rather than telling the reader to go and find one.
			//
			// The recorder capture under Saved/TrajectoryCapture/ is NOT what the analysis scripts consume -
			// they read the canonical <base>.csv + <base>.meta.json pair, and until now nothing in an
			// automated run produced it. SaveHeatmapToPNG() was reachable only through
			// UHeatmapSubsystem::SaveSelectedHeatmapsToPNG, i.e. off a UI button, so Saved/Heatmap/ did not
			// exist on any machine and the band refit had no input. mobius.Heatmap.DumpTrajectoryCsv is a
			// different, older artefact: texel BYTES from the accumulation texture, no sidecar.
			//
			// This is the run that has the real dataset loaded and the field populated, so this is where the
			// export belongs. Cheap (one pass over occupied cells) and it makes the capture self-sufficient.
			if (Heatmap)
			{
				Heatmap->SaveHeatmapToPNG();
				UE_LOG(LogTemp, Display,
					TEXT("[TrajectoryRealData] %s: canonical export written to Saved/Heatmap/ ")
					TEXT("(<name>_Trajectory_<stamp>.csv + .meta.json + .png). Run ")
					TEXT("MobiusPerf\\analysis\\field_stats.py then band_fit.py against that PAIR - not ")
					TEXT("against the Saved/TrajectoryCapture/ recorder output - to refit the band edges."),
					NameFor(Dataset));
			}

			Test.TestTrue(TEXT("the dataset put something on the heatmap"), Stats.TouchedTexels > 0);

		// A0: the gate that used to sit here asserted BelowFirstBandTexels == 0, i.e. that no touched texel
		// encodes below 24.5/255 -- and 24.5 is literally the OLD seed byte. That invariant held only
		// because the removed rasteriser stamped every touched texel to byte 25 before incrementing, which
		// is exactly what made the first band meaningless; TEST_PLAN section 7 lists those byte-edge fits
		// among the coverage that dies with the brush. Under a no-seed encode a genuinely faint route
		// legitimately lands in 1..24, so the assertion tested a property this design does not promise, and
		// it failed on all three datasets (1706 / 6997 / 1670 texels) for the right reason.
		//
		// The invariant that IS promised -- byte 0 means no-data, exclusively -- is now asserted exactly, in
		// Tier A, against the field itself: Lifecycle.EncodeReservesZeroForNoData. What remains here is the
		// band-fit MEASUREMENT it was always really producing, reported rather than gated, because the band
		// edges are provisional under D9 and are refitted by band_fit.py.

			// Structural, and true of any dataset: LOS_A is reserved for "no data", so nothing that was
			// actually walked on may land in it. A hit here means the seed and the band edge have drifted
			// apart, which is the original banding bug returning.
			UE_LOG(LogTemp, Display,
			TEXT("[TrajectoryRealData] %s band-fit input: %d of %d touched texels (%.1f%%) fall below the ")
			TEXT("provisional first band edge - feed this to MobiusPerf\\analysis\\band_fit.py, it is the ")
			TEXT("signal that the edges need refitting, NOT a failure"),
			NameFor(Dataset), Stats.BelowFirstBandTexels, Stats.TouchedTexels,
			Stats.TouchedTexels > 0 ? (100.0f * Stats.BelowFirstBandTexels / Stats.TouchedTexels) : 0.0f);

			ReportEdgePileup(Stats);

			switch (Dataset)
			{
			case EDataset::Baseline1000:
				// The usable case: most of the map should still have headroom. If a 1000-agent run is
				// already clipping heavily then the accumulator ceiling, not the band edges, is what needs
				// attention first. (Canonical units: TotalPersonMetres/peak density are logged above for
				// whoever refits the bands; no threshold is asserted on them here since this is a REAL
				// dataset with no oracle-derived expected value -- see T5_TEST_REWRITE.md's own rule
				// against inventing one.)
				Test.TestTrue(*FString::Printf(
						TEXT("a 1000-agent run keeps most texels below saturation (%.1f%% clipped)"),
						Stats.SaturatedFraction() * 100.0f),
					Stats.SaturatedFraction() < 0.25f);
				break;

			case EDataset::Oversaturation5000:
				// UPDATED FOR THE REBUILD (see file header). The finding is no longer "clips hard and
				// renders largely red" -- that was the OLD fixed-scale uint8 buffer. Two things are now
				// asserted instead:
				//   (1) something clearly got hot (peak byte > 100), same regression guard as before --
				//       the test must still fail if the run silently did nothing;
				//   (2) presentation "saturates gracefully": auto-exposure means only the TRUE peak cell(s)
				//       should pin at byte 255, not a large swath of the map, so SaturatedFraction should
				//       stay small. This is the observable, byte-level half of "no hard clip".
				// The canonical-field half ("the raw value keeps rising, unbounded") is Tier A's job --
				// TrajectoryHeatmapCalibrationTest.cpp's T_SAT_1 proves it directly against a synthetic
				// overload with a known repeat count. This test can only log the real canonical total
				// (above) as a cross-check that the accessor path agrees there IS a large, non-clamped
				// number behind the encode, not re-derive the unbounded-growth proof from a real dataset
				// whose true expected total is not known in advance.
				Test.TestTrue(*FString::Printf(
						TEXT("a 5000-agent run drives the accumulator well past a single pass (peak %d)"),
						Stats.PeakByte),
					Stats.PeakByte > 100);
				Test.TestTrue(*FString::Printf(
						TEXT("presentation saturates gracefully: only a few cells pin at 255 (%.1f%% clipped, ceiling %.0f%%)"),
						Stats.SaturatedFraction() * 100.0f, OversaturationMaxSaturatedFraction * 100.0f),
					Stats.SaturatedFraction() < OversaturationMaxSaturatedFraction);
				if (Stats.bCanonicalFieldAvailable)
				{
					Test.TestTrue(TEXT("canonical total is a large, finite, non-zero number behind the encode"),
						Stats.CanonicalTotalPersonMetres > 0.0 && FMath::IsFinite(Stats.CanonicalTotalPersonMetres));
				}
				UE_LOG(LogTemp, Display,
					TEXT("[TrajectoryRealData] OVERSATURATION: %.1f%% of touched texels are clipped at 255 ")
					TEXT("(ceiling %.0f%% -- above that means auto-exposure regressed toward the old hard-clip ")
					TEXT("behaviour). Counts above 255 are still lost to the 8-bit display encode, so any ")
					TEXT("per-crossing reading there is a floor on the DISPLAY, not on the canonical field."),
					Stats.SaturatedFraction() * 100.0f, OversaturationMaxSaturatedFraction * 100.0f);
				break;
			}
			return true;
		}

	private:
		/**
		 * Positions outside the mesh are clamped onto the texture edge by the rasteriser, so an undersized
		 * heatmap does not error - it quietly stacks the entire outside world onto the border. Anything
		 * beyond a sliver there means the capture is squashed and the numbers should not be trusted.
		 */
		void ReportEdgePileup(const FRealDataStats& Stats) const
		{
			if (Stats.TouchedTexels <= 0)
			{
				return;
			}
			const float EdgeFraction = static_cast<float>(Stats.EdgeTexels) / Stats.TouchedTexels;
			if (EdgeFraction > 0.01f)
			{
				Test.AddWarning(FString::Printf(
					TEXT("%.1f%% of touched texels sit on the texture border - the heatmap is probably ")
					TEXT("smaller than the building and out-of-range agents are being clamped onto the edge. ")
					TEXT("Re-run with -MobiusHeatmapMetres=<larger> before using this capture to fit bands."),
					EdgeFraction * 100.0f));
			}
		}

		FAutomationTestBase& Test;
		EDataset Dataset;
	};

	/**
	 * Point the view straight down at the heatmap and screenshot it, so the in-level render can be put
	 * beside the CPU-colourised PNG the recorder writes.
	 *
	 * EXPECT THE TWO TO DIFFER, and know where. The PNG is per-texel exact. An earlier claim that the render
	 * samples through a shared world-group sampler was WRONG and is retracted:
	 * `Material.SamplesWithTextureOwnSampler` reads the deserialised property and passes, so every sample
	 * honours the texture's own sampler. Since 2026-08-05 that sampler is TF_BILINEAR by owner ruling, so
	 * the render interpolates between texel centres DELIBERATELY and boundary pixels are supposed to differ
	 * from the export. Band membership still survives, which is why bilinear was safe: the material bands
	 * the scalar after sampling, so an interpolated value lands in a band, never between two colours.
	 *
	 * ⚠️ THIS IS A SMOKE TEST, NOT A DEFECT DETECTOR. READ THIS BEFORE ACTING ON ITS OUTPUT.
	 *
	 * The capture reproduces roughly ONE TEXEL IN TEN of the field (ruling A0-70). The export draws
	 * continuous lines; this capture shows scattered dots sitting on them. **That is a property of the
	 * capture, not of the app** -- the owner confirmed on 2026-08-05 that the editor viewport renders
	 * continuous coloured lines matching the export, which is what closed AC12. So a "missing" band or a
	 * broken-looking stroke in this PNG is almost certainly this harness, and anyone who treats it as a
	 * heatmap bug will chase something that is not there.
	 *
	 * What it is still good for: proving the capture path runs, that the camera frames the heatmap, and
	 * that registration is exact -- the heatmap square was located by searching offsets and landed on the
	 * predicted origin with 92x contrast between export-occupied cells and the rest (A0-66).
	 *
	 * What is known about the loss, so nobody re-derives it:
	 *   - The field is 2000x2000 texels rendered into an ~810x810 square: 6.1 texels per pixel, sampled
	 *     NEAREST with AA off, so a 1-texel stroke survives in about 1 pixel in 6. Measured on the export
	 *     alone that costs 44% -- REAL, but under half the loss (A0-70).
	 *   - The export is raw linear x255 with no sRGB encode (COLOR_TO_BYTE) while the render is exposed
	 *     and tone-curved, so exact RGB equality is impossible BY CONSTRUCTION (A0-68). Do not re-open it.
	 *   - The heatmap surface has no detectable edge at its own boundary, so band A renders as nothing
	 *     rather than as a crushed blue (A0-69). Unexplained, and deliberately not pursued.
	 * Three suspects were measured and refuted: the tonemapper, AEM_Manual, and undersampling as
	 * sufficient cause. Do not re-run those experiments; the numbers are in STATUS.md A0-66 to A0-70.
	 *
	 * If a real pixel comparison is ever wanted, do NOT extend this: use USceneCaptureComponent2D with
	 * SCS_SceneColorHDR into a render target sized exactly to the field dims and camera aspect 1.0. That
	 * makes texel-to-pixel 1:1, which removes the undersampling by construction, and skips exposure and
	 * the tone curve entirely. Compare against a LINEAR reference, not the sRGB PNG.
	 *
	 * Compare by BAND ASSIGNMENT over the occupied region, never by pixel equality: a soft edge is not a
	 * finding, a wrong band is. Never judge on exact palette matches -- A0-43 retired them as a criterion
	 * and A0-68 showed they are unachievable anyway.
	 */
	class FCaptureRenderedViewCommand : public IAutomationLatentCommand
	{
	public:
		FCaptureRenderedViewCommand(FAutomationTestBase& InTest, EDataset InDataset)
			: Test(InTest), Dataset(InDataset) {}

		virtual bool Update() override
		{
			UWorld* World = GetActiveGameWorld();
			if (!World)
			{
				Test.AddError(TEXT("game world disappeared before the rendered capture"));
				return true;
			}

			if (!bRequested)
			{
				if (!FrameTheHeatmap(World))
				{
					return true;
				}
				if (!bSuppressed)
				{
					SuppressPostProcessing(World->GetFirstPlayerController(), Camera);
					bSuppressed = true;
					FramesToSettle = 4;
					return false;
				}
				// Show-flag console changes land on a LATER frame, so settle before grabbing the buffer or
				// the capture records the state from before they applied.
				if (FramesToSettle-- > 0)
				{
					return false;
				}

				OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TrajectoryCapture"),
					FString::Printf(TEXT("rendered_%s.png"), NameFor(Dataset)));
				IFileManager::Get().Delete(*OutputPath, false, true);
				FScreenshotRequest::RequestScreenshot(OutputPath, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
				bRequested = true;
				LastSize = -1;
				Deadline = FPlatformTime::Seconds() + 30.0;
				return false;
			}

			if (FPaths::FileExists(OutputPath))
			{
				// A screenshot appears on disk while it is still being written, so require the size to hold
				// steady across two ticks before declaring it landed. A half-written PNG read by the
				// analysis scripts is a silent data error, not a visible failure.
				const int64 Size = IFileManager::Get().FileSize(*OutputPath);
				if (Size <= 0 || Size != LastSize)
				{
					LastSize = Size;
					return false;
				}

				UE_LOG(LogTemp, Display, TEXT("[TrajectoryRealData] rendered view written to %s (%lld bytes)"),
					*OutputPath, Size);
				UE_LOG(LogTemp, Display,
					TEXT("[TrajectoryRealData] SMOKE TEST ONLY - this capture reproduces about one texel in ")
					TEXT("ten (A0-70). It shows dots where the export shows lines, and that is the harness, ")
					TEXT("NOT the app. Do not raise a heatmap defect from it. Compare band assignment with ")
					TEXT("_CurrentHandoff/trajectoryFix/analysis/ab_compare.py if you need the numbers."));
				return true;
			}
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test.AddError(TEXT("the rendered screenshot was requested but never landed on disk"));
				return true;
			}
			return false;
		}

	private:
		/** Put a camera above the heatmap centre looking straight down, and make it the view target. */
		bool FrameTheHeatmap(UWorld* World)
		{
			if (Camera)
			{
				return true;
			}

			AHeatmapPixelTextureVisualizer* Heatmap = FindOwnHeatmap(World);
			if (!Heatmap)
			{
				Test.AddError(TEXT("no trajectory heatmap to frame for the rendered capture"));
				return false;
			}

			APlayerController* Controller = World->GetFirstPlayerController();
			if (!Controller)
			{
				Test.AddError(TEXT("no player controller - a rendered capture needs a view target"));
				return false;
			}

			// The mesh origin is its bottom-left corner, so the centre is half a span along both axes.
			const float SizeCm = HeatmapMetres() * 100.0f;
			const FVector Origin = Heatmap->GetActorLocation();
			const FVector Centre(Origin.X + SizeCm * 0.5f, Origin.Y + SizeCm * 0.5f, Origin.Z);
			// Height is not critical under an orthographic projection (below) - it only has to clear the
			// geometry. Kept at the old framing distance so the view is unchanged in every other respect.
			const FVector Eye(Centre.X, Centre.Y, Centre.Z + SizeCm * 0.75f);

			// YAW MUST BE -90, NOT 0. The capture has to align to WORLD XY, because that is what the
			// exported PNG is: px_x = cell_x = world X, and px_y = cell_y = world Y increasing DOWN the
			// image. With pitch -90 (straight down), UE gives:
			//     yaw   0  ->  camera right = world +Y, camera up = world +X   (transposed 90 deg)
			//     yaw -90  ->  camera right = world +X, camera up = world -Y   (matches the PNG)
			// The old yaw of 0 therefore rendered the heatmap rotated a quarter turn against its own
			// export, which the owner spotted by eye on 2026-08-05. It is NOT a data defect: the field's
			// orientation was measured correct against the building's world extents on both axes
			// independently (ruling A0-51). A non-square site makes a 90 deg camera yaw and a transposed
			// buffer look identical, so verify this by projection, never by eye.
			Camera = World->SpawnActor<ACameraActor>(Eye, FRotator(-90.0f, -90.0f, 0.0f));
			if (!Camera)
			{
				Test.AddError(TEXT("failed to spawn the capture camera"));
				return false;
			}

			// ORTHOGRAPHIC, not perspective. Under perspective the world->screen scale falls off with
			// distance from the view axis, so a texel near the edge covers fewer pixels than one at the
			// centre and no pixel comparison against a uniform-scale export can ever be exact. Ortho makes
			// world XY map linearly onto screen XY, which is the whole point of an overhead capture.
			if (UCameraComponent* CameraComponent = Camera->GetCameraComponent())
			{
				CameraComponent->SetProjectionMode(ECameraProjectionMode::Orthographic);
				// The heatmap span is square and the capture frame is wider than it is tall, so the
				// VERTICAL axis is the binding one: OrthoWidth is a width, so scale it up by the aspect
				// ratio or the top and bottom of the span are cropped.
				const float AspectRatio = CameraComponent->AspectRatio > 0.0f ? CameraComponent->AspectRatio : (4.0f / 3.0f);
				CameraComponent->SetOrthoWidth(SizeCm * AspectRatio);
			}

			Controller->SetViewTargetWithBlend(Camera, 0.0f);

			// Clear the A/B artefacts. The A0-68 run wrote one PNG per variant; those configurations no
			// longer exist, so leaving them beside this capture would invite a stale file being read as
			// fresh output. The originals are preserved in _CurrentHandoff/trajectoryFix/analysis/evidence/.
			static const TCHAR* RetiredVariants[] = {
				TEXT("A_notonemap_pinnedexposure"),
				TEXT("B_tonemap_pinnedexposure"),
				TEXT("C_tonemap_freeexposure"),
			};
			for (const TCHAR* Retired : RetiredVariants)
			{
				IFileManager::Get().Delete(*FPaths::Combine(FPaths::ProjectSavedDir(),
					TEXT("TrajectoryCapture"),
					FString::Printf(TEXT("rendered_%s_%s.png"), NameFor(Dataset), Retired)), false, true);
			}
			return true;
		}

		/**
		 * Quiet the stages that blur or recolour the capture, WITHOUT changing the pipeline the owner
		 * actually views the app through. This is the A/B's winning configuration (variant C, A0-68):
		 * tonemapper left ON, no exposure override.
		 *
		 * Two levers were tried here and both are deliberately absent, so nobody re-adds them:
		 *   - `ShowFlag.Tonemapper 0` -- MEASURED WORSE. Turning the tonemapper off made the image DARKER
		 *     (background #000102 -> #090B10 is the other direction; off was the #090B10 one) and HALVED
		 *     the rank correlation against the export, +0.245 -> +0.119. The filmic toe was not the
		 *     problem. It also renders through a pipeline no user sees, which makes the capture answer a
		 *     question nobody asked.
		 *   - `AEM_Manual` exposure pin -- MEASURED IRRELEVANT. With it and without it the capture differed
		 *     by ONE distinct colour and 0.0016 of correlation. `ShowFlag.EyeAdaptation 0` below already
		 *     fixes exposure, so the override was dead weight. It is also not the "pin to 1.0" it was
		 *     written as: manual mode applies photographic exposure from shutter/ISO/aperture, ~1/500 at
		 *     defaults.
		 *
		 * What stays off, and why none of it is contentious: temporal AA blends neighbouring texels
		 * together, and fog, bloom, colour grading, vignette, motion blur and DoF are all transforms
		 * applied after the value being read. None of them can darken a surface.
		 *
		 * This is capture-view only. Nothing here changes the material, the asset, or what a user sees.
		 */
		static void SuppressPostProcessing(APlayerController* Controller, ACameraActor* Camera)
		{
			if (!Controller)
			{
				return;
			}

			// Show flags and AA are per-view and only reachable by console in a -game client.
			static const TCHAR* Commands[] = {
				TEXT("r.AntiAliasingMethod 0"),   // no TSR/TAA: it blends neighbouring texels together
				TEXT("ShowFlag.Fog 0"),
				TEXT("ShowFlag.AtmosphericFog 0"),
				TEXT("ShowFlag.VolumetricFog 0"),
				TEXT("ShowFlag.Bloom 0"),
				TEXT("ShowFlag.EyeAdaptation 0"), // no auto-exposure: it rescales by scene content
				TEXT("ShowFlag.ColorGrading 0"),
				TEXT("ShowFlag.Vignette 0"),
				TEXT("ShowFlag.MotionBlur 0"),
				TEXT("ShowFlag.DepthOfField 0"),
				TEXT("ShowFlag.ScreenPercentage 0"),
			};
			for (const TCHAR* Command : Commands)
			{
				Controller->ConsoleCommand(Command, /*bWriteToLog*/ false);
			}

			// Bloom and vignette are pinned off on the camera as well, so the result does not depend on
			// whether a given engine version honours the show flags on an offscreen capture.
			if (UCameraComponent* CameraComponent = Camera ? Camera->GetCameraComponent() : nullptr)
			{
				FPostProcessSettings& Post = CameraComponent->PostProcessSettings;
				Post.bOverride_BloomIntensity = true;
				Post.BloomIntensity = 0.0f;
				Post.bOverride_VignetteIntensity = true;
				Post.VignetteIntensity = 0.0f;
			}
		}

		FAutomationTestBase& Test;
		EDataset Dataset;
		ACameraActor* Camera = nullptr;
		FString OutputPath;
		int32 FramesToSettle = 4;
		int64 LastSize = -1;
		bool bSuppressed = false;
		bool bRequested = false;
		double Deadline = 0.0;
	};

	/** Shared body: locate the dataset, import it, spawn, capture, measure. */
	static bool RunDataset(FAutomationTestBase& Test, EDataset Dataset, bool bCaptureRenderedView = false)
	{
		const FString DatasetPath = ResolveDatasetPath(Dataset);
		if (DatasetPath.IsEmpty())
		{
			// Skip, do not fail. The dataset is ~760 MB and excluded from git on purpose, so its absence
			// is the normal state on most machines and must not red-light the suite. A SKIP IS NOT A PASS
			// though (RunTests.ps1 keys off $t.state, and AddWarning alone does not fail a test) -- the
			// literal marker below is maximally distinctive so A0 (or anyone grepping the run log) can
			// tell a skip from a real green run at a glance, since this file cannot change that pass/fail
			// semantics itself (RunTests.ps1 is A0's file, not this agent's).
			Test.AddWarning(FString::Printf(
				TEXT("SKIPPED-NOT-A-PASS %s: %s not found. Expected under ")
				TEXT("<Project>/UnitTestSampleData/TechSchoolTest/ (gitignored, ~760 MB) or via ")
				TEXT("-MobiusTechSchoolDir=<dir>. This test reports GREEN below, but it verified NOTHING -- ")
				TEXT("confirm the dataset was actually present before trusting a passing run of this file."),
				NameFor(Dataset), FileNameFor(Dataset)));
			return true;
		}

		UWorld* World = GetActiveGameWorld();
		if (!World)
		{
			Test.AddError(TEXT("no game world - run via RunTests.ps1 -InGame (UnrealEditor-Cmd -game)"));
			return false;
		}

		UProjectMobiusGameInstance* GameInstance = Cast<UProjectMobiusGameInstance>(World->GetGameInstance());
		Test.TestNotNull(TEXT("Mobius game instance"), GameInstance);
		if (!GameInstance)
		{
			return false;
		}

		UE_LOG(LogTemp, Display, TEXT("[TrajectoryRealData] %s <- %s"), NameFor(Dataset), *DatasetPath);

		// Deliberately NOT clearing the sim cache here, unlike the synthetic Tier B scenarios. Those
		// rewrite their fixture every run so a stale cache would be wrong; these read a fixed file that
		// takes minutes to import cold, and the cache is keyed on a content hash.
		GameInstance->SetPedestrianDataFilePath(DatasetPath);

		// 10 minutes: the 5000-agent file is 325 MB of JSON on a cold cache.
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForRealDataLoadedCommand(Test, 600.0));
		ADD_LATENT_AUTOMATION_COMMAND(FSpawnAndArmCommand(Test));
		ADD_LATENT_AUTOMATION_COMMAND(FPlayUntilCaptureCompleteCommand(Test));
		ADD_LATENT_AUTOMATION_COMMAND(FAssertRealDataCommand(Test, Dataset));
		if (bCaptureRenderedView)
		{
			// Last, so the accumulation has everything the CPU-side PNG has.
			ADD_LATENT_AUTOMATION_COMMAND(FCaptureRenderedViewCommand(Test, Dataset));
		}
		return true;
	}
}

// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryRealDataBaseline1000Test,
	"Mobius.InGame.TrajectoryRealData.Baseline1000",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryRealDataBaseline1000Test::RunTest(const FString& Parameters)
{
	return TrajectoryRealData::RunDataset(*this, TrajectoryRealData::EDataset::Baseline1000);
}

// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryRealDataOversaturation5000Test,
	"Mobius.InGame.TrajectoryRealData.Oversaturation5000",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryRealDataOversaturation5000Test::RunTest(const FString& Parameters)
{
	return TrajectoryRealData::RunDataset(*this, TrajectoryRealData::EDataset::Oversaturation5000);
}

// -------------------------------------------------------------------------------------------------
// The comparison run: same pipeline, plus a screenshot of the heatmap as the level actually renders it,
// so the GPU-sampled surface can be put beside the CPU-colourised PNG.
//
// Needs a real RHI, so it SKIPS under -nullrhi (which RunTests.ps1 passes by default). Use:
//     MobiusPerf\RunTests.ps1 -InGame -Rendered
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryRealDataRenderedComparisonTest,
	"Mobius.InGame.TrajectoryRealData.RenderedComparison",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryRealDataRenderedComparisonTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		// Skip rather than fail: the default harness is headless on purpose, and a rendered comparison
		// is a diagnostic a human looks at, not something that should gate a headless suite.
		AddWarning(TEXT("SKIPPED-NOT-A-PASS RenderedComparison: no RHI (running under -nullrhi). ")
			TEXT("Run MobiusPerf\\RunTests.ps1 -InGame -Rendered to produce the rendered view."));
		return true;
	}
	return TrajectoryRealData::RunDataset(*this, TrajectoryRealData::EDataset::Baseline1000,
		/*bCaptureRenderedView*/ true);
}

#endif // !UE_BUILD_SHIPPING
