// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// TrajectoryHeatmapInGameTest.cpp
//
// Tier B of the trajectory calibration suite: the three scenarios from the handoff document driven
// through the REAL pipeline — JSON import -> MASS spawn -> AgentHeatmapProcessor's 0.1 s simulation-time
// gate -> segment submission -> rasteriser — then measured by reading raw accumulation bytes back.
//
//   1. SingleRoute        one agent crossing once            -> must stay light blue (LOS_A)
//   2. SustainedRoute     one agent barely moving            -> dwell concentrates into a small hot patch
//   3. OverlappingRoutes  five agents on an IDENTICAL path   -> five times the per-texel hits
//
// The five agents in scenario 3 share byte-for-byte identical position sequences, differing only in
// entity id. That is deliberate: two independently generated routes would rasterise to neighbouring
// texel lines and accumulate +1 or +2 instead of a clean +3 per pass, which would look like a broken
// model rather than a coordinate coincidence.
//
// Assertions are on the MODAL byte across touched texels, not the peak. Consecutive segments share an
// endpoint, so texels at segment joins legitimately collect extra hits; the mode describes the ordinary
// interior texel, which is what the calibration is actually about.
//
// No building geometry, no WKT, no .msc needed — the heatmap is spawned with an explicit world size
// because InitializeHeatmap generates its own procedural mesh.
//
// Run: MobiusPerf\RunTests.ps1 -InGame     (or -ExecCmds="Automation RunTests Mobius.InGame.TrajectoryHeatmap")
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
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
#include "DynamicPixelRenderingTexture.h"

namespace TrajectoryInGame
{
	/** Sampling interval, matching UpdateHeatmapInterval's 0.1 s simulation-time gate exactly. */
	static constexpr float SampleInterval = 0.1f;
	static constexpr int32 Timesteps = 100;              // 10 s of playback
	static constexpr float DurationSeconds = Timesteps * SampleInterval;

	/** Heatmap span in metres. 50 m over 1024 texels gives ~4.88 cm per texel. */
	static constexpr float HeatmapMetres = 50.0f;

	/** Agents in the overlapping-routes scenario. */
	static constexpr int32 OverlapAgentCount = 5;

	enum class EScenario : uint8
	{
		SingleRoute,
		SustainedRoute,
		OverlappingRoutes
	};

	static int32 AgentCountFor(EScenario Scenario)
	{
		return Scenario == EScenario::OverlappingRoutes ? OverlapAgentCount : 1;
	}

	static const TCHAR* NameFor(EScenario Scenario)
	{
		switch (Scenario)
		{
		case EScenario::SingleRoute:       return TEXT("SingleRoute");
		case EScenario::SustainedRoute:    return TEXT("SustainedRoute");
		default:                           return TEXT("OverlappingRoutes");
		}
	}

