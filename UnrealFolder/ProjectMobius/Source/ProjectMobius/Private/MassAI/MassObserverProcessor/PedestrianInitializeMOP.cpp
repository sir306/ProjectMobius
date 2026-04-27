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

#include "MassAI/MassObserverProcessor/PedestrianInitializeMOP.h"
// Required headers for processing entities and there fragments
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassExternalSubsystemTraits.h" // This is needed so we can use subsystems and have no compile errors
// Fragments to include with this processor
#include "MassAI/Fragments/EntityInfoFragment.h"
// Shared Fragments to include with the processor
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
// Subsystems to include with the processor
#include "Subsystems/TimeDilationSubSystem.h"
#include "MassAI/SubSystems/AgentDataSubsystem.h"
// Tags required for the processor
#include "MassAI/Tags/MassAITags.h"
// Other includes
#include <Kismet/KismetMathLibrary.h>

#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/Actors/PedestrianCollisionHolder.h"

#include "Subsystems/HeatmapSubsystem.h"
#include "Subsystems/LoadingSubsystem.h"
#include "Subsystems/StatisticSubsystem.h"

UPedestrianInitializeMOP::UPedestrianInitializeMOP()
{
	// Set the observed type fragments of this observer processor
	ObservedType = FEntityInfoFragment::StaticStruct();
	Operation = EMassObservedOperation::Add;

	bRequiresGameThreadExecution = true;
}

