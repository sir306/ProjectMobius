// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/StatisticActorManagementSubsystem.h"
#include "Util/MemoryTraceHelper.h"

UStatisticActorManagementSubsystem::UStatisticActorManagementSubsystem()
{
}

void UStatisticActorManagementSubsystem::AddFlowCounter(AFlowCounter* FlowCounter)
{
	if (!FlowCounters.Contains(FlowCounter))
	{
		FlowCounters.Add(FlowCounter);

#if !UE_BUILD_SHIPPING
		FMobiusMemSnapshot::Take(
			FString::Printf(TEXT("FCMgr_Add[count=%d]"), FlowCounters.Num())).LogAbsolute();
#endif

		// Notify listeners that the flow counters have changed
		OnFlowCountersChanged.ExecuteIfBound();
	}
}

void UStatisticActorManagementSubsystem::RemoveFlowCounter(AFlowCounter* FlowCounter)
{
	if (FlowCounters.Contains(FlowCounter))
	{
#if !UE_BUILD_SHIPPING
		FMobiusMemSnapshot SnapBefore = FMobiusMemSnapshot::Take(
			FString::Printf(TEXT("FCMgr_RemoveStart[count=%d]"), FlowCounters.Num()));
#endif

		int32 Index = FlowCounters.Find(FlowCounter);
		FlowCounters.RemoveAt(Index);

		// once we remove it we need to correct the indices of the remaining flow counters

		// Notify listeners that the flow counters have changed
		OnFlowCountersChanged.ExecuteIfBound();

#if !UE_BUILD_SHIPPING
		FMobiusMemSnapshot::Take(
			FString::Printf(TEXT("FCMgr_RemoveEnd[count=%d]"), FlowCounters.Num())).LogDelta(SnapBefore);
#endif
	}
}
