// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/StatisticSubsystem.h"

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

void UStatisticSubsystem::UpdateAgentInfoMeshData(const TArray<FAgentMeshViewer>& AgentData)
{
	PedestrianAgentData = AgentData;
	// Notify listeners that the agent info has changed
	OnAgentInfoChanged.Broadcast();
}

void UStatisticSubsystem::UpdateSelectedAgentData(const FAgentMeshViewer& AgentData)
{
	SelectedAgentData = AgentData;
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
