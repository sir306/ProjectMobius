// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "HeatmapInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UHeatmapInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class HEATMAPVISUALIZATION_API IHeatmapInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	/*
	 * Method to update the heatmap from other classes
	 *
	 * @param AgentLocation The location of the agent in world space
	 * 
	 */
	virtual void UpdateHeatmap(const FVector& AgentLocation);
};
