// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/StatisticActorManagementSubsystem.h"

UStatisticActorManagementSubsystem::UStatisticActorManagementSubsystem()
{
}

void UStatisticActorManagementSubsystem::AddFlowCounter(AFlowCounter* FlowCounter)
{
	if (!FlowCounters.Contains(FlowCounter))
	{
		FlowCounters.Add(FlowCounter);
		
		// Notify listeners that the flow counters have changed
		OnFlowCountersChanged.ExecuteIfBound();
	}
}

void UStatisticActorManagementSubsystem::RemoveFlowCounter(AFlowCounter* FlowCounter)
{
	if (FlowCounters.Contains(FlowCounter))
	{
		int32 Index = FlowCounters.Find(FlowCounter);
		FlowCounters.RemoveAt(Index);
		
		// once we remove it we need to correct the indices of the remaining flow counters
		
		// Notify listeners that the flow counters have changed
		OnFlowCountersChanged.ExecuteIfBound();
	}
}
