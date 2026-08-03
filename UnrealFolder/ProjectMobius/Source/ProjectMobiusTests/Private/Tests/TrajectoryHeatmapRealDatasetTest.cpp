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
//   Oversaturation5000  TechnicalSchool5000DefaultExits.json - deliberately past the accumulator's
//                                                             ceiling. Expected to clip hard and render
//                                                             largely red; that IS the finding, and it
//                                                             bounds how much dynamic range is left.
//
// Each run arms FTrajectoryCaptureRecorder, so the artifacts are the full capture set under
// Saved/TrajectoryCapture/<stamp>/ -- raster.csv, texture.csv, accumulation_los.png and
// accumulation_scaled.png. Feed those to MobiusPerf\analysis\hits_per_crossing.py and
// crossings_vs_byte.py; both self-configure from the capture, including the brush radius.
//
// THE DATASET IS NOT IN GIT. .gitignore:162 excludes
// UnrealFolder/ProjectMobius/UnitTestSampleData/TechSchoolTest/ deliberately -- it is ~760 MB. These
// tests therefore SKIP (pass with a warning) when it is absent rather than failing, so the suite stays
// green on a machine that does not have it. Point them somewhere else with:
//
//     -MobiusTechSchoolDir="D:\...\Packaged\Development\Windows\ProjectMobius\UnitTestSampleData\TechSchoolTest"
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
#include "Subsystems/TimeDilationSubSystem.h"
#include "Actors/HeatmapPixelTextureVisualizer.h"
#include "Subsystems/HeatmapSubsystem.h"
#include "Diagnostics/TrajectoryCaptureRecorder.h"
#include "DynamicPixelRenderingTexture.h"

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
	 * Not derived from the building: these runs import agent movement only, and UHeatmapSubsystem sizes
	 * its heatmaps from HeatmapBoundingSize, which is populated by a building import. Rather than pull
	 * a 27 MB fbx through the runtime mesh path just to get a number, the size is passed in (exactly as
	 * mobius.Heatmap.SpawnTrajectory does) and the run self-checks: positions outside the mesh are
	 * CLAMPED onto the texture edge by the rasteriser, so a pile-up on the border means undersized.
	 * ReportEdgePileup below turns that into a warning instead of a silently squashed capture.
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

	/** Summary of one accumulation buffer. Mirrors the Tier B struct so the two runs read alike. */
	struct FRealDataStats
	{
		int32 TouchedTexels = 0;
		int32 PeakByte = 0;
		int32 ModalByte = 0;
		int32 SaturatedTexels = 0;   // byte == 255, i.e. counts already lost
		int32 EdgeTexels = 0;        // touched texels on the outermost row/column
		int32 NoDataCollisions = 0;  // touched texels that still colourise as "no data"

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
					++Stats.NoDataCollisions;
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

			AHeatmapPixelTextureVisualizer* Heatmap =
				World->SpawnActor<AHeatmapPixelTextureVisualizer>(FVector::ZeroVector, FRotator::ZeroRotator);
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
			const FString Error = FTrajectoryCaptureRecorder::Get().Arm(World, /*FloorID*/ 0, CaptureSeconds,
				/*MaxRowsPerStream*/ 8'000'000);
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
			AHeatmapPixelTextureVisualizer* Heatmap = nullptr;
			if (World)
			{
				for (TActorIterator<AHeatmapPixelTextureVisualizer> It(World); It; ++It)
				{
					if (IsValid(*It) && It->bTrajectoryHeatmap)
					{
						Heatmap = *It;
						break;
					}
				}
			}
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

			const FRealDataStats Stats = Measure(*Texture);

			// The measurement is the product. Reported unconditionally, pass or fail.
			UE_LOG(LogTemp, Display,
				TEXT("[TrajectoryRealData] %s: touched=%d peak=%d modal=%d saturated=%d (%.1f%%) edge=%d"),
				NameFor(Dataset), Stats.TouchedTexels, Stats.PeakByte, Stats.ModalByte,
				Stats.SaturatedTexels, Stats.SaturatedFraction() * 100.0f, Stats.EdgeTexels);
			UE_LOG(LogTemp, Display,
				TEXT("[TrajectoryRealData] capture written under Saved/TrajectoryCapture/ - run ")
				TEXT("MobiusPerf\\analysis\\crossings_vs_byte.py against it to refit the band edges"));

			Test.TestTrue(TEXT("the dataset put something on the heatmap"), Stats.TouchedTexels > 0);

			// Structural, and true of any dataset: LOS_A is reserved for "no data", so nothing that was
			// actually walked on may land in it. A hit here means the seed and the band edge have drifted
			// apart, which is the original banding bug returning.
			Test.TestEqual(TEXT("no touched texel colourises as no-data"), Stats.NoDataCollisions, 0);

			ReportEdgePileup(Stats);

			switch (Dataset)
			{
			case EDataset::Baseline1000:
				// The usable case: most of the map should still have headroom. If a 1000-agent run is
				// already clipping heavily then the accumulator ceiling, not the band edges, is what needs
				// attention first.
				Test.TestTrue(*FString::Printf(
						TEXT("a 1000-agent run keeps most texels below saturation (%.1f%% clipped)"),
						Stats.SaturatedFraction() * 100.0f),
					Stats.SaturatedFraction() < 0.25f);
				break;

			case EDataset::Oversaturation5000:
				// Expected to clip. Asserting only that it produced a hotter map than a bare seed, so the
				// test still fails if the run silently did nothing. The clipped fraction is the number to
				// read, not a threshold to pass.
				Test.TestTrue(*FString::Printf(
						TEXT("a 5000-agent run drives the accumulator well past a single pass (peak %d)"),
						Stats.PeakByte),
					Stats.PeakByte > 100);
				UE_LOG(LogTemp, Display,
					TEXT("[TrajectoryRealData] OVERSATURATION: %.1f%% of touched texels are clipped at 255. ")
					TEXT("Counts above that are lost, so any per-crossing reading in those areas is a floor, ")
					TEXT("not a measurement."),
					Stats.SaturatedFraction() * 100.0f);
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
	 * EXPECT THE TWO TO DIFFER, and know where. The PNG is per-texel exact. The render samples the same
	 * buffer through a shared world-group sampler, which IGNORES the texture's TF_Nearest, so the GPU
	 * bilinear-filters it before the material bands it. They should agree on which band a texel lands in
	 * and disagree along band boundaries, where interpolation smears one into the next. A difference
	 * anywhere else is a real finding; a soft edge is not.
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
				// A frame or two for the view target switch to take effect before grabbing the buffer.
				if (FramesToSettle-- > 0)
				{
					return false;
				}

				OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TrajectoryCapture"),
					FString::Printf(TEXT("rendered_%s.png"), NameFor(Dataset)));
				IFileManager::Get().Delete(*OutputPath, false, true);
				FScreenshotRequest::RequestScreenshot(OutputPath, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
				bRequested = true;
				Deadline = FPlatformTime::Seconds() + 30.0;
				return false;
			}

			if (FPaths::FileExists(OutputPath))
			{
				UE_LOG(LogTemp, Display, TEXT("[TrajectoryRealData] rendered view written to %s"), *OutputPath);
				UE_LOG(LogTemp, Display,
					TEXT("[TrajectoryRealData] compare against accumulation_los.png in the newest ")
					TEXT("Saved/TrajectoryCapture/<stamp>/ - they should agree on band assignment and ")
					TEXT("differ only along band edges (the render bilinear-filters, the PNG does not)"));
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

			AHeatmapPixelTextureVisualizer* Heatmap = nullptr;
			for (TActorIterator<AHeatmapPixelTextureVisualizer> It(World); It; ++It)
			{
				if (IsValid(*It) && It->bTrajectoryHeatmap)
				{
					Heatmap = *It;
					break;
				}
			}
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
			// At the default 90 degree FOV a height of half the span exactly frames it; add margin so the
			// edges are not clipped by aspect ratio.
			const FVector Eye(Centre.X, Centre.Y, Centre.Z + SizeCm * 0.75f);

			Camera = World->SpawnActor<ACameraActor>(Eye, FRotator(-90.0f, 0.0f, 0.0f));
			if (!Camera)
			{
				Test.AddError(TEXT("failed to spawn the capture camera"));
				return false;
			}
			Controller->SetViewTargetWithBlend(Camera, 0.0f);
			return true;
		}

		FAutomationTestBase& Test;
		EDataset Dataset;
		ACameraActor* Camera = nullptr;
		FString OutputPath;
		int32 FramesToSettle = 3;
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
			// is the normal state on most machines and must not red-light the suite.
			Test.AddWarning(FString::Printf(
				TEXT("SKIPPED %s: %s not found. Expected under <Project>/UnitTestSampleData/TechSchoolTest/ ")
				TEXT("(gitignored, ~760 MB) or via -MobiusTechSchoolDir=<dir>."),
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
		AddWarning(TEXT("SKIPPED RenderedComparison: no RHI (running under -nullrhi). ")
			TEXT("Run MobiusPerf\\RunTests.ps1 -InGame -Rendered to produce the rendered view."));
		return true;
	}
	return TrajectoryRealData::RunDataset(*this, TrajectoryRealData::EDataset::Baseline1000,
		/*bCaptureRenderedView*/ true);
}

#endif // !UE_BUILD_SHIPPING
