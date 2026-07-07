// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// InGameTenabilityScrubTest.cpp
//
// Task 6 of the B-RISK tenability-timeline plan (2026-07-07-brisk-tenability-timeline-v2.md):
// end-to-end proof that per-agent tenability is a pure function of the recorded data (identical at
// any playhead position regardless of navigation), and recomputes correctly whenever the agent file
// or the B-RISK file changes. Drives the SAME entry points the UI uses, through the real Mass
// processors ticking in a live game world:
//   - GameInstance->SetPedestrianDataFilePath(...)  -> agent import/spawn (bumps DataGeneration)
//   - GameInstance->SetBRiskSmvFilePath(...)         -> OnBRiskFileChanged -> UBRiskDataSubsystem::
//                                                        LoadScenarioFromSmv -> OnBRiskScenarioLoaded
//   - TimeDilationSubSystem->OverrideCurrentTime(t)  -> OnNewCurrentTime -> playhead scrub
//   - UBRiskDataSubsystem::ClearScenario()           -> OnBRiskScenarioCleared -> timelines dropped
//
// HARNESS: reuses MobiusInGameTests.cpp's idiom VERBATIM — GetActiveGameWorld() over
// GEngine->GetWorldContexts(), IAutomationLatentCommand subclasses with a deadline, the fixture
// written under FPaths::ProjectSavedDir(). The B-RISK fixture writers (MakeSmv/MakeZoneCsv/
// MakeOutputXml/MakeInputXml) are adapted from BRiskTenabilityTest.cpp's file-local writers, with
// the curves retuned so toxic FED dose is nonzero MID-exposure (before the visibility failure time)
// — the exact case v1's fixture masked (scrub target before untenability onset showed dose == 0,
// hiding the navigation-dependence bug this test must expose).
//
// CONTEXT (like MobiusInGameTests): a full processor/subsystem tick chain needs a REAL game world,
// so this is a ClientContext test — it runs under `-game` (MobiusPerf\RunTests.ps1 -InGame, which
// filters Mobius.InGame.*) and is INVISIBLE to the headless-editor / default correctness automation
// lists. It is deliberately gated on ClientContext because there is no way to tick
// PedestrianMovement/AgentEgressHealth processors without a live world.
//
// VERIFIED as a regression net (2026-07-07): the retro-fail check ran via a temporary one-line
// latch sabotage in the health processor's projection block
// (`bTenabilityFailed = bTenabilityFailed || bFailedByNow`) — Scenario 1 failed on
// "S1@T0: not failed (scrubbed before failure time)" for every agent, then went green on restore.
// (A `git stash` of the processor cpp cannot serve as the retro-fail: the pre-timeline processor
// no longer compiles against the current fragments header.)
//
// Run: MobiusPerf\RunTests.ps1 -InGame
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"

#include "GameInstances/ProjectMobiusGameInstance.h"
#include "MassEntitySubsystem.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Fragments/AgentEgressTenabilityFragments.h"
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "BRisk/BRiskDataSubsystem.h"
#include "BRisk/BRiskEgressSubsystem.h"
#include "Subsystems/TimeDilationSubSystem.h"

namespace
{
	// --- Fixture geometry --------------------------------------------------------------------------
	//
	// B-RISK ROOM 1: dims "24 5.5 2.6" m, origin "0 0 0" m. With BRiskCoord::ToUnrealBox (X<->Y swap,
	// Scale 100 cm/m) this resolves to WorldBounds Min=(0,0,0) Max=(550,2400,260) cm (verified against
	// BRiskCoord::ToUnrealBox + RebuildRoomCache). The B-RISK width 24 m -> UE Y=2400; depth 5.5 m -> UE
	// X=550.
	//
	// Agent JSON positions are transformed by FProcessAgentSimulationDataRunnable
	// (AgentDataSubsystem.cpp:606-608): CurrentLocation = ( x, -y, z ) * 100 for isSI=true JSON (note the
	// Y NEGATION, JSON-format-specific). The health processor samples the breathing point
	// CurrentLocation + (0,0,BreathingHeightCm=160). So to land the breathing point inside the room box:
	//   JSON x in [0, 5.5]   -> world X in [0, 550]
	//   JSON y in [-24, 0]   -> world Y in [0, 2400]   (must be NEGATIVE JSON y)
	//   JSON z = 0           -> world Z = 0, breathing Z = 160 in [0, 260]
	// Agents oscillate in JSON y within [-16, -8] (world Y 800..1600) so they (a) stay inside the room
	// for the whole run (one occupancy interval, dose == room FEDSum) and (b) move >= 0.5 m/record so the
	// gait bracketer (AgentDataSubsystem::CalcSmoothedStepMovementBrackets) assigns a non-Emb_NotMoving
	// bracket mid-run — the "not frozen" signal the frozen-agent check reads.

	constexpr int32 AgentCount = 3;
	constexpr float SamplingRate = 0.5f;      // s per record
	constexpr float Duration = 100.0f;        // s of trajectory
	constexpr int32 Timesteps = 200;          // Duration / SamplingRate

	constexpr float RoomFixedJsonX = 3.0f;    // world X = 300 cm (mid room depth 0..550)
	constexpr float OscMinJsonY = -16.0f;     // world Y = 1600 cm
	constexpr float OscMaxJsonY = -8.0f;      // world Y = 800 cm
	constexpr float OscStepJsonY = 1.0f;      // 1 m JSON / record -> 100 cm world / record; well above
	                                          // the 0.5 m/record floor for a moving gait bracket

	// --- Playhead sample times ---------------------------------------------------------------------
	// T1 is past the fixture-A visibility failure (~65 s, see MakeOutputXmlSlow); T0 is mid-exposure but
	// BEFORE that failure, with nonzero toxic dose on the first pass (the case v1 masked).
	constexpr float T0 = 30.0f;   // mid-exposure: dose > 0 (toxic FEDSum(30) ~= 0.075), no failure yet
	constexpr float T1 = 90.0f;   // past failure onset (agents failed by now)

