// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/StatisticSubsystem.h"

#include "Actors/FlowCounter.h"
#include "Subsystems/StatisticActorManagementSubsystem.h"
#include "Util/MemoryTraceHelper.h"

UStatisticSubsystem::UStatisticSubsystem()
{
}

void UStatisticSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// add the statistic actor management subsystem to the collection
	Collection.InitializeDependency<UStatisticActorManagementSubsystem>();


	Super::Initialize(Collection);

	// bind to the flow counters changed delegate
	if (UStatisticActorManagementSubsystem* StatisticActorManagementSubsystem = GetWorld()->GetSubsystem<UStatisticActorManagementSubsystem>())
	{
		StatisticActorManagementSubsystem->OnFlowCountersChanged.BindDynamic(this, &UStatisticSubsystem::UpdateFlowCounters);
	}
}

void UStatisticSubsystem::Deinitialize()
{
#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot SnapDeinit = FMobiusMemSnapshot::Take(
		FString::Printf(TEXT("StatSub_Deinit_Start[flow=%d,active=%d,agents=%d]"),
			FlowCounters.Num(), ActiveFlowCounters.Num(), PedestrianAgentData.Num()));
#endif

	Super::Deinitialize();

	// unbind to the flow counters changed delegate
	if (UStatisticActorManagementSubsystem* StatisticActorManagementSubsystem = GetWorld()->GetSubsystem<UStatisticActorManagementSubsystem>())
	{
		StatisticActorManagementSubsystem->OnFlowCountersChanged.Unbind();
	}

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot::Take(TEXT("StatSub_Deinit_End")).LogDelta(SnapDeinit);
#endif
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
	return HoveredAgentData;
}

void UStatisticSubsystem::PublishAgentEgressHealthData(TArray<FAgentEgressTenabilityViewer>& InOutAgentData)
{
	Swap(AgentEgressHealthData, InOutAgentData);
	++AgentEgressHealthRevision;
}

TConstArrayView<FAgentEgressTenabilityViewer> UStatisticSubsystem::GetAgentEgressHealthData() const
{
	return AgentEgressHealthData;
}

void UStatisticSubsystem::ClearAgentEgressHealthData()
{
	AgentEgressHealthData.Reset();
	++AgentEgressHealthRevision;
}

void UStatisticSubsystem::UpdateFlowCounters()
{
	if (!GetWorld()){return;}

	UStatisticActorManagementSubsystem* StatisticActorManagementSubsystem = GetWorld()->GetSubsystem<UStatisticActorManagementSubsystem>();

	if (StatisticActorManagementSubsystem)
	{
		// Copy-assign is enough — TArray::operator= replaces contents. Previous code
		// also called Empty() first which triggered an extra allocator trip on every
		// FlowCounter Add/Remove broadcast (O(N²) churn at startup with 100 counters).
		// TODO: switch to delta add/remove instead of full-array rebroadcast.
		FlowCounters = StatisticActorManagementSubsystem->FlowCounters;
	}
}

//TODO: we are going to need to expand this functionality when we increase the number of flow counters and their Z bounds
// as it will become taxing on performance to loop through all flow counters every time we check if an agent is in a flow counter band
// will require a spatial hash or similar to quickly check if an agent is in a flow counter band,
// which should be a larger unrotated 2D plane that represents min max XY for agents to be considered in a flow counter band check
bool UStatisticSubsystem::IsAgentLocationInAFlowCounterBand(const FVector& AgentLocation, int32 FlowCounterID) const
{
	if (!ActiveFlowCounters.IsValidIndex(FlowCounterID) ||
		ActiveFlowCounters[FlowCounterID] == nullptr ||
		ActiveFlowCounters[FlowCounterID]->IsActorBeingDestroyed())
	{
		return false;
	}

	AFlowCounter* FlowCounter = ActiveFlowCounters[FlowCounterID];

	// First check: is the agent's Z coordinate within this counter's Z bounds?
	if (!FlowCounter->FlowCounterZSearchLimits.IsInZBounds(AgentLocation.Z))
	{
		return false;
	}

	// // Get an expanded XY bounding box around the FlowCounter's trigger volume to allow leniency in horizontal proximity
	// const UE::Math::TBox FlowCounterBox = FlowCounter->FlowCounterTriggerBox->Bounds.GetBox().ExpandBy(FVector(500.0f, 500.0f, 0.0f));
	//
	// // Measure the 3D distance between the agent and the FlowCounter
	// const float DistanceToFlowCounter = FVector::Dist(FlowCounter->GetActorLocation(), AgentLocation);
	//
	// // If the agent is too far away (> 500 units), skip further checks for performance unless inside expanded box
	// if (DistanceToFlowCounter > 500.0f && !FMath::PointBoxIntersection(AgentLocation, FlowCounterBox))
	// {
	// 	continue;
	// }

	return true;
}

bool UStatisticSubsystem::HasAgentBeenCountedInFlowCounter(const int32 AgentID, int32 FlowCounterID) const
{
	if (!ActiveFlowCounters.IsValidIndex(FlowCounterID) ||
		ActiveFlowCounters[FlowCounterID] == nullptr ||
		ActiveFlowCounters[FlowCounterID]->IsActorBeingDestroyed())
	{
		return false;
	}

	AFlowCounter* FlowCounter = ActiveFlowCounters[FlowCounterID];

	// Check if the agent has already been counted in this flow counter
	return FlowCounter->HasAgentAlreadyPassedThrough(AgentID);
}


