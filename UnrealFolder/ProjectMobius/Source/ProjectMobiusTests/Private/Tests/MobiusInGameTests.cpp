// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// MobiusInGameTests.cpp
//
// PRD 02 task T7 — in-game functional cases. These run in a REAL game world (the shipping map,
// GameInstance, Mass subsystems, processors ticking), not against isolated classes, driving the
// same entry points the UI uses: SetPedestrianDataFilePath -> full import/spawn pipeline, and
// OverrideCurrentTime -> playhead scrubs (the call path of the A4 big-skip crash).
//
// ClientContext only: they need a live game world, so they exist in `-game` runs and are invisible
// to the editor/headless-editor automation lists. Named "Mobius.InGame." (no "ProjectMobius."
// substring) so the default correctness filter never picks them up either.
//
// Run: MobiusPerf\RunTests.ps1 -InGame
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
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

namespace
{
	constexpr int32 InGameAgentCount = 3;
	constexpr int32 InGameTimesteps = 200; // sampling 0.1 -> 20 s of playback

	FString InGameFixturePath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusInGameTest"), TEXT("InGameFixture.json"));
	}

	/** 3 agents moving linearly for 200 timesteps — enough span for big forward/backward scrubs. */
	bool WriteInGameFixture()
	{
		const FString Path = InGameFixturePath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);

		FString Json;
		Json.Reserve(64 * 1024 + 200 * InGameAgentCount * 128);
		Json += TEXT("{\n\"metadata\": { \"duration\": 20.0, \"sampling_rate\": 0.1, \"max_num_entities\": 3, \"isSI\": true, \"isDeg\": true },\n");
		Json += TEXT("\"entities\": [\n");
		for (int32 Agent = 0; Agent < InGameAgentCount; ++Agent)
		{
			Json += FString::Printf(TEXT("{ \"id\": %d, \"name\": \"ingame_%d\", \"simTimeS\": 20.0, \"max_speed\": 1.5, \"m_plane\": \"floor_0\", \"map\": 0 }%s\n"),
				Agent, Agent, Agent + 1 < InGameAgentCount ? TEXT(",") : TEXT(""));
		}
		Json += TEXT("],\n\"simulation\": [\n");
		for (int32 Ts = 0; Ts < InGameTimesteps; ++Ts)
		{
			Json += TEXT("{ \"samples\": [\n");
			for (int32 Agent = 0; Agent < InGameAgentCount; ++Agent)
			{
				Json += FString::Printf(TEXT("{ \"entity\": %d, \"rotation\": %d.0, \"speed\": 1.0, \"mode\": \"walk\", \"position\": { \"x\": %d.%02d, \"y\": %d.0, \"z\": 0.0 } }%s\n"),
					Agent, (Ts * 2) % 360, Ts / 100, Ts % 100, Agent * 2,
					Agent + 1 < InGameAgentCount ? TEXT(",") : TEXT(""));
			}
			Json += FString::Printf(TEXT("] }%s\n"), Ts + 1 < InGameTimesteps ? TEXT(",") : TEXT(""));
		}
		Json += TEXT("]\n}\n");
		return FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	UWorld* GetActiveGameWorld()
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

	/** Latent: wait (with deadline) until the import pipeline reports loaded + entity data cached. */
	class FWaitForSimLoadedCommand : public IAutomationLatentCommand
	{
	public:
		FWaitForSimLoadedCommand(FAutomationTestBase& InTest, double InTimeoutSeconds)
			: Test(InTest)
			, Deadline(FPlatformTime::Seconds() + InTimeoutSeconds)
		{
		}

		virtual bool Update() override
		{
			// bIsDataLoaded is a one-frame handshake (the subsystem Tick consumes and resets it),
			// so the durable "load done and fragments built" signal is CachedEntityData, which
			// BuildPedestrianMovementFragmentData fills and which persists until the next file.
			UWorld* World = GetActiveGameWorld();
			if (World)
			{
				const UAgentDataSubsystem* AgentData = World->GetSubsystem<UAgentDataSubsystem>();
				if (AgentData && AgentData->CachedEntityData.Num() == InGameAgentCount)
				{
					return true;
				}
			}
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test.AddError(TEXT("timed out waiting for the in-game sim load (CachedEntityData)"));
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase& Test;
		double Deadline;
	};

	/** Latent: big forward/backward playhead jumps with a few frames of playback between each —
	 *  the automated form of the A4 big-skip crash reproducer. */
	class FScrubTortureCommand : public IAutomationLatentCommand
	{
	public:
		explicit FScrubTortureCommand(FAutomationTestBase& InTest)
			: Test(InTest)
		{
		}

		virtual bool Update() override
		{
			UWorld* World = GetActiveGameWorld();
			if (!World)
			{
				Test.AddError(TEXT("game world disappeared mid-scrub"));
				return true;
			}
			if (FramesToSettle > 0)
			{
				--FramesToSettle;
				return false;
			}

			static const float Jumps[] = { 0.5f, 15.0f, 2.0f, 19.5f, 0.1f, 10.0f, 19.9f, 0.0f };
			if (JumpIndex >= UE_ARRAY_COUNT(Jumps))
			{
				return true;
			}

			UTimeDilationSubSystem* TimeDilation = World->GetSubsystem<UTimeDilationSubSystem>();
			if (!TimeDilation)
			{
				Test.AddError(TEXT("TimeDilation subsystem missing"));
				return true;
			}
			TimeDilation->OverrideCurrentTime(Jumps[JumpIndex], /*PreviouslyPaused*/ 0);
			++JumpIndex;
			FramesToSettle = 6; // let the movement processor serve several frames at the new playhead
			return false;
		}

	private:
		FAutomationTestBase& Test;
		int32 JumpIndex = 0;
		int32 FramesToSettle = 0;
	};

	/** Latent: end-state assertions + optional cvar restore. */
	class FInGameEpilogueCommand : public IAutomationLatentCommand
	{
	public:
		FInGameEpilogueCommand(FAutomationTestBase& InTest, IConsoleVariable* InRestoreCVar, int32 InRestoreValue)
			: Test(InTest)
			, RestoreCVar(InRestoreCVar)
			, RestoreValue(InRestoreValue)
		{
		}

		virtual bool Update() override
		{
			UWorld* World = GetActiveGameWorld();
			const UAgentDataSubsystem* AgentData = World ? World->GetSubsystem<UAgentDataSubsystem>() : nullptr;
			Test.TestTrue(TEXT("world alive after scrub torture"), World != nullptr);
			Test.TestTrue(TEXT("entity data intact after scrub torture"),
				AgentData && AgentData->CachedEntityData.Num() == InGameAgentCount);
			if (RestoreCVar)
			{
				RestoreCVar->Set(RestoreValue, ECVF_SetByCode);
			}
			return true;
		}

	private:
		FAutomationTestBase& Test;
		IConsoleVariable* RestoreCVar;
		int32 RestoreValue;
	};
}

