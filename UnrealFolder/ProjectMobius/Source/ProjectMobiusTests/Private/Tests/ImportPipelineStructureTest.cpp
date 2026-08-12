// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// ImportPipelineStructureTest.cpp
//
// PRD 02 task T5 — asserts the exact structures the REAL import runnable builds from a known
// fixture, end-to-end: FSimulationFragment map contents (unit scaling m->cm, Y-invert,
// yaw = -rot - 90), NumOfAgentsPerTimeStep, MaxAgents/TimeBetweenSteps/MaxTime, entity metadata,
// interned ModeIndex, movement brackets — and then the same structures again through the A6
// fast-reload path, which must be bit-identical to the full parse (the runnable-level half of
// A6's golden equality; the reader-level half is SimCacheCorrectnessTest).
//
// Uses the runnable's bAutoStartThread=false test seam: Run() executes synchronously on the
// automation thread, so results are inspected deterministically with no polling.
//
// Run: MobiusPerf\RunTests.ps1 -Filter "ProjectMobius.SimData."
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "SimData/SimDiskCache.h"

namespace
{
	/**
	 * Deterministic fixture. Expected post-conversion values are computed with the SAME float
	 * expressions the gathering loop uses, so equality checks are exact:
	 *   Position = (X, -Y, Z) * 100 (SI JSON), Yaw = -rot - 90, Speed passthrough.
	 * Timesteps: ts0 = 2 samples, ts1 = 2, ts2 = empty, ts3 = 1 (trailing single sample).
	 * Entity 1 never moves -> its brackets must come out Emb_NotMoving.
	 */
	const TCHAR* GetStructureFixtureJson()
	{
		return TEXT(R"({
	"metadata": { "duration": 0.3, "sampling_rate": 0.1, "max_num_entities": 2, "isSI": true, "isDeg": true },
	"entities": [
		{ "id": 0, "name": "walker_a", "simTimeS": "0.3", "max_speed": 1.5, "m_plane": "floor_0", "map": 1 },
		{ "id": 1, "name": "walker_b", "simTimeS": 0.2, "max_speed": 2.0, "m_plane": "floor_1", "map": 0 }
	],
	"simulation": [
		{ "samples": [
			{ "entity": 0, "rotation": 45.0, "speed": 1.25, "mode": "walk", "position": { "x": 1.5, "y": 2.0, "z": 0.25 } },
			{ "entity": 1, "rotation": 0.0, "speed": 0.0, "mode": "walk", "position": { "x": 0.0, "y": -1.0, "z": 0.0 } }
		] },
		{ "samples": [
			{ "entity": 0, "rotation": 45.0, "speed": 1.25, "mode": "walk", "position": { "x": 1.6, "y": 2.0, "z": 0.25 } },
			{ "entity": 1, "rotation": 0.0, "speed": 0.0, "mode": "walk", "position": { "x": 0.0, "y": -1.0, "z": 0.0 } }
		] },
		{ "samples": [] },
		{ "samples": [
			{ "entity": 0, "rotation": 90.0, "speed": 1.3, "mode": "walk", "position": { "x": 1.7, "y": 2.0, "z": 0.25 } }
		] }
	]
})");
	}

	/** Mirror of the gathering loop's conversion, for exact expected values. */
	FVector ExpectedPosition(float X, float Y, float Z)
	{
		FVector Position(X, -Y, Z);
		Position *= 100.0f;
		return Position;
	}

	float ExpectedYaw(float SourceRotation)
	{
		return -SourceRotation - 90.0f;
	}

	uint64 DoubleBits(double Value)
	{
		uint64 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return Bits;
	}

	bool VectorsBitEqual(const FVector& A, const FVector& B)
	{
		return DoubleBits(A.X) == DoubleBits(B.X) && DoubleBits(A.Y) == DoubleBits(B.Y) && DoubleBits(A.Z) == DoubleBits(B.Z);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FImportPipelineStructureTest,
	"ProjectMobius.SimData.ImportPipelineStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FImportPipelineStructureTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusImportStructureTest"));
	FileManager.MakeDirectory(*Dir, /*Tree*/ true);
	const FString FixturePath = FPaths::Combine(Dir, TEXT("StructureFixture.json"));
	TestTrue(TEXT("fixture written"), FFileHelper::SaveStringToFile(
		GetStructureFixtureJson(), *FixturePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	// Pin the cache cvars for a deterministic first run (write on, reload on, no cache present).
	IConsoleVariable* WriteCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mobius.SimCache.WriteOnImport"));
	IConsoleVariable* FastReloadCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mobius.SimCache.FastReload"));
	const int32 SavedWrite = WriteCVar ? WriteCVar->GetInt() : 1;
	const int32 SavedFastReload = FastReloadCVar ? FastReloadCVar->GetInt() : 1;
	if (WriteCVar) { WriteCVar->Set(1, ECVF_SetByCode); }
	if (FastReloadCVar) { FastReloadCVar->Set(1, ECVF_SetByCode); }

	const uint64 SourceHash = MobiusSimCache::ComputeSourceHash(FixturePath);
	const FString CachePath = MobiusSimCache::MakeCacheFilePath(FixturePath, SourceHash);
	FileManager.Delete(*CachePath, /*RequireExists*/ false, /*EvenReadOnly*/ true);

	UAgentDataSubsystem* Owner = NewObject<UAgentDataSubsystem>();
	TestNotNull(TEXT("owner subsystem created"), Owner);

	// ---------- Run 1: full parse + convert (no cache exists yet) ----------
	FProcessAgentSimulationDataRunnable RunnableFull(FixturePath, Owner, /*bAutoStartThread*/ false);
	RunnableFull.Run();

	TestFalse(TEXT("full run parsed the source (not the cache)"), RunnableFull.ImportTimings.bUsedFastReload);
	TestTrue(TEXT("full run recorded a parse phase"), RunnableFull.ImportTimings.ParseSeconds > 0.0);
	TestEqual(TEXT("MaxAgents from metadata"), RunnableFull.MaxAgents, 2);
	TestEqual(TEXT("TimeBetweenSteps from sampling_rate"), RunnableFull.TimeBetweenSteps, 0.1f, 0.0f);
	TestEqual(TEXT("MaxTime from duration"), RunnableFull.AgentMovementInfoData.MaxTime, 0.3f, 0.0f);
	TestEqual(TEXT("file format"), static_cast<int32>(RunnableFull.AgentFileFormat), static_cast<int32>(EMobiusAgentFileFormat::Json));

	// Entities parsed intact.
	TestEqual(TEXT("entity count"), RunnableFull.AgentSimulationData.Entities.Num(), 2);
	if (RunnableFull.AgentSimulationData.Entities.Num() == 2)
	{
		const FMobiusAgentEntityData& E0 = RunnableFull.AgentSimulationData.Entities[0];
		TestEqual(TEXT("entity[0].Name"), E0.Name, FString(TEXT("walker_a")));
		TestEqual(TEXT("entity[0].SimTimeS (string form)"), E0.SimTimeS, 0.3f, 0.0f);
		TestEqual(TEXT("entity[0].Map"), E0.Map, 1);
		TestEqual(TEXT("entity[1].Name"), RunnableFull.AgentSimulationData.Entities[1].Name, FString(TEXT("walker_b")));
	}

	// Per-timestep structure.
	TestEqual(TEXT("NumOfAgentsPerTimeStep size"), RunnableFull.NumOfAgentsPerTimeStep.Num(), 4);
	if (RunnableFull.NumOfAgentsPerTimeStep.Num() == 4)
	{
		TestEqual(TEXT("counts[0]"), RunnableFull.NumOfAgentsPerTimeStep[0], 2);
		TestEqual(TEXT("counts[1]"), RunnableFull.NumOfAgentsPerTimeStep[1], 2);
		TestEqual(TEXT("counts[2] (empty timestep)"), RunnableFull.NumOfAgentsPerTimeStep[2], 0);
		TestEqual(TEXT("counts[3]"), RunnableFull.NumOfAgentsPerTimeStep[3], 1);
	}

	const TMap<int32, TArray<FSimMovementSample>>& FullMap = *RunnableFull.AgentMovementInfoData.SimulationData;
	TestEqual(TEXT("map timestep count"), FullMap.Num(), 4);

	const TArray<FSimMovementSample>* Ts0 = FullMap.Find(0);
	TestNotNull(TEXT("ts0 present"), Ts0);
	if (Ts0 && Ts0->Num() == 2)
	{
		const FSimMovementSample& S0 = (*Ts0)[0];
		TestEqual(TEXT("ts0[0].EntityID"), S0.EntityID, 0);
		TestTrue(TEXT("ts0[0] position = (150, -200, 25)"), VectorsBitEqual(S0.Position, ExpectedPosition(1.5f, 2.0f, 0.25f)));
		TestEqual(TEXT("ts0[0] yaw = -135"), S0.Rotation.Yaw, static_cast<double>(ExpectedYaw(45.0f)), 0.0);
		TestEqual(TEXT("ts0[0] pitch = 0"), S0.Rotation.Pitch, 0.0, 0.0);
		TestEqual(TEXT("ts0[0].Speed"), S0.Speed, 1.25f, 0.0f);
		TestEqual(TEXT("ts0[0].ModeIndex interned to 0 (A2 drops mode strings)"), static_cast<int32>(S0.ModeIndex), 0);

		const FSimMovementSample& S1 = (*Ts0)[1];
		TestEqual(TEXT("ts0[1].EntityID"), S1.EntityID, 1);
		TestTrue(TEXT("ts0[1] position Y-inverted = (0, 100, 0)"), VectorsBitEqual(S1.Position, ExpectedPosition(0.0f, -1.0f, 0.0f)));
		TestEqual(TEXT("ts0[1] yaw = -90"), S1.Rotation.Yaw, static_cast<double>(ExpectedYaw(0.0f)), 0.0);
		TestEqual(TEXT("ts0[1] stationary bracket"), static_cast<int32>(S1.MovementBracket), static_cast<int32>(EPedestrianMovementBracket::Emb_NotMoving));
	}
	else
	{
		AddError(TEXT("ts0 missing or wrong sample count"));
	}

	const TArray<FSimMovementSample>* Ts2 = FullMap.Find(2);
	TestNotNull(TEXT("ts2 present (empty)"), Ts2);
	if (Ts2)
	{
		TestEqual(TEXT("ts2 empty"), Ts2->Num(), 0);
	}

	const TArray<FSimMovementSample>* Ts3 = FullMap.Find(3);
	if (Ts3 && Ts3->Num() == 1)
	{
		TestTrue(TEXT("ts3[0] position = (170, -200, 25)"), VectorsBitEqual((*Ts3)[0].Position, ExpectedPosition(1.7f, 2.0f, 0.25f)));
		TestEqual(TEXT("ts3[0] yaw = -180"), (*Ts3)[0].Rotation.Yaw, static_cast<double>(ExpectedYaw(90.0f)), 0.0);
	}
	else
	{
		AddError(TEXT("ts3 missing or wrong sample count"));
	}

	// The full run must have produced the .msc for run 2 (A3 write-on-import).
	TestTrue(TEXT("full run wrote the cache"), FileManager.FileExists(*CachePath));

	// ---------- Run 2: fast-reload from the cache (A6) — bit-identical structures ----------
	FProcessAgentSimulationDataRunnable RunnableFast(FixturePath, Owner, /*bAutoStartThread*/ false);
	RunnableFast.Run();

	TestTrue(TEXT("fast run served the cache (A6 path taken)"), RunnableFast.ImportTimings.bUsedFastReload);
	TestEqual(TEXT("fast run recorded no parse phase"), RunnableFast.ImportTimings.ParseSeconds, 0.0, 0.0);
	TestEqual(TEXT("fast MaxAgents"), RunnableFast.MaxAgents, RunnableFull.MaxAgents);
	TestEqual(TEXT("fast TimeBetweenSteps"), RunnableFast.TimeBetweenSteps, RunnableFull.TimeBetweenSteps, 0.0f);
	TestEqual(TEXT("fast MaxTime"), RunnableFast.AgentMovementInfoData.MaxTime, RunnableFull.AgentMovementInfoData.MaxTime, 0.0f);
	TestEqual(TEXT("fast file format restored"), static_cast<int32>(RunnableFast.AgentFileFormat), static_cast<int32>(RunnableFull.AgentFileFormat));
	TestEqual(TEXT("fast entity count"), RunnableFast.AgentSimulationData.Entities.Num(), RunnableFull.AgentSimulationData.Entities.Num());
	TestTrue(TEXT("fast NumOfAgentsPerTimeStep identical"), RunnableFast.NumOfAgentsPerTimeStep == RunnableFull.NumOfAgentsPerTimeStep);

	const TMap<int32, TArray<FSimMovementSample>>& FastMap = *RunnableFast.AgentMovementInfoData.SimulationData;
	TestEqual(TEXT("fast map timestep count"), FastMap.Num(), FullMap.Num());
	for (const TPair<int32, TArray<FSimMovementSample>>& Pair : FullMap)
	{
		const TArray<FSimMovementSample>* FastSamples = FastMap.Find(Pair.Key);
		if (!FastSamples || FastSamples->Num() != Pair.Value.Num())
		{
			AddError(FString::Printf(TEXT("fast-reload ts %d missing or wrong count"), Pair.Key));
			continue;
		}
		for (int32 i = 0; i < Pair.Value.Num(); ++i)
		{
			const FSimMovementSample& A = Pair.Value[i];
			const FSimMovementSample& B = (*FastSamples)[i];
			const bool bEqual = A.EntityID == B.EntityID
				&& VectorsBitEqual(A.Position, B.Position)
				&& DoubleBits(A.Rotation.Pitch) == DoubleBits(B.Rotation.Pitch)
				&& DoubleBits(A.Rotation.Yaw) == DoubleBits(B.Rotation.Yaw)
				&& DoubleBits(A.Rotation.Roll) == DoubleBits(B.Rotation.Roll)
				&& A.Speed == B.Speed
				&& A.MovementBracket == B.MovementBracket
				&& A.ModeIndex == B.ModeIndex;
			if (!bEqual)
			{
				AddError(FString::Printf(TEXT("fast-reload ts %d sample %d differs from full parse (bitwise)"), Pair.Key, i));
			}
		}
	}

	if (WriteCVar) { WriteCVar->Set(SavedWrite, ECVF_SetByCode); }
	if (FastReloadCVar) { FastReloadCVar->Set(SavedFastReload, ECVF_SetByCode); }
	FileManager.Delete(*CachePath, false, true);
	FileManager.DeleteDirectory(*Dir, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

#endif // !UE_BUILD_SHIPPING
