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
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentNiagaraRepSharedFrag.h"
#include "NiagaraAgentRepProcessor.generated.h"


class UNiagaraComponent;
struct FAgentRepresentationFragment;
struct FEntityInfoFragment;
class UTimeDilationSubSystem;
class UMRS_RepresentationSubsystem;
enum class EPedestrianMovementBracket : uint8;
/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UNiagaraAgentRepProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UNiagaraAgentRepProcessor();
	
protected:
	virtual void ConfigureQueries() override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	/**
	 * Extract the data from the entity info fragment and populate the arrays to send to the Niagara System
	 *
	 * @param[FMassExecutionContext] Context - The execution context
	 */
	void ExtractAgentData(FMassExecutionContext& Context);

	/**
	 * Helper method to set the extracted data at the correct index and in the correct array
	 *
	 * @param[int32] Index - The index to set the data at
	 * @param[FEntityInfoFragment] EntityInfo - The entity info fragment to get the data from
	 * @param[TArray<FVector4>] LocationAndScales - The array to set the data at for the location and scales
	 * @param[TArray<FQuat>] Rotations - The array to set the data at for the rotations
	 * @param[TArray<int32>] AnimationStates - The array to set the data at for the animation states
	 * 
	 */
	static void SetAgentData(int32 Index, FEntityInfoFragment& EntityInfo, TArray<FVector4>& LocationAndScales, TArray<FQuat>& Rotations, TArray<int32>& AnimationStates);
	
	/**
	 * Helper method to get the correct animation integer state value
	 *
	 * @param[EPedestrianMovementBracket] AnimState - The animation state to get the integer value for
	 */
	static int32 GetIntAnimState(EPedestrianMovementBracket AnimState);

	/**
	 * Assign the properties of this processor - will only be called till properties are assigned
	 *
	 * @param Context - The execution context
	 */
	void RegisterProperties(FMassExecutionContext& Context);

	/**
	 * Pause/Resume all animations for the agents
	 *
	 * @param[bool] bPause - If we want to pause or unpause the animations
	 */
	void PauseResumeAnimations(bool bPause) const;

	/**
	 * Get the number of agents in the system and map it to the array
	 *
	 * @param[FAgentRepresentationFragment] AgentRepresentationFragment The agent representation fragment to get the number of agents from
	 */
	void MapAgentCountToArray(const FAgentRepresentationFragment& AgentRepresentationFragment);

	/**
	 * Check the amount of agents in count array against the corresponding location and scale arrays
	 *
	 * @return[bool] If the arrays are the same size returns true
	 */
	bool CheckAgentCountArraySize(const FAgentRepresentationFragment& AgentRepresentationFragment) const;

	/**
	 * Helper method to check if the agent count array is the same size as the location and scale arrays
	 * at the specified index for the specified array
	 *
	 * @param[int32] Index - The index to check
	 * @param[int32] ArraySize - The size of the array to check
	 *
	 * @return[bool] If the arrays are the same size returns true
	 */
	bool CheckAgentArraySize(int32 Index, int32 ArraySize) const;

	/**
	 * Update the data in the Niagara System with the new data
	 * 
	 * @param NiagaraComp The Niagara Component to set the data on
	 * @param BaseName The gender and age demographic name of the niagara value to set
	 * @param Locations The new locations to set
	 * @param Rotations The new rotations to set
	 * @param AnimationStates The new animation states to set
	 * 
	 */
	static void SetNiagaraAgentData(UNiagaraComponent* NiagaraComp, const FString& BaseName, const TArray<FVector4>& Locations, const TArray<FQuat>& Rotations, const TArray<int32>& AnimationStates);
	
