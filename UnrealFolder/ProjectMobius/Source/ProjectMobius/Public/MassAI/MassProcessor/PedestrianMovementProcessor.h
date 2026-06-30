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
#include "MassProcessor.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "PedestrianMovementProcessor.generated.h"


struct FEntityRenderingFragment;
struct FEntityMovementFragment;
struct FEntityInfoFragment;
/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UPedestrianMovementProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UPedestrianMovementProcessor();

	/**
	 * B2 per-timestep map cache invalidation predicate (pure; unit-tested in MovementMapCacheInvalidation).
	 * The EntityID->sample-index maps are derived from SimulationData->Find(timestep); this processor is
	 * persistent across file switches, so the cache key MUST be the composite (DataGeneration, timestep),
	 * never timestep alone: a file switch resets the clock to t=0 while a NEW fragment is built, so a
	 * t=0 -> t=0 switch looks unchanged under a timestep-only key and would reuse the previous file's map
	 * against the new (possibly shorter) sample array -> out-of-bounds read. Returns true when EITHER the
	 * data generation OR the timestep differs from what the maps were last built for.
	 */
	static bool ShouldRebuildSampleIndexMaps(uint32 CachedGen, int32 CachedStep, uint32 CurGen, int32 CurStep)
	{
		return CachedGen != CurGen || CachedStep != CurStep;
	}

protected:
#pragma region PROTECTED_METHODS
	virtual void ConfigureQueries() override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	//bool IsThereDataToProcess(const FSimulationFragment& InSharedAgentMovement, int32 CurrentTimeStep);

	/**
	* Method to update an EntityInfoFragment to either render or not render
	* 
	* @param EntityRenderFragToUpdate: The EntityInfoFragment to update
	* @param bNewRenderStatus: The new render status to update the EntityInfoFragment with
	* 
	*/
	static void RenderEntityInfoFragment(FEntityRenderingFragment& EntityRenderFragToUpdate, bool bNewRenderStatus = false);

	/**
	* Method to update an EntityInfoFragment with new location, rotation and render status
	* 
	* @param EntityMovementFragToUpdate: The EntityInfoFragment to update
	* @param EntityRenderFragToUpdate : The EntityRenderingFragment to update
	* @param NewLocation: The new location to update the EntityInfoFragment with
	* @param NewRotation: The new rotation to update the EntityInfoFragment with
	* @param bNewRenderStatus: The new render status to update the EntityInfoFragment with
	* 
	*/
	static void UpdateEntityInfoFragment(FEntityMovementFragment& EntityMovementFragToUpdate, FEntityRenderingFragment& EntityRenderFragToUpdate, const FVector& NewLocation, const FRotator& NewRotation, bool bNewRenderStatus = true);

	/**
	* Method to check ID of movement data matches the ID of the EntityInfoFragment
	* TODO: This will be eventually replaced to be a lookup method but for now simple check
	* 
	* @param MovementDataID: The ID of the movement data to check
	* @param EntityInfoID: The EntityInfoID to check
	* 
	* @return bool: True if the ID's match, false if they do not
	*/
	static bool DoesMovementAndEntityIDMatch(int32 MovementDataID, int32 EntityInfoID);

	/** Method to error check sample sizes */
	void ErrorCheckData(int32 MovementSampleSize, int32 EntityInfoSize) const;

	/**
	 * Get the current time step location and rotation and the next time step location and rotation if available
	 * otherwise it will return the same location and rotation
	 *
	 * @param[FEntityInfoFragment&] Entity: The entity to get the location and rotation from
	 * @param[FVector&] OutStartLocation: Out parameter to store the start location
	 * @param[FVector&] OutEndLocation: Out parameter to store the end location
	 * @param[FRotator&] OutStartRotation: Out parameter to store the start rotation
	 * @param[FRotator&] OutEndRotation: Out parameter to store the end rotation
	 * 
	 */
	void GetEntityLocationAndRotation(const FEntityInfoFragment& Entity, FVector& OutStartLocation, FVector& OutEndLocation, FRotator& OutStartRotation, FRotator& OutEndRotation) const;

	/**
	 * Method to linearly interpolate between two vectors and two rotations
	 * @param[FVector] StartLocation: The start location to interpolate from
	 * @param[FVector] EndLocation: The end location to interpolate to
	 * @param[FRotator] StartRotation: The start rotation to interpolate from
	 * @param[FRotator] EndRotation: The end rotation to interpolate to
	 *
	 * @return[TPair<FVector, FRotator>]: The interpolated location and rotation
	 */
	TPair<FVector,FRotator> LinearInterpolate(const FVector& StartLocation, const FVector& EndLocation, const FRotator& StartRotation, const FRotator& EndRotation) const;
	// [1] Helpers
	inline void AssignFromSample(
		FEntityMovementFragment& MoveFrag, 
		FEntityRenderingFragment& RenderFrag, 
		const FSimMovementSample& Sample, 
		bool bEnableRender);

	inline void InterpolateAndAssign(
		FEntityMovementFragment& MoveFrag, 
		FEntityRenderingFragment& RenderFrag, 
		const FSimMovementSample& Current, 
		const FSimMovementSample& Next, 
		float LerpAlpha);
	
	
