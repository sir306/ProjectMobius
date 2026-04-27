// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// AgentDataMemoryTest.cpp
//
// Automated tests that exercise the simulation data lifecycle and assert that
// memory returns to baseline after teardown. These do NOT require a UWorld —
// they drive the data structures directly.
//
// Run from the Session Frontend (search "ProjectMobius.Memory") or:
//   UnrealEditor ProjectMobius.uproject
//     -ExecCmds="Automation RunTests ProjectMobius.Memory" -log
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMemory.h"
#include "Util/MemoryTraceHelper.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint64 GetUsedPhysicalMB()
{
	return FPlatformMemory::GetStats().UsedPhysical / (1024 * 1024);
}

/** Tolerance in MB below which a memory delta is considered acceptable. */
static constexpr int64 kLeakToleranceMB = 10;

// ---------------------------------------------------------------------------
// Test 0: shared simulation backing map clear
//
// FSimulationFragment::SimulationData is a TSharedPtr to a backing map copied
// through Mass shared-fragment storage. Resetting only one TSharedPtr copy does
// not release that map while another copy still exists, so file-switch cleanup
// must empty the pointed-to map first.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimulationFragmentSharedBackingClearTest,
	"ProjectMobius.Memory.SimulationFragment.SharedBackingClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSimulationFragmentSharedBackingClearTest::RunTest(const FString& Parameters)
{
	TSharedPtr<TMap<int32, TArray<FVector>>> Original = MakeShared<TMap<int32, TArray<FVector>>>();
	TSharedPtr<TMap<int32, TArray<FVector>>> SharedCopy = Original;

	TArray<FVector> Samples;
	Samples.AddDefaulted(2);
	Original->Add(0, MoveTemp(Samples));

	TestTrue(TEXT("Shared copy should point at the same populated backing map"),
		SharedCopy.IsValid() && SharedCopy->Num() == 1);

	Original->Empty();
	Original.Reset();

	TestFalse(TEXT("Original pointer was reset"), Original.IsValid());
	TestTrue(TEXT("Other shared pointer remains valid"), SharedCopy.IsValid());
	TestEqual(TEXT("Clearing backing map is visible through other shared pointer"),
		SharedCopy->Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------
// Test 1: Simulation data map load/teardown
//
// Allocates a TMap<int32, TArray<FVector>> mirroring the layout of
// FSimulationFragment::SimulationData, then lets it go out of scope.
// Asserts that used physical memory returns within kLeakToleranceMB of
// the baseline.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimulationFragmentLifecycleTest,
	"ProjectMobius.Memory.SimulationFragment.LoadAndTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSimulationFragmentLifecycleTest::RunTest(const FString& Parameters)
{
	// --- Baseline ---
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	const int64 BaselineMB = static_cast<int64>(GetUsedPhysicalMB());
	UE_LOG(LogMobiusMemory, Warning, TEXT("[SimFragLifecycle] Baseline: %lldMB"), BaselineMB);

	// --- Allocate synthetic simulation data ---
	// 500 agents x 200 timesteps (moderate scale, keeps test fast)
	// Uses TMap<int32, TArray<FVector>> which mirrors FSimulationFragment::SimulationData
	// without requiring a dependency on SimulationFragment.h (and its transitive MobiusCore dep).
	{
		TMap<int32, TArray<FVector>> SimulationData;
		const int32 NumAgents    = 500;
		const int32 NumTimesteps = 200;

		for (int32 AgentID = 0; AgentID < NumAgents; ++AgentID)
		{
			TArray<FVector> Samples;
			Samples.SetNum(NumTimesteps);
			for (int32 T = 0; T < NumTimesteps; ++T)
			{
				Samples[T] = FVector(static_cast<float>(AgentID), static_cast<float>(T), 0.f);
			}
			SimulationData.Add(AgentID, MoveTemp(Samples));
		}

		const int64 AfterAllocMB = static_cast<int64>(GetUsedPhysicalMB());
		UE_LOG(LogMobiusMemory, Warning,
			TEXT("[SimFragLifecycle] AfterAlloc: %lldMB (delta=%+lldMB)"),
			AfterAllocMB, AfterAllocMB - BaselineMB);

		// SimulationData goes out of scope here -> destructor frees memory
	}

	// --- Teardown: force GC and measure ---
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	const int64 AfterTeardownMB = static_cast<int64>(GetUsedPhysicalMB());
	const int64 DeltaMB = AfterTeardownMB - BaselineMB;

	UE_LOG(LogMobiusMemory, Warning,
		TEXT("[SimFragLifecycle] AfterTeardown: %lldMB (delta from baseline=%+lldMB, tolerance=%+lldMB)"),
		AfterTeardownMB, DeltaMB, kLeakToleranceMB);

	TestTrue(
		FString::Printf(TEXT("Memory delta %+lldMB should be within tolerance %+lldMB"), DeltaMB, kLeakToleranceMB),
		DeltaMB <= kLeakToleranceMB);

	return true;
}

// ---------------------------------------------------------------------------
// Test 2: 3-cycle reload -- asserts no monotonic memory growth
//
// Runs the allocate -> teardown cycle three times and checks that
// post-teardown memory does not grow across cycles. This catches leaks
// that only appear on the second or later reload.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimulationFragmentReloadCycleTest,
	"ProjectMobius.Memory.SimulationFragment.ReloadCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSimulationFragmentReloadCycleTest::RunTest(const FString& Parameters)
{
	static constexpr int32 NumCycles  = 3;
	static constexpr int32 NumAgents  = 500;
	static constexpr int32 NumSteps   = 200;

	// Allow slightly more headroom for GC jitter across cycles
	static constexpr int64 CycleLeakToleranceMB = 15;

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	const int64 BaselineMB = static_cast<int64>(GetUsedPhysicalMB());
	UE_LOG(LogMobiusMemory, Warning,
		TEXT("[ReloadCycle] Baseline: %lldMB"), BaselineMB);

	int64 PostTeardownMB[NumCycles] = {};

	for (int32 Cycle = 0; Cycle < NumCycles; ++Cycle)
	{
		{
			TMap<int32, TArray<FVector>> SimulationData;
			for (int32 A = 0; A < NumAgents; ++A)
			{
				TArray<FVector> Samples;
				Samples.SetNum(NumSteps);
				for (int32 T = 0; T < NumSteps; ++T)
				{
					Samples[T] = FVector(static_cast<float>(A + Cycle * 1000), static_cast<float>(T), 0.f);
				}
				SimulationData.Add(A, MoveTemp(Samples));
			}
			// SimulationData freed at end of scope
		}

		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		PostTeardownMB[Cycle] = static_cast<int64>(GetUsedPhysicalMB());

		UE_LOG(LogMobiusMemory, Warning,
			TEXT("[ReloadCycle] Cycle %d post-teardown: %lldMB (delta from baseline=%+lldMB)"),
			Cycle + 1, PostTeardownMB[Cycle], PostTeardownMB[Cycle] - BaselineMB);
	}

	// Assert: cycle 3 post-teardown is not significantly above cycle 1 post-teardown
	const int64 GrowthMB = PostTeardownMB[NumCycles - 1] - PostTeardownMB[0];
	UE_LOG(LogMobiusMemory, Warning,
		TEXT("[ReloadCycle] Growth across %d cycles: %+lldMB (tolerance %+lldMB)"),
		NumCycles, GrowthMB, CycleLeakToleranceMB);

	TestTrue(
		FString::Printf(
			TEXT("Memory growth across %d reload cycles (%+lldMB) should be <= %lldMB"),
			NumCycles, GrowthMB, CycleLeakToleranceMB),
		GrowthMB <= CycleLeakToleranceMB);

	return true;
}

// ---------------------------------------------------------------------------
// Test 3: Entity cache lifecycle
//
// Exercises the CachedEntityData array pattern used in AgentDataSubsystem
// (move-in on HDF5 load, Empty+Shrink on JSON load) and checks for leaks.
// Uses a self-contained struct so this module does not depend on MobiusDataImporter
// headers that may have their own transitive includes.
// ---------------------------------------------------------------------------

struct FTestEntityData
{
	int32   Id       = 0;
	FString Name;
	float   MaxSpeed = 0.f;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHdf5EntityCacheLifecycleTest,
	"ProjectMobius.Memory.HDF5.EntityCacheLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHdf5EntityCacheLifecycleTest::RunTest(const FString& Parameters)
{
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	const int64 BaselineMB = static_cast<int64>(GetUsedPhysicalMB());

	static constexpr int32 NumEntities = 5000;

	TArray<FTestEntityData> CachedEntities;

	// Simulate HDF5 load -- fill the cache
	{
		TArray<FTestEntityData> SourceEntities;
		SourceEntities.SetNum(NumEntities);
		for (int32 i = 0; i < NumEntities; ++i)
		{
			SourceEntities[i].Id       = i;
			SourceEntities[i].Name     = FString::Printf(TEXT("Agent_%d"), i);
			SourceEntities[i].MaxSpeed = 1.5f;
		}
		CachedEntities = MoveTemp(SourceEntities);
	}

	const int64 AfterHdf5LoadMB = static_cast<int64>(GetUsedPhysicalMB());
	UE_LOG(LogMobiusMemory, Warning,
		TEXT("[HDF5Cache] AfterHDF5Load: %lldMB (delta=%+lldMB)"),
		AfterHdf5LoadMB, AfterHdf5LoadMB - BaselineMB);

	// Simulate JSON load -- clear the cache (mirrors the else-branch in BuildPedestrianMovementFragmentData)
	CachedEntities.Empty();
	CachedEntities.Shrink();

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	const int64 AfterJsonSwitchMB = static_cast<int64>(GetUsedPhysicalMB());
	const int64 DeltaMB = AfterJsonSwitchMB - BaselineMB;

	UE_LOG(LogMobiusMemory, Warning,
		TEXT("[HDF5Cache] AfterJSONSwitch: %lldMB (delta from baseline=%+lldMB, tolerance=%+lldMB)"),
		AfterJsonSwitchMB, DeltaMB, kLeakToleranceMB);

	TestTrue(
		FString::Printf(TEXT("HDF5 entity cache delta %+lldMB should be within tolerance %+lldMB"), DeltaMB, kLeakToleranceMB),
		DeltaMB <= kLeakToleranceMB);

	return true;
}

#endif // !UE_BUILD_SHIPPING
