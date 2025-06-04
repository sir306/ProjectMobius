// Fill out your copyright notice in the Description page of Project Settings.


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