//TODO: this should be private method that we call after filtered in to correspond flowcounter groups - but prototype only using one so direct call to this is fine
void UStatisticSubsystem::SendArrayDataToFlowCounter(TArray<FFlowCounterData>& FlowData,
                                                int32 FlowCounterIndex)
{
	if (!FlowCounters.IsValidIndex(FlowCounterIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid FlowCounterIndex: %d"), FlowCounterIndex);
		return;
	}
	AFlowCounter* FlowCounter = FlowCounters[FlowCounterIndex];

	if (FlowCounter && !FlowCounter->IsActorBeingDestroyed())
	{
		FlowCounter->NewAgentData(FlowData);
	}
}

void UStatisticSubsystem::SendDataToFlowCounter(const FFlowCounterData& FlowData, int32 FlowCounterIndex)
{
	if (!ActiveFlowCounters.IsValidIndex(FlowCounterIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid FlowCounterIndex: %d"), FlowCounterIndex);
		return;
	}
	AFlowCounter* FlowCounter = ActiveFlowCounters[FlowCounterIndex];

	if (FlowCounter && !FlowCounter->IsActorBeingDestroyed())
	{
		FlowCounter->ProcessAgentFlowCrossing(FlowData);
	}
}

void UStatisticSubsystem::ResetFlowCounters()
{
#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot SnapResetAll = FMobiusMemSnapshot::Take(
		FString::Printf(TEXT("StatSub_ResetAll_Start[flow=%d]"), FlowCounters.Num()));
#endif

	for (AFlowCounter* FlowCounter : FlowCounters)
	{
		if (FlowCounter)
		{
			FlowCounter->ResetFlowCounterTrackingData();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("FlowCounter is null"));
		}
	}

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot::Take(TEXT("StatSub_ResetAll_End")).LogDelta(SnapResetAll);
#endif
}

void UStatisticSubsystem::ResetForFileSwitch()
{
#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot SnapStart = FMobiusMemSnapshot::Take(
		FString::Printf(TEXT("StatSub_ResetForFileSwitch_Start[agents=%d]"),
			PedestrianAgentData.Num()));
#endif

	PedestrianAgentData.Empty();
	SelectedAgentData = FAgentMeshViewer();
	HoveredAgentData = FAgentMeshViewer();
	ClearAgentEgressHealthData();

	OnSelectedAgentInfoChanged.Broadcast();

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot::Take(TEXT("StatSub_ResetForFileSwitch_End")).LogDelta(SnapStart);
#endif
}

void UStatisticSubsystem::AddRemoveActiveFlowCounter(AFlowCounter* FlowCounter, bool bAddToActiveCounters)
{
	if (bAddToActiveCounters && FlowCounter)
	{
		if (!ActiveFlowCounters.Contains(FlowCounter))
		{
			ActiveFlowCounters.Add(FlowCounter);
		}
	}
	else if (!bAddToActiveCounters && FlowCounter)
	{
		ActiveFlowCounters.Remove(FlowCounter);
	}
}

// TODO: Instantaneous flow rate is the important one need to add
// max flow rate over the total instantaneous flow rate
// specific flow and specific instantaneous flow

float UStatisticSubsystem::ComputeFlow(int32 Pedestrians, float TimeSeconds)
{
	return Pedestrians / TimeSeconds;
}

float UStatisticSubsystem::ComputeFlowRatePerWidth(int32 PedestrianCount, float TimeSeconds, float WidthMeters)
{
	if (TimeSeconds <= 0.f || WidthMeters <= 0.f) return 0.f;
	return static_cast<float>(PedestrianCount) / (TimeSeconds * WidthMeters);
}

float UStatisticSubsystem::ComputeDensity(int32 PedestrianCount, float AreaSqMeters)
{
	if (AreaSqMeters <= 0.f) return 0.f;
	return static_cast<float>(PedestrianCount) / AreaSqMeters;
}

float UStatisticSubsystem::ComputeLinearDensity(int32 PedestrianCount, float LengthMeters)
{
	if (LengthMeters <= 0.f) return 0.f;
	return static_cast<float>(PedestrianCount) / LengthMeters;
}

float UStatisticSubsystem::ComputeSpecificFlow(float Flow, float WidthMeters)
{
	if (WidthMeters <= 0.f) return 0.f;
	return Flow / WidthMeters;
}

float UStatisticSubsystem::ComputeSpacePerPedestrian(float Density)
{
	if (Density <= 0.f) return 0.f;
	return 1.f / Density;
}

float UStatisticSubsystem::ComputeTravelTime(float LengthMeters, float Speed)
{
	if (Speed <= 0.f) return 0.f;
	return LengthMeters / Speed;
}

float UStatisticSubsystem::ComputeHeadway(float CurrentTime, float PreviousTime)
{
	return CurrentTime - PreviousTime;
}

float UStatisticSubsystem::ComputeInstantaneousFlow(float Headway)
{
	if (Headway <= 0.f) return 0.f;
	return 1.f / Headway;
}

float UStatisticSubsystem::ComputeEvacuationTime(int32 PedestrianCount, float CapacityPerWidth, float WidthMeters)
{
	if (CapacityPerWidth <= 0.f || WidthMeters <= 0.f) return 0.f;
	return static_cast<float>(PedestrianCount) / (CapacityPerWidth * WidthMeters);
}

float UStatisticSubsystem::ComputeWeidmannSpeed(float Density, float FreeSpeed, float JamDensity)
{
	if (JamDensity <= 0.f) return 0.f;

	// Weidmann (1993) speed-density relation
	const float ExpTerm = -1.913f * ((1.f / Density) - (1.f / JamDensity));
	return FreeSpeed * (1.f - FMath::Exp(ExpTerm));
}