private:
	// Entity Query
	FMassEntityQuery EntityQuery;

	/** Niagara System */
	UPROPERTY()
	TObjectPtr<class ANiagaraAgentRepActor> NiagaraAgentRepActor;

	/** Array Storing the different number of agents */
	UPROPERTY()
	TArray<int32> NumberOfAgentsArray;

	/** Array of male locations to send to the Niagara System */
	UPROPERTY()
	TArray<FVector4> MaleAdultAgentLocationAndScales;

	/** Array of male rotations -> Niagara System Expects Quats or Fvector for rotation */
	UPROPERTY()
	TArray<FQuat> MaleAdultAgentRotations;
	
	/** Array of male animation states -> we use int now as we can do the value setting of frames etc on the GPU in niagara */
	UPROPERTY()
	TArray<int32> MaleAnimationStates;

	/** Array of Elderly male locations to send to the Niagara System */
	UPROPERTY()
	TArray<FVector4> ElderlyMaleAdultAgentLocationAndScales;

	/** Array of Elderly male rotations -> Niagara System Expects Quats or Fvector for rotation */
	UPROPERTY()
	TArray<FQuat> ElderlyMaleAdultAgentRotations;
	
	/** Array of Elderly male animation states -> we use int now as we can do the value setting of frames etc on the GPU in niagara */
	UPROPERTY()
	TArray<int32> ElderlyMaleAnimationStates;

	/** Array of female locations to send to the Niagara System */
	UPROPERTY()
	TArray<FVector4> FemaleAdultAgentLocationAndScales;

	/** Array of female rotations -> Niagara System Expects Quats or Fvector for rotation */
	UPROPERTY()
	TArray<FQuat> FemaleAdultAgentRotations;

	/** Array of female animation states -> we use int now as we can do the value setting of frames etc on the GPU in niagara */
	UPROPERTY()
	TArray<int32> FemaleAnimationStates;

	/** Array of Elderly female locations to send to the Niagara System */
	UPROPERTY()
	TArray<FVector4> ElderlyFemaleAdultAgentLocationAndScales;

	/** Array of Elderly female rotations -> Niagara System Expects Quats or Fvector for rotation */
	UPROPERTY()
	TArray<FQuat> ElderlyFemaleAdultAgentRotations;
	
	/** Array of Elderly female animation states -> we use int now as we can do the value setting of frames etc on the GPU in niagara */
	UPROPERTY()
	TArray<int32> ElderlyFemaleAnimationStates;

	/** Array of Children locations to send to the Niagara System */
	UPROPERTY()
	TArray<FVector4> ChildrenAgentLocationAndScales;

	/** Array of Children rotations -> Niagara System Expects Quats or Fvector for rotation */
	UPROPERTY()
	TArray<FQuat> ChildrenAgentRotations;
	
	/** Array of Children animation states -> we use int now as we can do the value setting of frames etc on the GPU in niagara */
	UPROPERTY()
	TArray<int32> ChildrenAnimationStates;
	
	/** Number of Agents */
	UPROPERTY()
	int32 NumberOfAgents = 0;

	/** Niagara Shared Frag */
	UPROPERTY()
	FAgentNiagaraRepSharedFrag AgentNiagaraRepSharedFrag;

	// bool to say if we have the registered the necessary properties of this processor
	UPROPERTY()
	bool bRegisteredProperties = false;

	/** Current Time step, if we store a value of a current time step we can check if changed as we don't need to update renders every tick only when data changes */
	UPROPERTY()
	int32 CurrentTimeStep = -1; // -1 is the default this ensures when checking if the time step has changed it will always be true on the first run

	/** stores the current animation pause state from the subsystem */
	UPROPERTY()
	bool bIsPaused = false;

	/** stores the last loop pause state, this way it's not updating custom data values for every entity every loop */
	UPROPERTY()
	bool bLastPauseLoop = false;

	/** Ptr to the time dilation subsystem */
	UPROPERTY()
	TObjectPtr<UTimeDilationSubSystem> TimeDilationSubSystem;

	/** Ptr to the Agent Representation Subsystem */
	UPROPERTY()
	TObjectPtr<UMRS_RepresentationSubsystem> RepresentationSubsystem;

	// last current time value
	UPROPERTY()
	float LastUpdatedCurrentTime = 0.0f;
};
