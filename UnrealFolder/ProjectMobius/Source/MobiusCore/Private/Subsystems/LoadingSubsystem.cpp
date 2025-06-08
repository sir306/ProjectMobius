// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/LoadingSubsystem.h"

ULoadingSubsystem::ULoadingSubsystem()
{
}

void ULoadingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void ULoadingSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void ULoadingSubsystem::CalculateLoadPercentInt(int32 CurrentLoad, int32 TotalLoad)
{
	// Calculate the load percent
	float NewLoadPercent =  static_cast<float>(CurrentLoad / TotalLoad);

	OnLoadingPercentChanged.Broadcast(NewLoadPercent);
}

void ULoadingSubsystem::CalculateLoadPercentFloat(float CurrentLoad, float TotalLoad)
{
	// Calculate the load percent
	float NewLoadPercent =  CurrentLoad / TotalLoad;

	OnLoadingPercentChanged.Broadcast(NewLoadPercent);
}
