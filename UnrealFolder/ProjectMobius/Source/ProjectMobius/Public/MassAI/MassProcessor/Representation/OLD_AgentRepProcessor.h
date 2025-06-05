// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 Nicholas R. Harding
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
#include "MassAI/Actors/AgentRepresentationActorISM.h"
#include "OLD_AgentRepProcessor.generated.h"

enum class EPedestrianMovementBracket : uint8;
class UMRS_RepresentationSubsystem;
class UTimeDilationSubSystem;
struct FEntityInfoFragment;
/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UAgentRepProcessor : public UMassProcessor
{
	GENERATED_BODY()
	
public:
	UAgentRepProcessor();

protected:
	virtual void ConfigureQueries() override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	/**
	 * Assign the properties of this processor - will only be called till properties are assigned
	 *
	 * @param Context - The execution context
	 */
	void RegisterProperties(FMassExecutionContext& Context);

	/**
	 * Set the Entity representation fragment
	 *
	 * @param EntityInfo - The entity info fragment
	 */
	void UpdateEntityRepresentation(FEntityInfoFragment& EntityInfo) const;

	/**
	 * If a simulation is paused to unpaused or vice versa, we want to stop the animations of the agents moving or get them moving again
	 *
	 * @param[UInstancedStaticMeshComponent] IsmComp - The ISM component to stop or start the animations
	 * @param[int32] InstanceIndex - The index of the instance we want to stop or start the animations
	 * @param[bool] bPause - If we want to pause or unpause the animations
	 */
	static void SetAnimationPause(UInstancedStaticMeshComponent* IsmComp, int32 InstanceIndex, bool bPause);

	/**
	 * Gets the correct ISM component to use for the agent
	 *
	 * @param[bool] bIsMale - Is this a male agent?
	 * @param[AAgentRepresentationActorISM] AgentRepresenterActor - The agent representation actor to use - should have a male and female ISM component
	 * @param[bool] bValidReq - If we don't find the ISM component or the actor is null, we want to use this bool to say if we found it or not
	 *
	 * @return[UInstancedStaticMeshComponent*] The ISM component to use for the agent
	 */
	static UInstancedStaticMeshComponent* GetISMComponent(bool bIsMale, AAgentRepresentationActorISM* AgentRepresenterActor, bool& bValidReq);

	/**
	 * Update the transform for a specified agent at a specified index - transform, rotation and scale all default to zero
	 *
	 * @param[UInstancedStaticMeshComponent] IsmComp - The ISM component to update
	 * @param[int32] InstanceIndex - The index of the instance to update
	 * @param[FRotator] Rotation - The rotation to set the instance to
	 * @param[FVector] Location - The location to set the instance to
	 * @param[FVector] Scale - The scale to set the instance to
	 * 
	 */
	static void UpdateInstanceTransformForISMComp(UInstancedStaticMeshComponent* IsmComp, int32 InstanceIndex, const FRotator& Rotation = FRotator::ZeroRotator, const FVector& Location = FVector::ZeroVector, const FVector& Scale = FVector::ZeroVector);


	/**
	 * Update CustomData for the ISM component at a specified index
	 *
	 * @param[UInstancedStaticMeshComponent] IsmComp - The ISM component to update
	 * @param[int32] InstanceIndex - The index of the instance to update
	 * @param[EPedestrianMovementBracket] MovementBracket - The movement bracket of the agent
	 */
	void UpdateCustomDataForISMComp(UInstancedStaticMeshComponent* IsmComp, int32 InstanceIndex, EPedestrianMovementBracket MovementBracket) const;
	
private:
	// Entity Query
	FMassEntityQuery EntityQuery;

	// Agent Representation Actor
	UPROPERTY()
	class AAgentRepresentationActorISM* AgentRepresentationActor;

	/** Subsystem for heatmaps -- TODO: move this to its own processor */
	UPROPERTY()
	class UHeatmapSubsystem* HeatmapSubsystem;

	// bool to say if we have the registered the necessary properties of this processor
	UPROPERTY()
	bool bRegisteredProperties;

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

	/** bool to say when heatmaps should be updated */
	UPROPERTY()
	bool bUpdateHeatmap = false;

	// last current time value
	UPROPERTY()
	float LastUpdatedCurrentTime = 0.0f;

	/** Agent Render Height - the Z coordinate where agents are rendered down from  */
	UPROPERTY()
	float AgentRenderHeight = FLT_MAX;

	/** Stores the locations of the agents so it can be sent to the heatmap subsystem */
	UPROPERTY()
	TArray<FVector> HeatmapLocations;

	/** Size for heatmap location array = number of agents */
	UPROPERTY()
	int32 HeatmapLocationsSize = 0;
};

class FRunnableRepresentationProcessorOLD : public FRunnable
{
public:
	FRunnableRepresentationProcessorOLD(FEntityInfoFragment* InEntityInfo, class AAgentRepresentationActorISM* InAgentRepresentationActor);
	virtual ~FRunnableRepresentationProcessorOLD() override;

	FRunnableThread* Thread;
	FEntityInfoFragment* EntityInfo;
	class AAgentRepresentationActorISM* AgentRepresentationActor;
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	
};