	// --- Latent-command tick budgets (bounded; NO wall-clock sleeps) -------------------------------
	constexpr int32 SettleTicks = 8;          // frames to let processors serve a new playhead position
	constexpr int32 MaxTimelineBuildTicks = 600; // upper bound on frames to await AreAgentTimelinesCurrent

	FString FixtureRoot()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("InGameTenabilityScrubTest"));
	}

	bool WriteText(const FString& Path, const FString& Contents)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);
		return FFileHelper::SaveStringToFile(Contents, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
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

	// -----------------------------------------------------------------------------------------------
	// Agent fixture: AgentCount agents, all resident inside ROOM 1 for the whole run, oscillating in
	// world Y so they keep a moving gait bracket. bSlightOffset shifts the second agent set's X to make
	// a genuinely DIFFERENT trajectory file (scenario 2's agent-file swap) that still lands in-room.
	// -----------------------------------------------------------------------------------------------
	FString MakeAgentJson(const int32 NumAgents, const float JsonXBase)
	{
		FString Json;
		Json.Reserve(64 * 1024);
		Json += FString::Printf(
			TEXT("{\n\"metadata\": { \"duration\": %.1f, \"sampling_rate\": %.4f, \"max_num_entities\": %d, \"isSI\": true, \"isDeg\": true },\n"),
			Duration, SamplingRate, NumAgents);
		Json += TEXT("\"entities\": [\n");
		for (int32 Agent = 0; Agent < NumAgents; ++Agent)
		{
			Json += FString::Printf(
				TEXT("{ \"id\": %d, \"name\": \"scrub_%d\", \"simTimeS\": %.1f, \"max_speed\": 1.5, \"m_plane\": \"floor_0\", \"map\": 0 }%s\n"),
				Agent, Agent, Duration, Agent + 1 < NumAgents ? TEXT(",") : TEXT(""));
		}
		Json += TEXT("],\n\"simulation\": [\n");
		for (int32 Ts = 0; Ts < Timesteps; ++Ts)
		{
			Json += TEXT("{ \"samples\": [\n");
			for (int32 Agent = 0; Agent < NumAgents; ++Agent)
			{
				// Triangle-wave oscillation in JSON y between OscMinJsonY and OscMaxJsonY.
				const float Span = OscMaxJsonY - OscMinJsonY;           // 8
				const float Cycle = 2.0f * Span;                        // 16
				const float Phase = FMath::Fmod(static_cast<float>(Ts) * OscStepJsonY + Agent * 2.0f, Cycle);
				const float TriY = Phase <= Span ? Phase : (Cycle - Phase); // 0..Span..0
				const float JsonY = OscMinJsonY + TriY;                 // in [-16, -8]
				const float JsonX = JsonXBase + Agent * 0.3f;           // small per-agent X separation, in-room
				// rotation is degrees (isDeg true); speed field is unused by the gait bracketer (it
				// derives speed from position deltas), but written for schema completeness.
				Json += FString::Printf(
					TEXT("{ \"entity\": %d, \"rotation\": 0.0, \"speed\": 1.0, \"mode\": \"walk\", \"position\": { \"x\": %.3f, \"y\": %.3f, \"z\": 0.0 } }%s\n"),
					Agent, JsonX, JsonY, Agent + 1 < NumAgents ? TEXT(",") : TEXT(""));
			}
			Json += FString::Printf(TEXT("] }%s\n"), Ts + 1 < Timesteps ? TEXT(",") : TEXT(""));
		}
		Json += TEXT("]\n}\n");
		return Json;
	}

	// -----------------------------------------------------------------------------------------------
	// B-RISK fixture writers (adapted from BRiskTenabilityTest.cpp). Single ROOM 1.
	// -----------------------------------------------------------------------------------------------
	FString MakeSmv(const TCHAR* ZoneCsvName)
	{
		return FString::Printf(
			TEXT("ROOM   1\n")
			TEXT(" 2.4000E+001 5.5000E+000 2.6000E+000\n")
			TEXT(" 0.0000E+000 0.0000E+000 0.0000E+000\n")
			TEXT("LABEL\n 0 0 0\nroom\n")
			TEXT("ZONE\n")
			TEXT("%s\n"),
			ZoneCsvName);
	}

	// Raw zone CSV (two header lines: units, then names). Values here are not the tenability source
	// (output1.xml is), but the importer requires a parseable zone table for HasScenarioData().
	FString MakeZoneCsv()
	{
		return TEXT("s,C,C,m,Pa,1 / m,1 / m,kW,\n")
			TEXT("Time,ULT_1,LLT_1,HGT_1,PRS_1,ULOD_1,LLOD_1,HRR_1,\n")
			TEXT("0,24,24,2.6,0,0.1,0.1,0,\n")
			TEXT("100,222,200,1.35,0,0.5,0.3,1391,\n");
	}

	FString MakeTimeBlock(
		const double T, const double HRR, const double Layer, const double UpT, const double LowT,
		const double FEDSum, const double Vis, const double FEDRad)
	{
		return FString::Printf(
			TEXT("      <time value=\"%g\" units=\"sec\">\n")
			TEXT("        <HeatRelease value=\"%g\" units=\"kW\" />\n")
			TEXT("        <layerheight value=\"%g\" units=\"m\" />\n")
			TEXT("        <uppertemp value=\"%g\" units=\"C\" />\n")
			TEXT("        <lowertemp value=\"%g\" units=\"C\" />\n")
			TEXT("        <FEDSum value=\"%g\" units=\"-\" />\n")
			TEXT("        <Visibility value=\"%g\" units=\"m\" />\n")
			TEXT("        <FEDRadSum value=\"%g\" units=\"-\" />\n")
			TEXT("      </time>\n"),
			T, HRR, Layer, UpT, LowT, FEDSum, Vis, FEDRad);
	}

	// FIXTURE A ("slow" curve). Piecewise-linear, hand-picked so the crossings are exact:
	//   Visibility: 20 @0, 20 @40, 4 @80  -> crosses endpoint 10 m at
	//               t* = 40 + 40*(20-10)/(20-4) = 40 + 25 = 65.0 s
	//   Toxic FED : 0 @0, 0.1 @40, 0.4 @80 -> dose(30) = 0.1*(30/40) = 0.075 (NONZERO, T0 mid-exposure);
	//               crosses endpoint 0.3 at t = 40 + 40*(0.3-0.1)/(0.4-0.1) = 40 + 26.67 = 66.67 s
	// First failure = Visibility @65 s (priority Visibility > ToxicFED, and 65 < 66.67).
	FString MakeOutputXmlSlow()
	{
		FString Xml =
			TEXT("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n")
			TEXT("<output>\n  <run id=\"input1.xml\">\n    <room id=\"1\">\n");
		Xml += MakeTimeBlock(0,   0.0,    2.60, 24.0, 24.0, 0.0,  20.0, 0.0);
		Xml += MakeTimeBlock(40,  500.0,  2.20, 60.0, 30.0, 0.10, 20.0, 0.0);
		Xml += MakeTimeBlock(80,  1000.0, 1.50, 120.0, 40.0, 0.40, 4.0, 0.05);
		Xml += MakeTimeBlock(100, 1200.0, 1.35, 150.0, 45.0, 0.60, 3.0, 0.10);
		Xml += TEXT("    </room>\n  </run>\n</output>\n");
		return Xml;
	}

	// FIXTURE B ("fast" curve, scenario 3). Visibility collapses earlier:
	//   Visibility: 20 @0, 4 @40 -> crosses endpoint 10 m at t* = 40*(20-10)/(20-4) = 25.0 s
	// So the fixture-B visibility failure time (~25 s) differs from fixture A's (~65 s): scenario 3
	// asserts the NEW curve's golden crossing after the B-RISK file swap.
	FString MakeOutputXmlFast()
	{
		FString Xml =
			TEXT("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n")
			TEXT("<output>\n  <run id=\"input1.xml\">\n    <room id=\"1\">\n");
		Xml += MakeTimeBlock(0,   0.0,    2.60, 24.0, 24.0, 0.0,  20.0, 0.0);
		Xml += MakeTimeBlock(40,  800.0,  2.00, 90.0, 40.0, 0.20, 4.0, 0.02);
		Xml += MakeTimeBlock(100, 1400.0, 1.30, 160.0, 50.0, 0.50, 2.0, 0.10);
		Xml += TEXT("    </room>\n  </run>\n</output>\n");
		return Xml;
	}

	FString MakeInputXml()
	{
		// endpoint_visibility 10 m, endpoint_FED 0.3, endpoint_radiation 0.3 (thermal). monitor_height 2.
		// endpoint_temp is deliberately NOT mapped to a Celsius criterion by the subsystem, so the
		// temperature criterion stays disabled — visibility/toxic FED drive the failures here.
		return TEXT("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n")
			TEXT("<bri>\n  <tenability>\n")
			TEXT("    <monitor_height>2</monitor_height>\n")
			TEXT("    <endpoint_radiation>0.3</endpoint_radiation>\n")
			TEXT("    <endpoint_temp>1146</endpoint_temp>\n")
			TEXT("    <endpoint_visibility>10</endpoint_visibility>\n")
			TEXT("    <endpoint_FED>0.3</endpoint_FED>\n")
			TEXT("  </tenability>\n</bri>\n");
	}

	// Golden failure times derived above (seconds). Tolerance covers timestep quantization of the
	// timeline occupancy (integrity invariant 5: occupancy quantized to the agent trajectory step).
	constexpr float FixtureA_VisFailTime = 65.0f;
	constexpr float FixtureB_VisFailTime = 25.0f;
	constexpr float FailTimeTolerance = 2.0f;      // ~= a few agent trajectory steps
	constexpr float DoseExactTolerance = 1e-6f;    // navigation-independent dose is a closed-form lookup

	// Write both B-RISK companion files for a given output1.xml into a unique dir; return the .smv path.
	FString WriteBRiskScenario(const FString& SubDirName, const FString& OutputXml)
	{
		const FString Dir = FPaths::Combine(FixtureRoot(), SubDirName);
		IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true);
		const TCHAR* ZoneCsvName = TEXT("scenario_zone.csv");
		const FString SmvPath = FPaths::Combine(Dir, TEXT("scenario.smv"));
		WriteText(SmvPath, MakeSmv(ZoneCsvName));
		WriteText(FPaths::Combine(Dir, ZoneCsvName), MakeZoneCsv());
		WriteText(FPaths::Combine(Dir, TEXT("output1.xml")), OutputXml);
		WriteText(FPaths::Combine(Dir, TEXT("input1.xml")), MakeInputXml());
		return SmvPath;
	}

	// -----------------------------------------------------------------------------------------------
	// Mass-fragment snapshot (from v1 Task 4 Step 1; field names adapted to the CURRENT projection
	// APIs). Queries live entities over FEntityMovementFragment + FAgentEgressTenabilityFragment.
	// -----------------------------------------------------------------------------------------------
	struct FAgentTenabilitySnapshot
	{
		int32 EntityID = INDEX_NONE;
		bool bTenabilityFailed = false;
		bool bIsDead = false;
		float FirstFailureTime = -1.0f;
		float DeathTime = -1.0f;
		float AccumulatedToxicFED = 0.0f;
		float AccumulatedThermalFED = 0.0f;
		float DisplayRisk = 0.0f;
		ETenabilityCriterion DominantCriterion = ETenabilityCriterion::None;
		ETenabilityCriterion FirstFailureCriterion = ETenabilityCriterion::None;
		EPedestrianMovementBracket MovementBracket = EPedestrianMovementBracket::Emb_NotMoving;
	};

	void SnapshotAgents(UWorld* World, TArray<FAgentTenabilitySnapshot>& Out)
	{
		Out.Reset();
		if (!World)
		{
			return;
		}
		UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
		if (!MassSubsystem)
		{
			return;
		}
		FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();

		// Ad-hoc (non-processor) query: default-constructed, requirements added, then
		// ForEachEntityChunk(EntityManager, ...) caches archetypes and iterates. Read-only requirements
		// mirror the health processor's own query so the same chunks are visited.
		FMassEntityQuery Query;
		Query.AddRequirement<FEntityMovementFragment>(EMassFragmentAccess::ReadOnly);
		Query.AddRequirement<FAgentEgressTenabilityFragment>(EMassFragmentAccess::ReadOnly);

		FMassExecutionContext QueryContext = EntityManager.CreateExecutionContext(0.0f);
		Query.ForEachEntityChunk(EntityManager, QueryContext, [&Out](FMassExecutionContext& Ctx)
		{
			const TConstArrayView<FEntityMovementFragment> Move = Ctx.GetFragmentView<FEntityMovementFragment>();
			const TConstArrayView<FAgentEgressTenabilityFragment> Ten = Ctx.GetFragmentView<FAgentEgressTenabilityFragment>();
			for (int32 i = 0; i < Ctx.GetNumEntities(); ++i)
			{
				FAgentTenabilitySnapshot S;
				S.EntityID = Move[i].EntityID;
				S.bTenabilityFailed = Ten[i].bTenabilityFailed;
				S.bIsDead = Ten[i].bIsDead;
				S.FirstFailureTime = Ten[i].FirstFailureTimeSeconds;
				S.DeathTime = Ten[i].DeathTimeSeconds;
				S.AccumulatedToxicFED = Ten[i].AccumulatedToxicFED;
				S.AccumulatedThermalFED = Ten[i].AccumulatedThermalFED;
				S.DisplayRisk = Ten[i].DisplayRisk;
				S.DominantCriterion = Ten[i].CurrentDominantCriterion;
				S.FirstFailureCriterion = Ten[i].FirstFailureCriterion;
				S.MovementBracket = Move[i].CurrentMovementBracket;
				Out.Add(S);
			}
		});
		Out.Sort([](const FAgentTenabilitySnapshot& A, const FAgentTenabilitySnapshot& B)
		{
			return A.EntityID < B.EntityID;
		});
	}

	const FAgentTenabilitySnapshot* FindByEntity(const TArray<FAgentTenabilitySnapshot>& Snap, const int32 EntityID)
	{
		return Snap.FindByPredicate([EntityID](const FAgentTenabilitySnapshot& S) { return S.EntityID == EntityID; });
	}

	// Subsystem accessors -------------------------------------------------------------------------
	UProjectMobiusGameInstance* GetGameInstance(UWorld* World)
	{
		return World ? Cast<UProjectMobiusGameInstance>(World->GetGameInstance()) : nullptr;
	}
	UTimeDilationSubSystem* GetTimeSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UTimeDilationSubSystem>() : nullptr;
	}
	UBRiskEgressSubsystem* GetEgressSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UBRiskEgressSubsystem>() : nullptr;
	}
	UBRiskDataSubsystem* GetBRiskDataSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UBRiskDataSubsystem>() : nullptr;
	}

	// -----------------------------------------------------------------------------------------------
	// Latent commands (all bounded by tick counts / deadlines — NO wall-clock sleeps).
	// -----------------------------------------------------------------------------------------------

	/** Wait until the agent import has cached AgentCount entities (durable "load done" signal), OR
	 *  a tick-bounded deadline. */
	class FWaitForAgentsLoadedCommand : public IAutomationLatentCommand
	{
	public:
		FWaitForAgentsLoadedCommand(FAutomationTestBase& InTest, int32 InExpectedAgents, int32 InMaxTicks)
			: Test(InTest), ExpectedAgents(InExpectedAgents), MaxTicks(InMaxTicks) {}
		virtual bool Update() override
		{
			if (UWorld* World = GetActiveGameWorld())
			{
				const UAgentDataSubsystem* AgentData = World->GetSubsystem<UAgentDataSubsystem>();
				if (AgentData && AgentData->CachedEntityData.Num() == ExpectedAgents)
				{
					return true;
				}
			}
			if (++Ticks >= MaxTicks)
			{
				Test.AddError(FString::Printf(TEXT("timed out waiting for %d agents to load (%d ticks)"), ExpectedAgents, MaxTicks));
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase& Test;
		int32 ExpectedAgents;
		int32 MaxTicks;
		int32 Ticks = 0;
	};

	/** Wait until AreAgentTimelinesCurrent() is true, OR a tick-bounded deadline. This is the exact
	 *  "recompute finished, safe to read" gate the health processor uses. */
	class FWaitForTimelinesCurrentCommand : public IAutomationLatentCommand
	{
	public:
		FWaitForTimelinesCurrentCommand(FAutomationTestBase& InTest, int32 InMaxTicks, const FString& InLabel)
			: Test(InTest), MaxTicks(InMaxTicks), Label(InLabel) {}
		virtual bool Update() override
		{
			if (const UBRiskEgressSubsystem* Egress = GetEgressSubsystem(GetActiveGameWorld()))
			{
				if (Egress->AreAgentTimelinesCurrent())
				{
					return true;
				}
			}
			if (++Ticks >= MaxTicks)
			{
				Test.AddError(FString::Printf(TEXT("[%s] timed out (%d ticks) waiting for AreAgentTimelinesCurrent()"), *Label, MaxTicks));
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase& Test;
		int32 MaxTicks;
		FString Label;
		int32 Ticks = 0;
	};

	/** Scrub the playhead and let processors settle N frames at the new position.
	 *  PreviouslyPaused=1 (stay paused): OverrideCurrentTime pauses first and, because we pass 1, does
	 *  NOT unpause — so CurrentSimulationTime HOLDS at TargetTime across the settle frames. This is
	 *  essential for the exact dose-equality assertions: the health processor projects DoseAt(current
	 *  time) every frame regardless of pause, so with time held the fragment value at TargetTime is
	 *  stable and a fresh direct DoseAt(TargetTime) matches it bitwise. (Playing UNpaused would advance
	 *  the clock every frame and defeat the exact comparison.) Navigation independence is still proven:
	 *  dose is a closed-form timeline lookup, so it does not matter that we jumped straight to TargetTime
	 *  rather than ticking through the intermediate times. */
	class FScrubAndSettleCommand : public IAutomationLatentCommand
	{
	public:
		FScrubAndSettleCommand(FAutomationTestBase& InTest, float InTime, int32 InSettleTicks)
			: Test(InTest), TargetTime(InTime), SettleTicks(InSettleTicks) {}
		virtual bool Update() override
		{
			if (!bScrubbed)
			{
				UTimeDilationSubSystem* TimeSub = GetTimeSubsystem(GetActiveGameWorld());
				if (!TimeSub)
				{
					Test.AddError(TEXT("TimeDilation subsystem missing during scrub"));
					return true;
				}
				TimeSub->OverrideCurrentTime(TargetTime, /*PreviouslyPaused*/ 1);
				bScrubbed = true;
				return false;
			}
			return ++Ticks >= SettleTicks;
		}
	private:
		FAutomationTestBase& Test;
		float TargetTime;
		int32 SettleTicks;
		bool bScrubbed = false;
		int32 Ticks = 0;
	};

	/** Run an arbitrary assertion functor once, then complete. */
	class FRunAssertionCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRunAssertionCommand(TFunction<void()> InFn) : Fn(MoveTemp(InFn)) {}
		virtual bool Update() override { if (Fn) { Fn(); } return true; }
	private:
		TFunction<void()> Fn;
	};

	/** Set an agent file (bumps DataGeneration) and settle one frame so the change is observable. */
	class FSetAgentFileCommand : public IAutomationLatentCommand
	{
	public:
		FSetAgentFileCommand(FAutomationTestBase& InTest, FString InPath) : Test(InTest), Path(MoveTemp(InPath)) {}
		virtual bool Update() override
		{
			if (UProjectMobiusGameInstance* GI = GetGameInstance(GetActiveGameWorld()))
			{
				GI->SetPedestrianDataFilePath(Path);
			}
			else
			{
				Test.AddError(TEXT("Mobius game instance missing during agent-file set"));
			}
			return true;
		}
	private:
		FAutomationTestBase& Test;
		FString Path;
	};

	/** Set a B-RISK .smv path (broadcasts OnBRiskFileChanged -> async LoadScenarioFromSmv). */
	class FSetBRiskFileCommand : public IAutomationLatentCommand
	{
	public:
		FSetBRiskFileCommand(FAutomationTestBase& InTest, FString InPath) : Test(InTest), Path(MoveTemp(InPath)) {}
		virtual bool Update() override
		{
			if (UProjectMobiusGameInstance* GI = GetGameInstance(GetActiveGameWorld()))
			{
				GI->SetBRiskSmvFilePath(Path);
			}
			else
			{
				Test.AddError(TEXT("Mobius game instance missing during B-RISK file set"));
			}
			return true;
		}
	private:
		FAutomationTestBase& Test;
		FString Path;
	};

	/** Wait until the B-RISK scenario has tenability data (async load committed), OR deadline. */
	class FWaitForBRiskLoadedCommand : public IAutomationLatentCommand
	{
	public:
		FWaitForBRiskLoadedCommand(FAutomationTestBase& InTest, int32 InMaxTicks) : Test(InTest), MaxTicks(InMaxTicks) {}
		virtual bool Update() override
		{
			if (const UBRiskDataSubsystem* Data = GetBRiskDataSubsystem(GetActiveGameWorld()))
			{
				if (Data->HasTenabilityData() && !Data->IsLoading())
				{
					return true;
				}
			}
			if (++Ticks >= MaxTicks)
			{
				Test.AddError(TEXT("timed out waiting for the B-RISK scenario to load (HasTenabilityData)"));
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase& Test;
		int32 MaxTicks;
		int32 Ticks = 0;
	};

	/** Clear the active B-RISK scenario (drops timelines, tenability inert). */
	class FClearBRiskCommand : public IAutomationLatentCommand
	{
	public:
		explicit FClearBRiskCommand(FAutomationTestBase& InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			if (UBRiskDataSubsystem* Data = GetBRiskDataSubsystem(GetActiveGameWorld()))
			{
				Data->ClearScenario();
			}
			else
			{
				Test.AddError(TEXT("B-RISK data subsystem missing during clear"));
			}
			return true;
		}
	private:
		FAutomationTestBase& Test;
	};

	// Shared mutable state between latent commands (snapshots captured across the sequence). Held in a
	// TSharedRef so each latent command's assertion closure can capture and mutate it safely.
	struct FScrubTestState
	{
		TArray<FAgentTenabilitySnapshot> SnapshotAtT1;      // scenario 1: forward first pass
		TArray<FAgentTenabilitySnapshot> SnapshotAtT0;      // scenario 1: scrubbed back (mid-exposure)
		TArray<FAgentTenabilitySnapshot> SnapshotReplayT1;  // scenario 1: replayed forward
	};
}

// ===================================================================================================
// Mobius.InGame.TenabilityScrubReplay
// ===================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FInGameTenabilityScrubReplayTest,
	"Mobius.InGame.TenabilityScrubReplay",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FInGameTenabilityScrubReplayTest::RunTest(const FString& Parameters)
{
	UWorld* World = GetActiveGameWorld();
	if (!World)
	{
		AddError(TEXT("no game world - run via RunTests.ps1 -InGame (UnrealEditor-Cmd -game)"));
		return false;
	}
	UProjectMobiusGameInstance* GameInstance = GetGameInstance(World);
	TestNotNull(TEXT("Mobius game instance"), GameInstance);
	if (!GameInstance)
	{
		return false;
	}

	// --- Fixtures on disk ------------------------------------------------------------------------
	const FString AgentFileA = FPaths::Combine(FixtureRoot(), TEXT("agents_A.json"));
	const FString AgentFileB = FPaths::Combine(FixtureRoot(), TEXT("agents_B.json"));
	TestTrue(TEXT("agent fixture A written"), WriteText(AgentFileA, MakeAgentJson(AgentCount, RoomFixedJsonX)));
	// Agent set B: one fewer agent + a different X base -> a genuinely different DataGeneration payload.
	TestTrue(TEXT("agent fixture B written"), WriteText(AgentFileB, MakeAgentJson(AgentCount - 1, RoomFixedJsonX + 1.0f)));

	const FString BRiskSlow = WriteBRiskScenario(TEXT("brisk_slow"), MakeOutputXmlSlow());
	const FString BRiskFast = WriteBRiskScenario(TEXT("brisk_fast"), MakeOutputXmlFast());

	TSharedRef<FScrubTestState> State = MakeShared<FScrubTestState>();

	// =============================================================================================
	// SCENARIO 1: navigation independence (MID-EXPOSURE), + frozen-agent freeing on scrub-back.
	// =============================================================================================
	// Load agent set A + the "slow" B-RISK curve, then wait for BOTH datasets and the timeline build.
	GameInstance->SetBRiskSmvFilePath(BRiskSlow);
	GameInstance->SetPedestrianDataFilePath(AgentFileA);

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForAgentsLoadedCommand(*this, AgentCount, /*MaxTicks*/ 6000));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitForBRiskLoadedCommand(*this, /*MaxTicks*/ 6000));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitForTimelinesCurrentCommand(*this, MaxTimelineBuildTicks, TEXT("S1 initial")));

	// Play forward to T1 (past the ~65 s visibility failure); capture snapshot A.
	ADD_LATENT_AUTOMATION_COMMAND(FScrubAndSettleCommand(*this, T1, SettleTicks));
	ADD_LATENT_AUTOMATION_COMMAND(FRunAssertionCommand([this, State]()
	{
		SnapshotAgents(GetActiveGameWorld(), State->SnapshotAtT1);
		TestEqual(TEXT("S1: AgentCount entities present at T1"), State->SnapshotAtT1.Num(), AgentCount);

		int32 FailedCount = 0;
		for (const FAgentTenabilitySnapshot& S : State->SnapshotAtT1)
		{
			if (S.bTenabilityFailed) { ++FailedCount; }
			// Fixture sanity: failure is the fixture-A visibility crossing (~65 s), by the visibility criterion.
			if (S.bTenabilityFailed)
			{
				TestTrue(TEXT("S1: first-failure time is the fixture-A visibility crossing (~65 s)"),
					FMath::IsNearlyEqual(S.FirstFailureTime, FixtureA_VisFailTime, FailTimeTolerance));
				TestEqual(TEXT("S1: failure criterion is Visibility"),
					static_cast<uint8>(S.FirstFailureCriterion), static_cast<uint8>(ETenabilityCriterion::Visibility));
				TestEqual(TEXT("S1: DisplayRisk locked to 1.0 after failure"), S.DisplayRisk, 1.0f, 1e-3f);
			}
		}
		// Otherwise the test proves nothing (v1 fixture-sanity requirement).
		TestTrue(TEXT("S1: at least one agent has failed by T1 (fixture sanity)"), FailedCount >= 1);
	}));

	// Scrub BACK to T0 (mid-exposure: dose nonzero, but before the 65 s failure). Capture snapshot.
	ADD_LATENT_AUTOMATION_COMMAND(FScrubAndSettleCommand(*this, T0, SettleTicks));
	ADD_LATENT_AUTOMATION_COMMAND(FRunAssertionCommand([this, State]()
	{
		SnapshotAgents(GetActiveGameWorld(), State->SnapshotAtT0);
		TestEqual(TEXT("S1: AgentCount entities present at T0"), State->SnapshotAtT0.Num(), AgentCount);

		int32 MovingCount = 0;
		for (const FAgentTenabilitySnapshot& S : State->SnapshotAtT0)
		{
			// FROZEN-AGENT FREEING: T0 (30 s) is before the failure time (65 s), so failure state must
			// be projected off. Unlike the old latch design, DeathTimeSeconds is NOT re-armed to -1 on
			// scrub: it is immutable precomputed data (the agent's known failure time, the ASET datum),
			// and the movement-processor freeze gates on CurrentSimTime >= DeathTimeSeconds itself.
			// "Freed" therefore means: not failed-by-now, and any recorded failure lies strictly in
			// the FUTURE of the playhead.
			TestFalse(TEXT("S1@T0: not failed (scrubbed before failure time)"), S.bTenabilityFailed);
			TestFalse(TEXT("S1@T0: not dead"), S.bIsDead);
			TestTrue(TEXT("S1@T0: recorded failure time is in the playhead's future (agent freed, not frozen)"),
				S.DeathTime < 0.0f || S.DeathTime > T0 + UE_KINDA_SMALL_NUMBER);
			// The precomputed failure time itself must still be the fixture crossing — visible
			// pre-failure as data, never cleared by navigation.
			if (S.DeathTime >= 0.0f)
			{
				TestTrue(TEXT("S1@T0: precomputed DeathTimeSeconds equals the fixture-A visibility crossing (~65 s)"),
					FMath::IsNearlyEqual(S.DeathTime, FixtureA_VisFailTime, FailTimeTolerance));
			}
			// Dose is nonzero mid-exposure on this pass (toxic FEDSum(30) ~= 0.075): the case v1 masked.
			TestTrue(TEXT("S1@T0: toxic dose nonzero mid-exposure"), S.AccumulatedToxicFED > 1e-4f);
			if (S.MovementBracket != EPedestrianMovementBracket::Emb_NotMoving)
			{
				++MovingCount;
			}
		}
		// Plan's explicit frozen check via CurrentMovementBracket: the freeze forces Emb_NotMoving on
		// EVERY failed agent, so if the scrub-back failed to free them all agents would read
		// Emb_NotMoving. The oscillating fixture keeps agents above the walking-speed floor at T0, so a
		// correctly-freed set shows a moving gait bracket. Counted (not per-agent asserted) so a single
		// agent momentarily at an oscillation turning point can't false-fail — the meaningful claim is
		// that agents are NOT all clamped to Emb_NotMoving by a stale freeze.
		TestTrue(TEXT("S1@T0: agents freed on scrub-back are moving (CurrentMovementBracket != Emb_NotMoving)"),
			MovingCount >= 1);

		// Navigation independence at T0 vs a FRESH direct query at T0 through the SAME closed-form path
		// the processor uses (FindCurrentAgentTimeline + DoseAt). Same t -> same dose, no matter how
		// playback reached it. Exact equality (1e-6; the only slack is float projection).
		const UBRiskEgressSubsystem* Egress = GetEgressSubsystem(GetActiveGameWorld());
		TestNotNull(TEXT("S1@T0: egress subsystem present"), Egress);
		if (Egress)
		{
			for (const FAgentTenabilitySnapshot& S : State->SnapshotAtT0)
			{
				const UE::Mobius::Tenability::FAgentTenabilityTimeline* TL =
					Egress->FindCurrentAgentTimeline(S.EntityID);
				TestNotNull(TEXT("S1@T0: timeline present for agent"), TL);
				if (TL)
				{
					float DirectToxic = 0.0f, DirectThermal = 0.0f;
					auto RoomSampler = [Egress](int32 RoomIndex, double Time, double& OutToxic, double& OutThermal)
					{
						Egress->SampleTenabilityDoseAtRoomIndex(RoomIndex, Time, OutToxic, OutThermal);
					};
					UE::Mobius::Tenability::FRoomFEDSampler SamplerRef(RoomSampler);
					TL->DoseAt(T0, SamplerRef, DirectToxic, DirectThermal);
					TestEqual(TEXT("S1@T0: fragment dose == fresh direct DoseAt(T0) (navigation-independent)"),
						S.AccumulatedToxicFED, DirectToxic, DoseExactTolerance);
				}
			}
		}
	}));

	// Replay FORWARD to T1 again; capture snapshot B and assert EXACT equality with the first pass.
	ADD_LATENT_AUTOMATION_COMMAND(FScrubAndSettleCommand(*this, T1, SettleTicks));
	ADD_LATENT_AUTOMATION_COMMAND(FRunAssertionCommand([this, State]()
	{
		SnapshotAgents(GetActiveGameWorld(), State->SnapshotReplayT1);
		TestEqual(TEXT("S1: AgentCount entities present at replay T1"), State->SnapshotReplayT1.Num(), AgentCount);

		for (const FAgentTenabilitySnapshot& B : State->SnapshotReplayT1)
		{
			const FAgentTenabilitySnapshot* A = FindByEntity(State->SnapshotAtT1, B.EntityID);
			TestNotNull(TEXT("S1: replay entity present in first pass"), A);
			if (!A)
			{
				continue;
			}
			// EXACT dose equality: same closed-form lookup at the same time, 1e-6 (float projection only).
			TestEqual(TEXT("S1: toxic dose EXACTLY equal across replay (dose_A == dose_B)"),
				B.AccumulatedToxicFED, A->AccumulatedToxicFED, DoseExactTolerance);
			TestEqual(TEXT("S1: thermal dose EXACTLY equal across replay"),
				B.AccumulatedThermalFED, A->AccumulatedThermalFED, DoseExactTolerance);
			// Failure state identical (times from precomputed Layer-2, no runtime re-derivation).
			TestEqual(TEXT("S1: bTenabilityFailed identical across replay"), B.bTenabilityFailed, A->bTenabilityFailed);
			TestEqual(TEXT("S1: FirstFailureTime EXACTLY equal across replay"),
				B.FirstFailureTime, A->FirstFailureTime, DoseExactTolerance);
			TestEqual(TEXT("S1: FirstFailureCriterion identical across replay"),
				static_cast<uint8>(B.FirstFailureCriterion), static_cast<uint8>(A->FirstFailureCriterion));
		}
	}));

	// =============================================================================================
	// SCENARIO 2: agent-file swap recompute (correct new timeline; no-data during the stale window).
	// =============================================================================================
	// Swap to agent set B. This bumps DataGeneration -> AreAgentTimelinesCurrent() goes false until the
	// rebuild lands. During that stale window the health processor must render the no-data state
	// (DisplayRisk 0, CurrentDominantCriterion None) — never numbers from the OLD (A) pair.
	ADD_LATENT_AUTOMATION_COMMAND(FSetAgentFileCommand(*this, AgentFileB));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitForAgentsLoadedCommand(*this, AgentCount - 1, /*MaxTicks*/ 6000));
	// Immediately after the new agents cache but BEFORE timelines are current: assert no-data.
	ADD_LATENT_AUTOMATION_COMMAND(FRunAssertionCommand([this]()
	{
		const UBRiskEgressSubsystem* Egress = GetEgressSubsystem(GetActiveGameWorld());
		if (Egress && !Egress->AreAgentTimelinesCurrent())
		{
			TArray<FAgentTenabilitySnapshot> Stale;
			SnapshotAgents(GetActiveGameWorld(), Stale);
			for (const FAgentTenabilitySnapshot& S : Stale)
			{
				// Stale window: no-data state only (invariant 2 — never a mismatched-triple value).
				TestEqual(TEXT("S2 stale window: DisplayRisk is the no-data 0"), S.DisplayRisk, 0.0f, 1e-4f);
				TestEqual(TEXT("S2 stale window: dominant criterion is None"),
					static_cast<uint8>(S.DominantCriterion), static_cast<uint8>(ETenabilityCriterion::None));
				TestFalse(TEXT("S2 stale window: not marked failed from the old pair"), S.bTenabilityFailed);
			}
		}
		// If timelines already re-built within one frame, the stale window is not observable here — the
		// steady-state assertion below still validates the recompute. (No error: a fast rebuild is fine.)
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitForTimelinesCurrentCommand(*this, MaxTimelineBuildTicks, TEXT("S2 rebuild")));
	ADD_LATENT_AUTOMATION_COMMAND(FScrubAndSettleCommand(*this, T1, SettleTicks));
	ADD_LATENT_AUTOMATION_COMMAND(FRunAssertionCommand([this]()
	{
		// New timelines keyed to the new agent set (fewer agents), still failing on the SAME slow curve.
		TArray<FAgentTenabilitySnapshot> Snap;
		SnapshotAgents(GetActiveGameWorld(), Snap);
		TestEqual(TEXT("S2: entity count follows the swapped agent file"), Snap.Num(), AgentCount - 1);
		int32 FailedCount = 0;
		for (const FAgentTenabilitySnapshot& S : Snap)
		{
			if (S.bTenabilityFailed)
			{
				++FailedCount;
				TestTrue(TEXT("S2: new set fails at the fixture-A visibility crossing (~65 s)"),
					FMath::IsNearlyEqual(S.FirstFailureTime, FixtureA_VisFailTime, FailTimeTolerance));
			}
		}
		TestTrue(TEXT("S2: swapped agent set recomputed and failed (>=1)"), FailedCount >= 1);
	}));

	// =============================================================================================
	// SCENARIO 3: B-RISK file swap recompute -> the NEW curve's golden crossing.
	// =============================================================================================
	// Swap to the "fast" B-RISK scenario (visibility crosses at ~25 s, not ~65 s). Bumps
	// ScenarioGeneration -> rebuild. Assert the failure time follows the NEW curve.
	ADD_LATENT_AUTOMATION_COMMAND(FSetBRiskFileCommand(*this, BRiskFast));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitForBRiskLoadedCommand(*this, /*MaxTicks*/ 6000));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitForTimelinesCurrentCommand(*this, MaxTimelineBuildTicks, TEXT("S3 rebuild")));
	ADD_LATENT_AUTOMATION_COMMAND(FScrubAndSettleCommand(*this, T1, SettleTicks));
	ADD_LATENT_AUTOMATION_COMMAND(FRunAssertionCommand([this]()
	{
		TArray<FAgentTenabilitySnapshot> Snap;
		SnapshotAgents(GetActiveGameWorld(), Snap);
		int32 FailedCount = 0;
		for (const FAgentTenabilitySnapshot& S : Snap)
		{
			if (S.bTenabilityFailed)
			{
				++FailedCount;
				// NEW curve's golden crossing (~25 s), NOT the old ~65 s.
				TestTrue(TEXT("S3: failure time follows the NEW (fast) curve's ~25 s crossing"),
					FMath::IsNearlyEqual(S.FirstFailureTime, FixtureB_VisFailTime, FailTimeTolerance));
				TestFalse(TEXT("S3: failure time is NOT the old fixture-A ~65 s crossing"),
					FMath::IsNearlyEqual(S.FirstFailureTime, FixtureA_VisFailTime, FailTimeTolerance));
			}
		}
		TestTrue(TEXT("S3: B-RISK swap recomputed and failed on the new curve (>=1)"), FailedCount >= 1);
	}));

	// =============================================================================================
	// SCENARIO 4: B-RISK cleared -> timelines dropped, tenability inert, no crash on playback.
	// =============================================================================================
	ADD_LATENT_AUTOMATION_COMMAND(FClearBRiskCommand(*this));
	// A couple of frames of continued playback after the clear (the "no crash on continued playback").
	ADD_LATENT_AUTOMATION_COMMAND(FScrubAndSettleCommand(*this, T0, SettleTicks));
	ADD_LATENT_AUTOMATION_COMMAND(FRunAssertionCommand([this]()
	{
		UWorld* CurWorld = GetActiveGameWorld();
		TestTrue(TEXT("S4: world alive after B-RISK clear + playback"), CurWorld != nullptr);
		const UBRiskEgressSubsystem* Egress = GetEgressSubsystem(CurWorld);
		TestNotNull(TEXT("S4: egress subsystem present"), Egress);
		if (Egress)
		{
			// Cleared scenario -> sentinel key -> timelines are not current and none can be handed out.
			TestFalse(TEXT("S4: timelines not current after clear"), Egress->AreAgentTimelinesCurrent());
			// Tenability inert: every agent shows the no-data state, nothing failed/frozen.
			TArray<FAgentTenabilitySnapshot> Snap;
			SnapshotAgents(CurWorld, Snap);
			for (const FAgentTenabilitySnapshot& S : Snap)
			{
				TestEqual(TEXT("S4: DisplayRisk inert (0) after clear"), S.DisplayRisk, 0.0f, 1e-4f);
				TestFalse(TEXT("S4: not failed after clear"), S.bTenabilityFailed);
				TestFalse(TEXT("S4: not dead after clear"), S.bIsDead);
				TestTrue(TEXT("S4: DeathTimeSeconds re-armed (<0) after clear"), S.DeathTime < 0.0f);
			}
		}
	}));

	// --- Cleanup (best-effort) -------------------------------------------------------------------
	ADD_LATENT_AUTOMATION_COMMAND(FRunAssertionCommand([AgentFileA, AgentFileB]()
	{
		IFileManager& FM = IFileManager::Get();
		FM.Delete(*AgentFileA, false, true);
		FM.Delete(*AgentFileB, false, true);
		FM.DeleteDirectory(*FixtureRoot(), false, true);
	}));

	return true;
}

#endif // !UE_BUILD_SHIPPING