#pragma  endregion PROTECTED_METHODS
	
#pragma region PROTECTED_VARIABLES


#pragma endregion PROTECTED_VARIABLES
private:

	//void GetEntitiesMovement();

#pragma region PRIVATE_VARIABLES
	/** Holds the entity query and used for adding conditions to the query that this processor uses */
	UPROPERTY()
	FMassEntityQuery EntityQuery;

	/** The subsystem that handles the simulation time and dilation logic */
	UPROPERTY()
	class UTimeDilationSubSystem* TimeDilationSubSystem;

	/**
	 * Spawn subsystem, cached source of the agent trajectory's sample interval. The agent sample
	 * map is keyed on the agent's own grid, which is independent of the shared clock interval
	 * (B-Risk may own that). Used to re-derive the sample index from absolute seconds.
	 */
	UPROPERTY()
	class UMassEntitySpawnSubsystem* AgentSpawnSubsystem = nullptr;

	/** The current Time step of the simulation */
	UPROPERTY()
	int32 CurrentTimeStep = 0;

	UPROPERTY()
	float CurrentSimTime = 0.0f;

	UPROPERTY()
	bool bAreSubSystemsSetup = false;

	UPROPERTY()
	int32 OffsetIndex = 0;

	/** Current time step percentage - used to interpolate between time steps */
	UPROPERTY()
	float TimeStepPercentage = 0.0f;

	/// To avoid repetitive loops through data sample we can use a map to store the entity ID and
	/// the index in the movement sample ///
	
	/**	Lookup Map Key:Entity ID, Val: Index in current movement sample */
	UPROPERTY()
	TMap<int32, int32> EntityIDToCurrentMovementSampleIndexMap;

	/**	Lookup Map Key:Entity ID, Val: Index in next movement sample */
	UPROPERTY()
	TMap<int32, int32> EntityIDToNextMovementSampleIndexMap;

	/**
	 * B2 cache key: the (DataGeneration, timestep) the two lookup maps above were last built for. Plain
	 * members (uint32 is not a reflectable UPROPERTY type, and they need no GC/serialisation). Start at
	 * (0, INDEX_NONE) while real generations start at 1, so the first Execute always rebuilds. See
	 * ShouldRebuildSampleIndexMaps.
	 */
	uint32 CachedDataGeneration = 0;
	int32  CachedMapsTimeStep   = INDEX_NONE;

	/** A1: last timestep we sent to ISimSampleProvider::NotifyPlayhead, so the movement direction hint
	 *  (forward/rewind) can be derived. Starts INDEX_NONE -> first notify reads as forward. */
	int32  PrevTimeStep = INDEX_NONE;

#pragma endregion PRIVATE_VARIABLES

#pragma region PRIVATE_METHODS
	/*
	 * Method to check if there is data to process in the processor
	 *
	 * @param ExecutionContext: The execution context to check if there is data to process
	 * @return bool: True if there is data to process, false if there is not
	 */
	bool IsThereDataToProcess(const FMassExecutionContext& ExecutionContext) const;

	/*
	 * This method will configure the sub systems that are required for the processor and query to function
	 * it will not allocate the subsystems if we are not in world, this is to prevent the processor from trying to
	 * allocate and increase performance as the processor won't need to check if in world every call
	 *
	 * @param ExecutionContext: The execution context to check if we are in world
	 */
	void SetupSubSystems(FMassExecutionContext& ExecutionContext);

	/*
	 * Updates the current time step, if the subsystem is nullptr it will return and not update the time step
	 */
	void UpdateCurrentTimeStepAndStepPercentage();

	/**
	 * Derive CurrentTimeStep + TimeStepPercentage from the absolute simulation time (CurrentSimTime)
	 * and the agent's native sample interval, so the agent sample map is indexed correctly even when
	 * B-Risk owns the shared clock interval. Identity with the clock grid when the agent owns the
	 * clock. Falls back to the shared clock's step/percentage when the agent interval is unavailable.
	 */
	void RecomputeAgentTimeIndex();
	
#pragma endregion PRIVATE_METHODS

#pragma region GETTERS_SETTER
	/*
	 * Setter for the CurrentTimeStep
	 *
	 * @param NewTimeStep: The new time step to set the CurrentTimeStep to
	 */
	FORCEINLINE void SetCurrentTimeStep(const int32 NewTimeStep) { CurrentTimeStep = NewTimeStep; }
	
	
#pragma endregion GETTERS_SETTER
	
};