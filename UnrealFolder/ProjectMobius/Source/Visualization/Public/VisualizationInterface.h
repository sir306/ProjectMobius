// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "HeatmapVisualizer.h"
#include "UObject/Interface.h"
#include "VisualizationInterface.generated.h"

// This class does not need to be modified.
UINTERFACE()
class UVisualizationInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class VISUALIZATION_API IVisualizationInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	// checks if the heatmap actor is valid or not
	static bool CheckIfHeatmapActorValid(const AHeatmapVisualizer* HeatmapActor, bool& WasSuccessful, FString& ErrorMessage);

	/*
	 * A method to get all heatmaps in the world - this should be called as little as possible due to performance reasons
	 *
	 * @param World - The world context
	 * @param OutHeatmapActors - The array of heatmap actors in the world
	 * @param WasSuccessful - If the method was successful
	 * @param ErrorMessage - The error message if the method was not successful 
	 */
	UFUNCTION()
	virtual void GetAllHeatmapsInWorld(UWorld* World, TArray<AHeatmapVisualizer*>& OutHeatmapActors, bool& WasSuccessful, FString& ErrorMessage);
	
	/*
	 * Method to update the heatmap from other classes
	 *
	 * @param HeatmapActor - The heatmap actor to update
	 * @param AgentLocation - The location of the agent in world space
	 * @param WasSuccessful - If the method was successful
	 * @param ErrorMessage - The error message if the method was not successful 
	 * 
	 */
	UFUNCTION()
	virtual void UpdateHeatmapWithNewLocation(AHeatmapVisualizer* HeatmapActor, const FVector& AgentLocation, bool& WasSuccessful, FString& ErrorMessage);

	/*
	 * Method to update the heatmap with multiple agents
	 *
	 * @param HeatmapActor - The heatmap actor to update
	 * @param AgentLocations - The array of agent locations in world space
	 * @param WasSuccessful - If the method was successful
	 * @param ErrorMessage - The error message if the method was not successful 
	 */
	UFUNCTION()
	virtual void UpdateHeatmapWithMultipleAgents(AHeatmapVisualizer* HeatmapActor, const TArray<FVector>& AgentLocations, bool& WasSuccessful, FString& ErrorMessage);
};

