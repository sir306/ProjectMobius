// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.  
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,  
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL  
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR  
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING  
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS  
 * IN THE SOFTWARE.
 */

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h" // So we can use the FMassFragment
#include "EnumsAndStructs/VelocityVector2D.h"
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
#include "SimulationFragment.generated.h"

// Forward-declared so FSimulationFragment can hold a provider pointer without including the interface
// header (ISimSampleProvider.h itself includes this file for FSimMovementSample). Consumers that call
// through the provider include ISimSampleProvider.h for the full type. (Perf task A1.)
class ISimSampleProvider;

/**
 * One agent's playback sample at one timestep. Stored ~millions of times (NumTimesteps x NumAgents) in
 * FSimulationFragment::SimulationData, so its inline size is a primary memory cost — keep it lean (perf task A2).
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FSimMovementSample
{
	GENERATED_BODY()
public:
	/** Entity ID */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	int32 EntityID = 0;

	/** Position for Entity */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	FVector Position = FVector::ZeroVector;

	/** Rotation for Entity */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Speed for Entity */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	float Speed = 0.f;

	/** predefined movement bracket (for animation) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	EPedestrianMovementBracket MovementBracket = EPedestrianMovementBracket::Emb_NotMoving;

	/**
	 * Interned index into the owning provider's mode table (ISimSampleProvider::GetModeTable), replacing the
	 * former per-sample `FString Mode` (perf task A2). 0 == unset / "" — the default for every sample today:
	 * the importer parses the source "mode" attribute (e.g. "walk") into the plugin's transient
	 * FMobiusAgentSampleData, but the FSimMovementSample conversion in
	 * FProcessAgentSimulationDataRunnable::RunSimulationDataGatheringLoop copies only EntityID/Position/
	 * Rotation/Speed and drops it. Kept as a 1-byte slot (was a 16-byte FString that, being always empty, never
	 * even heap-allocated) so the capability survives: to re-enable, intern Sample.Mode into the provider's
	 * ModeTable at that conversion and store the returned index here. See ISimSampleProvider::GetModeTable.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	uint8 ModeIndex = 0;
};

// A2: lock the shrunk inline footprint (was ~104 B with FString Mode + StepDurationMS + StepVector; now ~64 B
// after moving the two unread step-motion fields to FSimSampleStepMotion below and interning Mode -> ModeIndex).
// Win64/MSVC layout: EntityID 4 + pad 4 + Position 24 + Rotation 24 + Speed 4 + MovementBracket 1 + ModeIndex 1
// + tail pad 2 = 64. This static_assert is a deliberate tripwire: a future field addition fails the build (one
// number to update) so a size change is a reviewed memory decision, never a silent regression.
static_assert(sizeof(FSimMovementSample) == 64,
	"FSimMovementSample inline size changed - this struct is stored ~millions of times; review perf task A2 before editing");

/**
 * Step-motion data MOVED OUT of the hot FSimMovementSample (perf task A2). Holds the two fields the movement /
 * representation processors never read: StepDurationMS was computed at import (CalcSmoothedStepMovementBrackets)
 * but consumed by nothing, and StepVector was never even populated. They were removed from the per-sample struct
 * (stored ~millions of times) to shrink its inline footprint; only MovementBracket — the actually-consumed gait
 * output — stays on the sample.
 *
 * This struct is intentionally NOT stored anywhere yet. It exists to (a) keep the field definitions + their
 * meaning under version control and (b) document the re-enable path: if this data is ever needed for playback/
 * analysis, add a parallel per-timestep TArray<FSimSampleStepMotion> on the provider (mirroring the ModeTable
 * pattern), populate it at the import conversion, and read it by sample index — do NOT add these fields back
 * onto FSimMovementSample inline (that would undo the A2 shrink; the static_assert above guards against it).
 *
 * Plain struct (not USTRUCT): the originals were non-reflected members; nothing needs BP/serialisation.
 */
struct FSimSampleStepMotion
{
	/** Estimated step duration in milliseconds (computed at import, consumed by nothing). */
	unsigned long StepDurationMS = 0;

	/** Angular step vector, smoothed across estimated steps/strides (was never populated on the sample). */
	FVelocityVector2D StepVector = FVelocityVector2D();
};

/**
 * 
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FSimulationFragment : public FMassSharedFragment
{
	GENERATED_BODY()
public:
	// TODO: Add buffer method and not store all data in this struct
	// TODO: This Map logic needs improving as it is not efficient with large data sets and looping over all data is poor
	/** TMap for data the key is time and value is struct array of FSimMovementSample.
	 *  Heap-allocated via TSharedPtr so the 4 GB data block can be freed independently
	 *  of the Mass archetype that permanently holds this struct. Call SimulationData.Reset()
	 *  on file switch to release the allocation without waiting for archetype destruction. */
	TSharedPtr<TMap<int32, TArray<FSimMovementSample>>> SimulationData = MakeShared<TMap<int32, TArray<FSimMovementSample>>>();

	UPROPERTY()
	float MaxTime = 0.0f;

	/** Monotonic build id, bumped once each time the spawn subsystem rebuilds this fragment (i.e. on every
	 *  agent-file load/switch). Persistent objects (e.g. the movement processor) that cache data DERIVED
	 *  from SimulationData across frames use it as a cache-invalidation key. A composite (DataGeneration,
	 *  timestep) key is required, not timestep alone: a file switch resets CurrentTimeStep to 0 while a NEW
	 *  fragment is built, so a t=0 -> t=0 switch would otherwise look unchanged and reuse a stale map against
	 *  a freshly-loaded (possibly shorter) sample array -> out-of-bounds read. Plain member, not a UPROPERTY:
	 *  uint32 is not a reflectable property type, and (like SimulationData above) it needs no GC/serialisation. */
	uint32 DataGeneration = 0;

	/** A1: backend abstraction over the per-timestep samples. For now an FFullyResidentProvider that WRAPS
	 *  SimulationData above (shares the same TSharedPtr) — consumers read through this instead of touching
	 *  SimulationData directly, so a future streaming/disk-backed provider can be swapped in without changing
	 *  them. Built in UMassEntitySpawnSubsystem::BuildPedestrianMovementFragmentData. Plain member (not a
	 *  UPROPERTY), like SimulationData. */
	TSharedPtr<ISimSampleProvider> Provider;

	///** Mapped Simulation Data */
	//TArray<TArray<TMap<int32, FSimMovementSample>>> MappedSimulationData;

};