	static FString FixturePath(EScenario Scenario)
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TrajectoryCalibration"),
			FString::Printf(TEXT("Trajectory_%s.json"), NameFor(Scenario)));
	}

	/**
	 * Position for a given scenario at a given timestep, in metres.
	 *
	 * Every agent in a scenario gets the SAME position — see the header note on why overlap must be exact.
	 */
	static void PositionAt(EScenario Scenario, int32 Timestep, double& OutX, double& OutY)
	{
		const double Alpha = static_cast<double>(Timestep) / static_cast<double>(Timesteps - 1);
		OutY = 25.0;

		switch (Scenario)
		{
		case EScenario::SustainedRoute:
			// 0.5 m travelled across the whole run: ~0.005 m per sample against a ~0.049 m texel, so
			// consecutive samples land in the same texel and accumulate as dwell rather than as a path.
			// Non-zero movement is required — the processor and the actor both skip zero-length segments.
			OutX = 25.0 + (0.5 * Alpha);
			break;

		case EScenario::SingleRoute:
		case EScenario::OverlappingRoutes:
		default:
			// A straight, axis-aligned 40 m traverse. Axis-aligned keeps Bresenham run lengths uniform,
			// so an interior texel gets exactly three brush hits per pass.
			OutX = 5.0 + (40.0 * Alpha);
			break;
		}
	}

	/** Writes a Mobius agent-movement JSON fixture for one scenario. Mirrors MobiusInGameTests' schema. */
	static bool WriteFixture(EScenario Scenario)
	{
		const FString Path = FixturePath(Scenario);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);

		const int32 AgentCount = AgentCountFor(Scenario);

		FString Json;
		Json.Reserve(128 * 1024);
		Json += FString::Printf(
			TEXT("{\n\"metadata\": { \"duration\": %.1f, \"sampling_rate\": %.2f, \"max_num_entities\": %d, \"isSI\": true, \"isDeg\": true },\n"),
			DurationSeconds, SampleInterval, AgentCount);

		Json += TEXT("\"entities\": [\n");
		for (int32 Agent = 0; Agent < AgentCount; ++Agent)
		{
			Json += FString::Printf(
				TEXT("{ \"id\": %d, \"name\": \"%s_%d\", \"simTimeS\": %.1f, \"max_speed\": 1.5, \"m_plane\": \"floor_0\", \"map\": 0 }%s\n"),
				Agent, NameFor(Scenario), Agent, DurationSeconds, Agent + 1 < AgentCount ? TEXT(",") : TEXT(""));
		}
		Json += TEXT("],\n\"simulation\": [\n");

		for (int32 Ts = 0; Ts < Timesteps; ++Ts)
		{
			double X = 0.0;
			double Y = 0.0;
			PositionAt(Scenario, Ts, X, Y);

			Json += TEXT("{ \"samples\": [\n");
			for (int32 Agent = 0; Agent < AgentCount; ++Agent)
			{
				Json += FString::Printf(
					TEXT("{ \"entity\": %d, \"rotation\": 0.0, \"speed\": 1.0, \"mode\": \"walk\", \"position\": { \"x\": %.4f, \"y\": %.4f, \"z\": 0.0 } }%s\n"),
					Agent, X, Y, Agent + 1 < AgentCount ? TEXT(",") : TEXT(""));
			}
			Json += FString::Printf(TEXT("] }%s\n"), Ts + 1 < Timesteps ? TEXT(",") : TEXT(""));
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

	/** Summary of one accumulation buffer: how much was touched, how hot, and the typical texel. */
	struct FAccumulationStats
	{
		int32 TouchedTexels = 0;
		int32 PeakByte = 0;
		int32 ModalByte = 0;      // most common non-zero value == the ordinary interior texel
		int32 ModalCount = 0;
		int32 InLowestBandCount = 0;

		float LowestBandFraction() const
		{
			return TouchedTexels > 0 ? static_cast<float>(InLowestBandCount) / static_cast<float>(TouchedTexels) : 0.0f;
		}
	};

	static FAccumulationStats Measure(const UDynamicPixelRenderingTexture& Texture)
	{
		// LOS_A edge, mirrored from the material custom node / DynamicPixelRenderingTexture macros.
		constexpr float LOS_A = 0.1419f;

		FAccumulationStats Stats;
		int32 Histogram[256] = { 0 };

		const FVector2D Size = Texture.GetDynamicTextureSize();
		const int32 Width = FMath::TruncToInt(Size.X);
		const int32 Height = FMath::TruncToInt(Size.Y);

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
				++Histogram[Red];
				Stats.PeakByte = FMath::Max(Stats.PeakByte, static_cast<int32>(Red));
				if ((static_cast<float>(Red) / 255.0f) < LOS_A)
				{
					++Stats.InLowestBandCount;
				}
			}
		}

		for (int32 Value = 1; Value < 256; ++Value)
		{
			if (Histogram[Value] > Stats.ModalCount)
			{
				Stats.ModalCount = Histogram[Value];
				Stats.ModalByte = Value;
			}
		}
		return Stats;
	}

	/** Spawns a heatmap sized independently of any building and switches it into trajectory mode. */
	static AHeatmapPixelTextureVisualizer* SpawnTrajectoryHeatmap(UWorld* World)
	{
		UHeatmapSubsystem* Subsystem = World ? World->GetSubsystem<UHeatmapSubsystem>() : nullptr;
		if (!Subsystem)
		{
			return nullptr;
		}

		AHeatmapPixelTextureVisualizer* Heatmap =
			World->SpawnActor<AHeatmapPixelTextureVisualizer>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Heatmap)
		{
			return nullptr;
		}

		const float SizeCm = HeatmapMetres * 100.0f;
		Heatmap->ActorName = TEXT("Heatmap_TrajectoryCalibration");
		Heatmap->FloorID = 0;
		Heatmap->InitializeHeatmap(2, true, FVector2D(SizeCm, SizeCm), 0.0f, false);
		Subsystem->AddHeatmapActor(Heatmap);
		Subsystem->SetTrajectoryHeatmapsEnabled(true);
		return Heatmap;
	}

	// --- latent commands -------------------------------------------------------------------------

	/** Wait until the import pipeline has cached entity data for the expected agent count. */
	class FWaitForScenarioLoadedCommand : public IAutomationLatentCommand
	{
	public:
		FWaitForScenarioLoadedCommand(FAutomationTestBase& InTest, int32 InAgentCount, double InTimeoutSeconds)
			: Test(InTest), AgentCount(InAgentCount), Deadline(FPlatformTime::Seconds() + InTimeoutSeconds) {}

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
				Test.AddError(TEXT("timed out waiting for the trajectory fixture to import"));
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase& Test;
		int32 AgentCount;
		double Deadline;
	};

	/** Spawn the heatmap once the sim is loaded, so it exists before any segment is submitted. */
	class FSpawnHeatmapCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSpawnHeatmapCommand(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GetActiveGameWorld();
			if (!World)
			{
				Test.AddError(TEXT("no game world when spawning the calibration heatmap"));
				return true;
			}
			if (!SpawnTrajectoryHeatmap(World))
			{
				Test.AddError(TEXT("failed to spawn the calibration heatmap"));
			}
			return true;
		}

	private:
		FAutomationTestBase& Test;
	};

	/**
	 * Step the playhead forward in 0.1 s increments — exactly the processor's gate — letting a couple of
	 * frames elapse per step so the segment submission actually runs. Forward-only: a backward jump
	 * clears the trajectory buffer by design.
	 */
	class FPlayForwardCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPlayForwardCommand(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GetActiveGameWorld();
			if (!World)
			{
				Test.AddError(TEXT("game world disappeared during playback"));
				return true;
			}
			if (FramesToSettle > 0)
			{
				--FramesToSettle;
				return false;
			}
			if (Step >= Timesteps)
			{
				return true;
			}

			UTimeDilationSubSystem* TimeDilation = World->GetSubsystem<UTimeDilationSubSystem>();
			if (!TimeDilation)
			{
				Test.AddError(TEXT("TimeDilation subsystem missing"));
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

	/** Read the accumulation buffer back and assert the scenario's expected shape. */
	class FAssertScenarioCommand : public IAutomationLatentCommand
	{
	public:
		FAssertScenarioCommand(FAutomationTestBase& InTest, EScenario InScenario)
			: Test(InTest), Scenario(InScenario) {}

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

			const FAccumulationStats Stats = Measure(*Texture);

			// Always report the measurement — this is the calibration data the exercise exists to produce.
			UE_LOG(LogTemp, Display,
				TEXT("[TrajectoryCalibration] %s: touched=%d peak=%d modal=%d (x%d) lowestBand=%.1f%%"),
				NameFor(Scenario), Stats.TouchedTexels, Stats.PeakByte, Stats.ModalByte, Stats.ModalCount,
				Stats.LowestBandFraction() * 100.0f);

			Test.TestTrue(TEXT("the scenario touched some texels"), Stats.TouchedTexels > 0);

			switch (Scenario)
			{
			case EScenario::SingleRoute:
				// One pass over an interior texel: seed 25 plus two truncated increments.
				Test.TestEqual(TEXT("a single traversal's typical texel is 27"), Stats.ModalByte, 27);
				Test.TestTrue(TEXT("a single traversal stays overwhelmingly in the lowest band"),
					Stats.LowestBandFraction() > 0.9f);
				break;

			case EScenario::SustainedRoute:
				// Dwell puts many samples through the same few texels, so it must climb well past a
				// single pass while touching far fewer texels than a full traverse.
				Test.TestTrue(TEXT("dwell accumulates above a single traversal"), Stats.PeakByte > 27);
				Test.TestTrue(TEXT("dwell stays spatially concentrated"), Stats.TouchedTexels < 400);
				break;

			case EScenario::OverlappingRoutes:
				// Five identical passes: seed 25, then (5 * 3 - 1) truncated increments == 39.
				Test.TestEqual(TEXT("five overlapping traversals give a typical texel of 39"), Stats.ModalByte, 39);
				Test.TestTrue(TEXT("overlap reads hotter than a single traversal"), Stats.ModalByte > 27);
				break;
			}
			return true;
		}

	private:
		FAutomationTestBase& Test;
		EScenario Scenario;
	};

	/** Shared body: write the fixture, import it, spawn the heatmap, play, then measure. */
	static bool RunScenario(FAutomationTestBase& Test, EScenario Scenario)
	{
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

		Test.TestTrue(TEXT("fixture written"), WriteFixture(Scenario));

		// Drop any cache for this fixture so the run is a deterministic full import.
		const FString Path = FixturePath(Scenario);
		const uint64 Hash = MobiusSimCache::ComputeSourceHash(Path);
		IFileManager::Get().Delete(*MobiusSimCache::MakeCacheFilePath(Path, Hash), false, true);

		GameInstance->SetPedestrianDataFilePath(Path);

		ADD_LATENT_AUTOMATION_COMMAND(FWaitForScenarioLoadedCommand(Test, AgentCountFor(Scenario), 60.0));
		ADD_LATENT_AUTOMATION_COMMAND(FSpawnHeatmapCommand(Test));
		ADD_LATENT_AUTOMATION_COMMAND(FPlayForwardCommand(Test));
		ADD_LATENT_AUTOMATION_COMMAND(FAssertScenarioCommand(Test, Scenario));
		return true;
	}
}

// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryHeatmapSingleRouteTest,
	"Mobius.InGame.TrajectoryHeatmap.SingleRoute",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryHeatmapSingleRouteTest::RunTest(const FString& Parameters)
{
	return TrajectoryInGame::RunScenario(*this, TrajectoryInGame::EScenario::SingleRoute);
}

// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryHeatmapSustainedRouteTest,
	"Mobius.InGame.TrajectoryHeatmap.SustainedRoute",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryHeatmapSustainedRouteTest::RunTest(const FString& Parameters)
{
	return TrajectoryInGame::RunScenario(*this, TrajectoryInGame::EScenario::SustainedRoute);
}

// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryHeatmapOverlappingRoutesTest,
	"Mobius.InGame.TrajectoryHeatmap.OverlappingRoutes",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTrajectoryHeatmapOverlappingRoutesTest::RunTest(const FString& Parameters)
{
	return TrajectoryInGame::RunScenario(*this, TrajectoryInGame::EScenario::OverlappingRoutes);
}

#endif // !UE_BUILD_SHIPPING
