// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// MovementMapCacheInvalidationTest.cpp
//
// Guards the B2 per-timestep map cache in UPedestrianMovementProcessor. The processor caches its
// EntityID->sample-index lookup maps across frames and rebuilds them only when the data window
// changes, using the pure predicate ShouldRebuildSampleIndexMaps(cachedGen, cachedStep, curGen, curStep).
//
// THE HAZARD this proves is handled: the processor is persistent (not recreated on a file switch).
// On switch, UTimeDilationSubSystem::FileChanging() resets CurrentTimeStep to 0 while the spawn
// subsystem builds a NEW shared FSimulationFragment. So a switch that lands back on timestep 0 yields
// curStep == cachedStep (0 == 0): a timestep-ALONE key would skip the rebuild and reuse the previous
// file's map against the new (possibly shorter) sample array -> out-of-bounds read / wrong-agent pose.
// The composite (generation, timestep) key must force a rebuild because the generation differs.
//
// This is the PRD's lighter unit form of B2's file-switch regression test (pure predicate, no MASS
// context). The full per-agent CurrentLocation/bRenderAgent diff == 0 A/B and `stat mass` delta are
// verified at runtime in the live editor against a real dataset.
//
// Run from the Session Frontend (search "ProjectMobius.MassAI") or:
//   UnrealEditor ProjectMobius.uproject
//     -ExecCmds="Automation RunTests ProjectMobius.MassAI.MovementProcessor" -log
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "MassAI/MassProcessor/PedestrianMovementProcessor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMovementMapCacheInvalidationTest,
	"ProjectMobius.MassAI.MovementProcessor.MapCacheInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMovementMapCacheInvalidationTest::RunTest(const FString& Parameters)
{
	using Proc = UPedestrianMovementProcessor;

	// First Execute after construction: caches start (gen 0, INDEX_NONE); real generations start at 1.
	// The first compare must rebuild.
	TestTrue(TEXT("First use rebuilds (0/INDEX_NONE vs first real generation)"),
		Proc::ShouldRebuildSampleIndexMaps(/*cachedGen*/ 0, /*cachedStep*/ INDEX_NONE, /*curGen*/ 1, /*curStep*/ 0));

	// Same window: the within-frame cross-chunk skip and the cross-frame (timestep-held) skip.
	TestFalse(TEXT("Unchanged generation and timestep does NOT rebuild (the optimisation)"),
		Proc::ShouldRebuildSampleIndexMaps(1, 5, 1, 5));

	// Normal playback advance: same data, next timestep -> rebuild.
	TestTrue(TEXT("Same generation, advanced timestep rebuilds"),
		Proc::ShouldRebuildSampleIndexMaps(1, 5, 1, 6));

	// THE regression case: file switch that lands back on timestep 0. Timestep is identical (0 == 0),
	// only the generation differs. A timestep-alone key would return false here (the OOB-read bug);
	// the composite key MUST return true.
	TestTrue(TEXT("File switch back to same timestep rebuilds (generation differs) — the OOB guard"),
		Proc::ShouldRebuildSampleIndexMaps(/*cachedGen*/ 1, /*cachedStep*/ 0, /*curGen*/ 2, /*curStep*/ 0));

	// Both differ (switch + different timestep) -> rebuild.
	TestTrue(TEXT("Different generation and timestep rebuilds"),
		Proc::ShouldRebuildSampleIndexMaps(1, 5, 2, 0));

	return true;
}

#endif // !UE_BUILD_SHIPPING