// --- resident-provider path ---------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMobiusInGameLoadPlaybackScrubTest,
	"Mobius.InGame.LoadPlaybackScrub",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FMobiusInGameLoadPlaybackScrubTest::RunTest(const FString& Parameters)
{
	UWorld* World = GetActiveGameWorld();
	if (!World)
	{
		AddError(TEXT("no game world - run via RunTests.ps1 -InGame (UnrealEditor-Cmd -game)"));
		return false;
	}
	UProjectMobiusGameInstance* GameInstance = Cast<UProjectMobiusGameInstance>(World->GetGameInstance());
	TestNotNull(TEXT("Mobius game instance"), GameInstance);
	if (!GameInstance)
	{
		return false;
	}

	TestTrue(TEXT("fixture written"), WriteInGameFixture());
	// Deterministic full first import: no stale cache for the fixture.
	const uint64 Hash = MobiusSimCache::ComputeSourceHash(InGameFixturePath());
	IFileManager::Get().Delete(*MobiusSimCache::MakeCacheFilePath(InGameFixturePath(), Hash), false, true);

	// The exact entry point the file-picker UI uses.
	GameInstance->SetPedestrianDataFilePath(InGameFixturePath());

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForSimLoadedCommand(*this, /*TimeoutSeconds*/ 60.0));
	ADD_LATENT_AUTOMATION_COMMAND(FScrubTortureCommand(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FInGameEpilogueCommand(*this, nullptr, 0));
	return true;
}

// --- streaming-provider path (A4/A5) -------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMobiusInGameStreamingScrubTest,
	"Mobius.InGame.StreamingScrub",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FMobiusInGameStreamingScrubTest::RunTest(const FString& Parameters)
{
	UWorld* World = GetActiveGameWorld();
	if (!World)
	{
		AddError(TEXT("no game world - run via RunTests.ps1 -InGame (UnrealEditor-Cmd -game)"));
		return false;
	}
	UProjectMobiusGameInstance* GameInstance = Cast<UProjectMobiusGameInstance>(World->GetGameInstance());
	TestNotNull(TEXT("Mobius game instance"), GameInstance);
	if (!GameInstance)
	{
		return false;
	}

	TestTrue(TEXT("fixture written"), WriteInGameFixture());

	// Force the streaming provider (A4) regardless of dataset size, then run the SAME load +
	// scrub-torture flow: this is the automated reproducer of the 2026-07-03 big-skip crash.
	IConsoleVariable* ForceStreamingCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mobius.SimCache.ForceStreaming"));
	TestNotNull(TEXT("ForceStreaming cvar registered"), ForceStreamingCVar);
	const int32 SavedForceStreaming = ForceStreamingCVar ? ForceStreamingCVar->GetInt() : 0;
	if (ForceStreamingCVar)
	{
		ForceStreamingCVar->Set(1, ECVF_SetByCode);
	}

	GameInstance->SetPedestrianDataFilePath(InGameFixturePath());

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForSimLoadedCommand(*this, /*TimeoutSeconds*/ 60.0));
	ADD_LATENT_AUTOMATION_COMMAND(FScrubTortureCommand(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FInGameEpilogueCommand(*this, ForceStreamingCVar, SavedForceStreaming));
	return true;
}

#endif // !UE_BUILD_SHIPPING
