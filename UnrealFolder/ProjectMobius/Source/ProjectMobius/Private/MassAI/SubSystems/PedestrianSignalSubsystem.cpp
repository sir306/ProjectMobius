// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/SubSystems/PedestrianSignalSubsystem.h"
#include "MassSignalSubsystem.h"
#include "MassAI/SubSystems/MassEntitySpawnSubsystem.h"
#include "Subsystems/MobiusControllerSubsystem.h"

class UMassEntitySpawnSubsystem;

UPedestrianSignalSubsystem::UPedestrianSignalSubsystem()
{
}

void UPedestrianSignalSubsystem::CollisionsSettingChanged(uint8 EnableDisable)
{
	UE_LOG(LogTemp, Display, TEXT("Pedestrian CollisionsSettingChanged"));
	if (EnableDisable == 0)
	{
		DeactivateCollisions();
	}
	else if (EnableDisable == 1)
	{
		ActivateCollisions();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Pedestrian Signal Subsystem: Invalid EnableDisable value %d"), EnableDisable);
	}
}

void UPedestrianSignalSubsystem::ActivateCollisions()
{
	auto spawnSub = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>(); //TODO: convert to a pointer that we can use to avoid multiple calls to GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>()

	TArray<FMassEntityHandle> EntitiesToSignal;

	if (spawnSub)
	{
		EntitiesToSignal = spawnSub->SpawnedEntityPedestrianHandles;
	}
	
	auto SignalSubsystem =GetWorld()->GetSubsystem<UMassSignalSubsystem>();
	
	if (EntitiesToSignal.Num() == 0)
	{
		return;// No entities to signal, return early
	}
	if (SignalSubsystem)
	{
		SignalSubsystem->SignalEntities(PedestrianDataSignals::Signals::ActivateCollisions, EntitiesToSignal);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Pedestrian Signal Subsystem is not valid"));
	}
}

void UPedestrianSignalSubsystem::DeactivateCollisions()
{
	auto spawnSub = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>();
	
	TArray<FMassEntityHandle> EntitiesToSignal;
	
	if (spawnSub)
	{
		EntitiesToSignal = spawnSub->SpawnedEntityPedestrianHandles;
	}
	if (EntitiesToSignal.Num() == 0)
	{
		return;// No entities to signal, return early
	}
	
	auto SignalSubsystem =GetWorld()->GetSubsystem<UMassSignalSubsystem>();
	
	if (SignalSubsystem)
	{
		SignalSubsystem->SignalEntities(PedestrianDataSignals::Signals::DeactivateCollisions, EntitiesToSignal);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Pedestrian Signal Subsystem is not valid"));
	}
}

void UPedestrianSignalSubsystem::FlowCounterSettingChanged(uint8 EnableDisable)
{
	UE_LOG(LogTemp, Display, TEXT("Pedestrian Flow Counter %s"), EnableDisable == 0 ? TEXT("Deactivated") : TEXT("Activated"));
	if (EnableDisable == 0)
	{
		DeactivateFlowCounter();
	}
	else if (EnableDisable == 1)
	{
		ActivateFlowCounter();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Pedestrian Signal Subsystem: Invalid EnableDisable value %d"), EnableDisable);
	}
}

void UPedestrianSignalSubsystem::ActivateFlowCounter()
{
	auto spawnSub = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>();
	
	TArray<FMassEntityHandle> EntitiesToSignal;
	
	if (spawnSub)
	{
		EntitiesToSignal = spawnSub->SpawnedEntityPedestrianHandles;
	}
	if (EntitiesToSignal.Num() == 0)
	{
		return;// No entities to signal, return early
	}
	
	auto SignalSubsystem =GetWorld()->GetSubsystem<UMassSignalSubsystem>();
	
	if (SignalSubsystem)
	{
		SignalSubsystem->SignalEntities(PedestrianDataSignals::Signals::ActivateFlowCounter, EntitiesToSignal);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Pedestrian Signal Subsystem is not valid"));
	}
}

void UPedestrianSignalSubsystem::DeactivateFlowCounter()
{
	auto spawnSub = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>();
	
	TArray<FMassEntityHandle> EntitiesToSignal;
	
	if (spawnSub)
	{
		EntitiesToSignal = spawnSub->SpawnedEntityPedestrianHandles;
	}
	if (EntitiesToSignal.Num() == 0)
	{
		return;// No entities to signal, return early
	}
	
	auto SignalSubsystem =GetWorld()->GetSubsystem<UMassSignalSubsystem>();
	
	if (SignalSubsystem)
	{
		SignalSubsystem->SignalEntities(PedestrianDataSignals::Signals::DeactivateFlowCounter, EntitiesToSignal);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Pedestrian Signal Subsystem is not valid"));
	}
}

void UPedestrianSignalSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UPedestrianSignalSubsystem::Deinitialize()
{
	Super::Deinitialize();
}
