// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/StatisticSubsystem.h"

#include "Actors/FlowCounter.h"
#include "Kismet/GameplayStatics.h"

UStatisticSubsystem::UStatisticSubsystem()
{
}

void UStatisticSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// add any subsystem dependencies here

	
	Super::Initialize(Collection);
}

void UStatisticSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UStatisticSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Clear the flow counters list at the start of the world
	FlowCounters.Empty();
}

void UStatisticSubsystem::UpdateAgentInfoMeshData(const TArray<FAgentMeshViewer>& AgentData)
{
	if (AgentData.Num() == 0 && PedestrianAgentData.Num() == 0)
	{
		// No data to update, return early
		return;
	}
	PedestrianAgentData = AgentData;
	// Notify listeners that the agent info has changed
	OnAgentInfoChanged.Broadcast();
}

void UStatisticSubsystem::UpdateSelectedAgentData(const FAgentMeshViewer& AgentData)
{
	SelectedAgentData = AgentData;
	OnSelectedAgentInfoChanged.Broadcast();
}

void UStatisticSubsystem::UpdateHoveredAgentData(const FAgentMeshViewer& AgentData)
{
	HoveredAgentData = AgentData;
	OnSelectedAgentInfoChanged.Broadcast();
}

TArray<FAgentMeshViewer> UStatisticSubsystem::GetAgentInfoMeshData()
{
	return PedestrianAgentData;
}

FAgentMeshViewer UStatisticSubsystem::GetSelectedAgentInfoMeshData()
{
	return SelectedAgentData;
}

FAgentMeshViewer UStatisticSubsystem::GetHoveredAgentInfoMeshData()
{
	return MoveTemp(HoveredAgentData);
}

void UStatisticSubsystem::AddFlowCounter(AFlowCounter* FlowCounter)
{
	if (!FlowCounter)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowCounter is null! Cannot add to StatisticSubsystem."));
		return;
	}
	// if not null we need to check if the flow counter is already in the list as this would mean a resize
	if (!FlowCounters.Contains(FlowCounter) || FlowCounters.Num() == 0)
	{
		FlowCounters.Add(FlowCounter); // when multi-threaded is added we will need to use a lock or similar to ensure thread safety
	}
	
	// Now we should notify to the listeners that a flow counter has been added/updated
	
}

void UStatisticSubsystem::RemoveFlowCounter(AFlowCounter* FlowCounter)
{
	// if not null we need to check if the flow counter is already in the list as this would mean a resize
	if (FlowCounter && !FlowCounters.Contains(FlowCounter))
	{
		FlowCounters.Remove(FlowCounter); // when multi-threaded is added we will need to use a lock or similar to ensure thread safety
	}

	// if we have none left we need to notify the listeners that we have no flow counters left no need to process data
}

//TODO: we are going to need to expand this functionality when we increase the number of flow counters and their Z bounds
// as it will become taxing on performance to loop through all flow counters every time we check if an agent is in a flow counter band
// will require a spatial hash or similar to quickly check if an agent is in a flow counter band,
// which should be a larger unrotated 2D plane that represents min max XY for agents to be considered in a flow counter band check
bool UStatisticSubsystem::IsAgentLocationInAFlowCounterBand(const FVector& AgentLocation) const
{
	// we only return a bool because it is possible to have multiple flow counters that could share valid Z bounds
	bool bIsInBounds = false;
	// loop through counters and check if the agent location is within the bounds of any flow counter
	for (const AFlowCounter* FlowCounter : FlowCounters)
	{
		// null check
		if (FlowCounter)
		{
			bIsInBounds = FlowCounter->FlowCounterZSearchLimits.IsInZBounds(AgentLocation.Z);

			if (bIsInBounds)
			{
				// we found one so we can break out of the loop
				return true;
			}
		}
	}

	return bIsInBounds;
}

//TODO: this should be private method that we call after filtered in to correspond flowcounter groups - but prototype only using one so direct call to this is fine
void UStatisticSubsystem::SendDataToFlowCounter(UE::TConsumeAllMpmcQueue<FFlowCounterData>& FlowData,
                                                int32 FlowCounterIndex)
{
	// Check if the flow counters array is empty - it should never be empty as this can only be called from the FlowCounterProcessor
	if (FlowCounters.Num() == 0)
	{
		// attempt to get the flow counters from the world if they are not set
		if (GetWorld())
		{
			

			// As this can be called from outside of the game thread(ParallelFor), we need to get the flow counters from the world in a thread-safe manner.
			AsyncTask(ENamedThreads::GameThread, [this]()
			{
				TArray<AActor*> FoundActors;
				UWorld* World = GetWorld();
				UGameplayStatics::GetAllActorsOfClass(World, AFlowCounter::StaticClass(), FoundActors);
				if (FoundActors.Num() > 0)
				{
					for (AActor* Actor : FoundActors)
					{
						if (AFlowCounter* FlowCounter = Cast<AFlowCounter>(Actor))
						{
							// Add the flow counter to the array
							FlowCounters.Add(FlowCounter);
						}
						else
						{
							UE_LOG(LogTemp, Warning, TEXT("Found actor is not a FlowCounter: %s"), *Actor->GetName());
						}
					}
				}
			});
			//UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFlowCounter::StaticClass(), FoundActors);
			// if we found any flow counters, we can add them to the FlowCounters array
			
		}
	}
	
	// Check if the flow counter index is valid
	if (FlowCounterIndex < 0 || FlowCounterIndex >= FlowCounters.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid FlowCounterIndex: %d"), FlowCounterIndex);
		return;
	}
	AFlowCounter* FlowCounter = FlowCounters[FlowCounterIndex];

	// we may have found a valid index but we can still have a null pointer
	if (FlowCounter)
	{
		FlowCounter->NewAgentData(FlowData);
	}
}
