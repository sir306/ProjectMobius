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
#include "PedestrianMovementProcessor.generated.h"


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

protected:
#pragma region PROTECTED_METHODS
	virtual void ConfigureQueries() override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	//bool IsThereDataToProcess(const FSimulationFragment& InSharedAgentMovement, int32 CurrentTimeStep);

	/**
	* Method to update an EntityInfoFragment to either render or not render
	* 
	* @param EntityInfoFragToUpdate: The EntityInfoFragment to update
	* @param bNewRenderStatus: The new render status to update the EntityInfoFragment with
	* 
	*/
	static void RenderEntityInfoFragment(FEntityInfoFragment& EntityInfoFragToUpdate, bool bNewRenderStatus = false);

	/**
	* Method to update an EntityInfoFragment with new location, rotation and render status
	* 
	* @param EntityInfoFragToUpdate: The EntityInfoFragment to update
	* @param NewLocation: The new location to update the EntityInfoFragment with
	* @param NewRotation: The new rotation to update the EntityInfoFragment with
	* @param bNewRenderStatus: The new render status to update the EntityInfoFragment with
	* 
	*/
	static void UpdateEntityInfoFragment(FEntityInfoFragment& EntityInfoFragToUpdate, const FVector& NewLocation, const FRotator& NewRotation, bool bNewRenderStatus = true);

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
	
#pragma  endregion PROTECTED_METHODS
	
#pragma region PROTECTED_VARIABLES


#pragma endregion PROTECTED_VARIABLES
private:

	//void GetEntitiesMovement();

#pragma region PRIVATE_VARIABLES
	/** Holds the entity query and used for adding conditions to the query that this processor uses */
	FMassEntityQuery EntityQuery;

	/** The subsystem that handles the simulation time and dilation logic */
	UPROPERTY()
	class UTimeDilationSubSystem* TimeDilationSubSystem;

	/** The current Time step of the simulation */
	int32 CurrentTimeStep = 0;

	bool bAreSubSystemsSetup = false;

	int32 OffsetIndex = 0;

	/** Current time step percentage - used to interpolate between time steps */
	float TimeStepPercentage = 0.0f;

	/// To avoid repetitive loops through data sample we can use a map to store the entity ID and
	/// the index in the movement sample ///
	
	/**	Lookup Map Key:Entity ID, Val: Index in current movement sample */
	UPROPERTY()
	TMap<int32, int32> EntityIDToCurrentMovementSampleIndexMap;

	/**	Lookup Map Key:Entity ID, Val: Index in next movement sample */
	UPROPERTY()
	TMap<int32, int32> EntityIDToNextMovementSampleIndexMap;

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
	 * it will not allocate the subsytems if we are not in world, this is to prevent the processor from trying to
	 * allocate and increase performance as the processor won't need to check if in world every call
	 *
	 * @param ExecutionContext: The execution context to check if we are in world
	 */
	void SetupSubSystems(FMassExecutionContext& ExecutionContext);

	/*
	 * Updates the current time step, if the subsystem is nullptr it will return and not update the time step
	 */
	void UpdateCurrentTimeStepAndStepPercentage();
	
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