void UPedestrianInitializeMOP::ConfigureQueries()
{
	// The required fragments for this processor
	EntityQuery.AddSharedRequirement<FSimulationFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);


	// The Entity Query Required fragments for this processor
	EntityQuery.AddRequirement<FEntityInfoFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FEntityMovementFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FEntityRenderingFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FEntityCollisionFragment>(EMassFragmentAccess::ReadWrite);

	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);

	// Register requirements for the processor
	ProcessorRequirements.AddSubsystemRequirement<UTimeDilationSubSystem>(EMassFragmentAccess::ReadOnly);

	// Heatmap module subsystem
	ProcessorRequirements.AddSubsystemRequirement<UHeatmapSubsystem>(EMassFragmentAccess::ReadWrite);

	//ProcessorRequirements.AddSubsystemRequirement<UAgentDataSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UPedestrianInitializeMOP::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	UE_LOG(LogTemp, Display, TEXT("UPedestrianInitializeMOP::Execute()"));
	// int offset value for the entity index this is so the intitial values can be assigned correctly
	int32 EntityIndexOffset = 0;

	// Get the current time dilation subsystem time step
	const UTimeDilationSubSystem* TimeDilationSubSystem = ExecutionContext.GetSubsystem<UTimeDilationSubSystem>();
	
	int32 CurrentTimeStep = TimeDilationSubSystem->GetCurrentTimeStep();

	// check current time step not less than 0
	if (CurrentTimeStep < 0)
	{
		// log
		UE_LOG(LogTemp, Warning, TEXT("PedestrianInitializeMOP::Execute CurrentTimeStep < 0"));
		return;
	}
	
	// Create a float array to hold all the unique Z values of the agents
	TArray<float> UniqueZValues;
	
	// get the mobius widget subsystem
	auto LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>();
	
	// check if the widget subsystem is valid
	if (LoadingSubsystem)
	{

		FString LoadingText = FString::Printf(TEXT("Building Pedestrian Movement AI Data..."));
		
		// Set the loading text and title
		LoadingSubsystem->SetLoadingText(true, LoadingText);
		LoadingSubsystem->BroadcastNewLoadPercent(0.0f);
	}
	
	// Get Max Agent Count from Agent Data Subsystem
	UAgentDataSubsystem* AgentDataSubsystem = GetWorld()->GetSubsystem<UAgentDataSubsystem>();
	int32 MaxAgentCount = AgentDataSubsystem->GetMaxAgents();
	
	
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this, &EntityIndexOffset, CurrentTimeStep, &UniqueZValues, LoadingSubsystem, MaxAgentCount](FMassExecutionContext& Context) {

		//UE_LOG(LogTemp, Warning, TEXT("PedestrianInitializeMOP::Execute"));

		// log the number of entities
		//UE_LOG(LogTemp, Warning, TEXT("PedestrianInitializeMOP::Execute NumEntities: %d"), Context.GetNumEntities());

		// Get the shared fragment
		auto& SharedAgentMovement = Context.GetMutableSharedFragment<FSimulationFragment>();

		// Get the required fragments
		const TArrayView<FEntityInfoFragment>& EntityInfoFragment = Context.GetMutableFragmentView<FEntityInfoFragment>();

		const TArrayView<FEntityMovementFragment>& EntityMovementFragment = Context.GetMutableFragmentView<FEntityMovementFragment>();
		const TArrayView<FEntityRenderingFragment>& EntityRenderingFragment = Context.GetMutableFragmentView<FEntityRenderingFragment>();

		// check timestep index is valid
		if (!SharedAgentMovement.SimulationData.IsValid() || SharedAgentMovement.SimulationData->Num() - 1 < CurrentTimeStep)
		{
			// log
			UE_LOG(LogTemp, Warning, TEXT("PedestrianInitializeMOP::Execute CurrentTimeStep not valid"));
			return;
		}

		// Get the size of the data
		//int32 DataSize = SharedAgentMovement.SimulationData->operator[](CurrentTimeStep).Num();

		// Get the first Shared Movement Sample for all entities
		TArray<FSimMovementSample> AllAgentMovementSamples = (*SharedAgentMovement.SimulationData)[CurrentTimeStep];

		auto Entities = Context.GetEntities();
		const TArrayView<FEntityCollisionFragment>& EntityCollisions = Context.GetMutableFragmentView<FEntityCollisionFragment>();

		//TODO: We have two methods that rely on the agent subsystem we should assign it to a variable and use it
		for (int i = 0; i < Entities.Num(); i++)
		{
			auto Entity = Entities[i];
			// Add the Mass Entity Representation Tag to current Entity - so we know the data is ready for render logic
			Context.Defer().AddTag<FMassEntityRepresentationTag>(Entity);

			auto& EntityInfo = EntityInfoFragment[i];
			
			// Set the Agent Info
			InitializeEntityInfoAgent(EntityIndexOffset, EntityInfo);

			auto& EntityMovement = EntityMovementFragment[i];
			// Set the Entity Movement Fragment with correct data
			EntityMovement.EntityID = EntityInfo.EntityID;

			//TODO: This is a poor assumption and should really be searching and checking
			// assumption is that the movement data is ordered
			
			float ZValue = 0.f;
			auto& EntityRendering = EntityRenderingFragment[i];
			/* Due to other simulation test data, it is possible that an entity index is out of range of the movement samples,
			 * this can be due to bad data or the agent doesn't actually exist yet
			 */ 
			if (AllAgentMovementSamples.IsValidIndex(EntityIndexOffset))
			{
				EntityMovement.CurrentLocation = AllAgentMovementSamples[EntityIndexOffset].Position;
				EntityMovement.CurrentRotation = AllAgentMovementSamples[EntityIndexOffset].Rotation;
				EntityMovement.CurrentSpeed = AllAgentMovementSamples[EntityIndexOffset].Speed;
				ZValue = AllAgentMovementSamples[EntityIndexOffset].Position.Z;
			}
			else
			{
				for (auto Sample : *SharedAgentMovement.SimulationData)
				{
					if (AllAgentMovementSamples.IsValidIndex(EntityIndexOffset))
					{
						EntityMovement.CurrentLocation = Sample.Value[EntityIndexOffset].Position;
						EntityMovement.CurrentRotation = Sample.Value[EntityIndexOffset].Rotation;
						EntityMovement.CurrentSpeed = Sample.Value[EntityIndexOffset].Speed;
						ZValue = Sample.Value[EntityIndexOffset].Position.Z;
						break;
					}
				}
				EntityRendering.bRenderAgent = false;// setting to false should prevent any rendering issues at start but our check in processor could be a problem
			}
			
			UAgentDataSubsystem* AgentDataSubsystem = GetWorld()->GetSubsystem<UAgentDataSubsystem>();			
			AgentDataSubsystem->SetEntityRenderingByIndex(EntityIndexOffset, EntityRendering);

			// check all movement samples so we can get all unique Z values
			if (!UniqueZValues.Contains(ZValue))
			{
				UniqueZValues.Add(ZValue);
			}

			// Get or spawn the pedestrian collision holder actor
			auto PedestrianCollisionHolder = Cast<APedestrianCollisionHolder>(UGameplayStatics::GetActorOfClass(GetWorld(), APedestrianCollisionHolder::StaticClass()));
			// If the holder is not found, we can spawn it
			if (!PedestrianCollisionHolder)
			{
				// Spawn the PedestrianCollisionHolder actor
				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				PedestrianCollisionHolder = GetWorld()->SpawnActor<APedestrianCollisionHolder>(APedestrianCollisionHolder::StaticClass(),
					FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
			}

			// Initialize the collision fragment
			FVector SpawnLocation = EntityMovement.CurrentLocation;
			//FRotator SpawnRotation = AllAgentMovementSamples[EntityIndexOffset].Rotation;
			EntityCollisions[i].Capsule = NewObject<UCapsuleComponent>(PedestrianCollisionHolder);
			EntityCollisions[i].Capsule->SetCapsuleHalfHeight(95.0f);
			EntityCollisions[i].Capsule->SetCapsuleRadius(40.0f);
			//EntityCollisions[i].Capsule->SetCollisionProfileName(TEXT("Pawn"));
			// Set the capsule to only block on the ECC_GameTraceChannel1 channel and ignore all others
			EntityCollisions[i].Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
			EntityCollisions[i].Capsule->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
			EntityCollisions[i].Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

			
			
			EntityCollisions[i].Capsule->SetGenerateOverlapEvents(true);

			// Add the component to the PedestrianCollisionHolder actor
			EntityCollisions[i].Capsule->AttachToComponent(PedestrianCollisionHolder->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			EntityCollisions[i].Capsule->RegisterComponent();

			// Set the transform of the capsule component
			SpawnLocation.Z = SpawnLocation.Z + 95.0f;
			EntityCollisions[i].Capsule->SetWorldLocation(SpawnLocation);
			
			EntityIndexOffset++;
			
			// check if the widget subsystem is valid
			if (LoadingSubsystem)
			{
				// Calculate load percent
				float LoadPercent = (float)(EntityIndexOffset) / (float)(MaxAgentCount);
				
				// Broadcast the load percent
				LoadingSubsystem->BroadcastNewLoadPercent(LoadPercent);
			}
		}
		
		
	}));

	//TODO: MOVE INTO OWN OBSERVOR
	// from world get all flow counters, if we have any then we need to add tags to our entities
	auto FlowCounters = GetWorld()->GetSubsystem<UStatisticSubsystem>()->GetFlowCounters();
	bool bValidCounters = false;
	for (auto FlowCounter : FlowCounters)
	{
		if (FlowCounter != nullptr)
		{
			bValidCounters = true;
			break;
		}
	}

	if (bValidCounters)
	{
		// We have valid flow counters, so we need to add the MassFlowCounterTag to all entities
		EntityQuery.ParallelForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context) {
			const TConstArrayView<FMassEntityHandle> Entities = Context.GetEntities();
			for (auto Entity : Entities)
			{
				Context.Defer().AddTag<FMassFlowCounterTag>(Entity);
			}
		}));
	}


	// once all the chunks have been processed we need to set the unique Z values in the heatmap subsystem
	if (UniqueZValues.Num() > 0)
	{
		// Get the heatmap subsystem

		if (UHeatmapSubsystem* HeatmapSubsystem = GetWorld()->GetSubsystem<UHeatmapSubsystem>())
		{
			HeatmapSubsystem->UpdateSpawnHeightLocations(UniqueZValues);
		}
	}
	ExecutionContext.FlushDeferred();	
}

