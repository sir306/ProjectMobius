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

#include "VisualizationInterface.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"


bool IVisualizationInterface::CheckIfHeatmapActorValid(const AHeatmapVisualizer* HeatmapActor, bool& WasSuccessful,
                                                       FString& ErrorMessage)
{
	if(HeatmapActor)
	{
		WasSuccessful = true;
		ErrorMessage = "The heatmap actor is valid";
		return true;
	}
	
	WasSuccessful = false;
	ErrorMessage = "The heatmap actor is invalid";
	return false;
}

void IVisualizationInterface::GetAllHeatmapsInWorld(UWorld* World, TArray<AHeatmapVisualizer*>& OutHeatmapActors, bool& WasSuccessful, FString& ErrorMessage)
{
	// check if the world is valid
	if(!World)
	{
		WasSuccessful = false;
		ErrorMessage = "The world is invalid";
	}
	else
	{
		// get all the actors in the world
		TArray<AActor*> Actors;
		UGameplayStatics::GetAllActorsOfClass(World, AHeatmapVisualizer::StaticClass(), Actors);

		// check if there are any heatmap actors
		if(Actors.Num() == 0)
		{
			WasSuccessful = false;
			ErrorMessage = "There are no heatmap actors in the world";
		}
		else
		{
			// loop through the actors and add them to the OutHeatmapActors array
			for(AActor* Actor : Actors)
			{
				if(AHeatmapVisualizer* HeatmapActor = Cast<AHeatmapVisualizer>(Actor))
				{
					OutHeatmapActors.Add(HeatmapActor);
				}
			}
			WasSuccessful = true;
			ErrorMessage = "The heatmap actors were successfully retrieved";
		}
	}
}

void IVisualizationInterface::UpdateHeatmapWithNewLocation(AHeatmapVisualizer* HeatmapActor, const FVector& AgentLocation, bool& WasSuccessful, FString& ErrorMessage)
{
	// check if the heatmap actor is valid
	if(!CheckIfHeatmapActorValid(HeatmapActor,WasSuccessful, ErrorMessage))
	{
		HeatmapActor->UpdateHeatmap(AgentLocation);
		WasSuccessful = true;
		ErrorMessage = "The heatmap was updated with the agent location";
	}
}

void IVisualizationInterface::UpdateHeatmapWithMultipleAgents(AHeatmapVisualizer* HeatmapActor,
                                                              const TArray<FVector>& AgentLocations, bool& WasSuccessful, FString& ErrorMessage)
{
	// check if the agent locations are empty
	if(AgentLocations.Num() == 0)
	{
		WasSuccessful = false;
		ErrorMessage = "The agent locations are empty";
		return;
	}

	// check if the heatmap actor is valid
	if(!CheckIfHeatmapActorValid(HeatmapActor,WasSuccessful, ErrorMessage))
	{
		// update the heatmap with the agent locations
		HeatmapActor->UpdateHeatmapWithMultipleAgents(AgentLocations);
		WasSuccessful = true;
		ErrorMessage = "The heatmap was updated with the agent locations";
	}
}
