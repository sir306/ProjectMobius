// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// MovementMapCacheInvalidationTest.cpp
//
// Guards the B2 per-timestep map cache in UPedestrianMovementProcessor. The processor caches its
// EntityID->sample-index lookup maps across frames and rebuilds them only when the data window
// changes, using the pure predicate ShouldRebuildSampleIndexMaps.
//
// HAZARD 1 (B2, file switch): the processor is persistent (not recreated on a file switch).
// On switch, UTimeDilationSubSystem::FileChanging() resets CurrentTimeStep to 0 while the spawn
// subsystem builds a NEW shared FSimulationFragment. So a switch that lands back on timestep 0 yields
// curStep == cachedStep (0 == 0): a timestep-ALONE key would skip the rebuild and reuse the previous
// file's map against the new (possibly shorter) sample array -> out-of-bounds read / wrong-agent pose.
// The composite (generation, timestep) key must force a rebuild because the generation differs.
//
// HAZARD 2 (A4, streaming served-content swap — crash found 2026-07-03): FStreamingProvider may serve
// DIFFERENT content for an unchanged (generation, timestep). A big scrub cold-misses and serves the
// one-tick cosmetic stand-in block; the exact block replaces it next frame when the async read lands
// (and the Next window can go nullptr -> block the same way). Maps built from the stand-in indexed
// into the exact block -> out-of-bounds. The served-block identities are therefore the key's third
// component and either one changing must force a rebuild.
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

	// Opaque block identities for the pointer components — only ever compared, never dereferenced,
	// so local dummies are exactly as good as real sample arrays.
	int32 BlockA = 0, BlockB = 0, BlockC = 0;
	const void* A = &BlockA;
	const void* B = &BlockB;
	const void* C = &BlockC;

	// First Execute after construction: caches start (gen 0, INDEX_NONE, null blocks); real generations
	// start at 1. The first compare must rebuild.
	TestTrue(TEXT("First use rebuilds (0/INDEX_NONE/null vs first real window)"),
		Proc::ShouldRebuildSampleIndexMaps(/*cachedGen*/ 0, /*cachedStep*/ INDEX_NONE, nullptr, nullptr,
		                                   /*curGen*/ 1, /*curStep*/ 0, A, B));

	// Same window: the within-frame cross-chunk skip and the cross-frame (timestep-held) skip.
	TestFalse(TEXT("Unchanged generation, timestep and served blocks do NOT rebuild (the optimisation)"),
		Proc::ShouldRebuildSampleIndexMaps(1, 5, A, B, 1, 5, A, B));

	// Normal playback advance: same data, next timestep -> rebuild.
	TestTrue(TEXT("Same generation, advanced timestep rebuilds"),
		Proc::ShouldRebuildSampleIndexMaps(1, 5, A, B, 1, 6, B, C));

	// B2 regression case: file switch that lands back on timestep 0. Timestep is identical (0 == 0),
	// only the generation differs. A timestep-alone key would return false here (the OOB-read bug);
	// the composite key MUST return true.
	TestTrue(TEXT("File switch back to same timestep rebuilds (generation differs) — the B2 OOB guard"),
		Proc::ShouldRebuildSampleIndexMaps(/*cachedGen*/ 1, /*cachedStep*/ 0, A, B,
		                                   /*curGen*/ 2, /*curStep*/ 0, A, B));

	// A4 regression case 1 (the 2026-07-03 big-skip crash): unchanged (generation, timestep) but the
	// Current served block swaps stand-in -> exact when the streaming provider's async read lands.
	// A (gen, ts)-only key returns false here and the maps index out of bounds; the block identity
	// component MUST force the rebuild.
	TestTrue(TEXT("Served Current block swap at same (gen, ts) rebuilds — the A4 streaming OOB guard"),
		Proc::ShouldRebuildSampleIndexMaps(1, 5, A, B, 1, 5, C, B));

	// A4 regression case 2: the Next window arriving (nullptr -> block) at the same (gen, ts) must
	// also rebuild — the interpolation branch starts reading the Next map only once it is non-null.
	TestTrue(TEXT("Next block arriving (nullptr -> block) at same (gen, ts) rebuilds"),
		Proc::ShouldRebuildSampleIndexMaps(1, 5, A, nullptr, 1, 5, A, B));

	// Both differ (switch + different timestep) -> rebuild.
	TestTrue(TEXT("Different generation and timestep rebuilds"),
		Proc::ShouldRebuildSampleIndexMaps(1, 5, A, B, 2, 0, C, nullptr));

	return true;
}

#endif // !UE_BUILD_SHIPPING
