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
#include "AgentHeatmapProcessor.generated.h"

enum class EPedestrianMovementBracket : uint8;
class UMRS_RepresentationSubsystem;
class UTimeDilationSubSystem;
struct FEntityInfoFragment;
/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UAgentHeatmapProcessor : public UMassProcessor
{
	GENERATED_BODY()
	
public:
	UAgentHeatmapProcessor();

protected:
	virtual void ConfigureQueries() override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	/**
	 * Assign the properties of this processor - will only be called till properties are assigned
	 *
	 * @param Context - The execution context
	 */
	void RegisterProperties(FMassExecutionContext& Context);

private:
	// Entity Query
	FMassEntityQuery EntityQuery;

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

	/** bool to say when heatmaps should be updated */
	UPROPERTY()
	bool bUpdateHeatmap = true;

	// last current time value
	UPROPERTY()
	float LastUpdatedCurrentTime = 0.0f;

	/** Stores the locations of the agents so it can be sent to the heatmap subsystem */
	UPROPERTY()
	TArray<FVector> HeatmapLocations;

	/** Active Number of heatmaps */
	UPROPERTY()
	int32 ActiveHeatmapCount = 0;
	
};
