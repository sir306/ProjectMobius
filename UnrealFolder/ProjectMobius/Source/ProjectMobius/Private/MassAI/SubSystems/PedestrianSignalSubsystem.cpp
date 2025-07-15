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

void UPedestrianSignalSubsystem::CollisionsSettingChanged2(bool EnableDisable)
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
	auto SignalSubsystem =GetWorld()->GetSubsystem<UMassSignalSubsystem>();
	// entities might be getting mutated and not working correctly
	auto spawnSub = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>();
	if (spawnSub)
	{
		EntitiesToSignal = spawnSub->SpawnedEntityPedestrianHandles;
	}
	
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
	auto SignalSubsystem =GetWorld()->GetSubsystem<UMassSignalSubsystem>();
	if (EntitiesToSignal.Num() == 0)
	{
		return;// No entities to signal, return early
	}
	if (SignalSubsystem)
	{
		SignalSubsystem->SignalEntities(PedestrianDataSignals::Signals::DeactivateCollisions, EntitiesToSignal);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Pedestrian Signal Subsystem is not valid"));
	}
}

void UPedestrianSignalSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	auto MobiusControllerSubsystem = GetWorld()->GetSubsystem<UMobiusControllerSubsystem>();
	if (MobiusControllerSubsystem)
	{
		// Subscribe to the collision setting changed delegate
		MobiusControllerSubsystem->OnPedestrianCollisionSettingChanged.AddDynamic(this, &UPedestrianSignalSubsystem::CollisionsSettingChanged);
		MobiusControllerSubsystem->OnPedestrianCollisionSettingChanged2.AddDynamic(this, &UPedestrianSignalSubsystem::CollisionsSettingChanged2);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("MobiusControllerSubsystem is not valid"));
	}
	
	Super::Initialize(Collection);
}

void UPedestrianSignalSubsystem::Deinitialize()
{
	// Unsubscribe from the collision setting changed delegate
	auto MobiusControllerSubsystem = GetWorld()->GetSubsystem<UMobiusControllerSubsystem>();
	if (MobiusControllerSubsystem)
	{
		MobiusControllerSubsystem->OnPedestrianCollisionSettingChanged.RemoveDynamic(this, &UPedestrianSignalSubsystem::CollisionsSettingChanged);
	}
	
	Super::Deinitialize();
}