void UPedestrianInitializeMOP::InitializeEntityInfoAgent(int32 InEntityID, FEntityInfoFragment& EntityInfoToAssign)
{
	UAgentDataSubsystem* AgentDataSubsystem = GetWorld()->GetSubsystem<UAgentDataSubsystem>();
	
	AgentDataSubsystem->SetEntityInfoByIndex(InEntityID, EntityInfoToAssign);
}

void UPedestrianInitializeMOP::InitializeEntityInfoAgent(FEntityInfoFragment& EntityInfoToAssign, int32 InEntityID, FString InEntityName, FString InEntitySimTimeS, float InEntityMaxSpeed, FString InEntityM_Plane, int32 InEntityMap)
{
	TSharedPtr<FJsonObject> TempObj = MakeShared<FJsonObject>();
	TempObj->SetNumberField(TEXT("id"), InEntityID);
	TempObj->SetStringField(TEXT("name"), InEntityName);
	TempObj->SetStringField(TEXT("simTimeS"), InEntitySimTimeS);
	TempObj->SetNumberField(TEXT("max_speed"), InEntityMaxSpeed);
	TempObj->SetStringField(TEXT("m_plane"), InEntityM_Plane);
	TempObj->SetNumberField(TEXT("map"), InEntityMap);

	UAgentDataSubsystem::ParseEntityInfo(TempObj, EntityInfoToAssign);
}
