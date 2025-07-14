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

#include "Subsystems/HeatmapSubsystem.h"

UPedestrianInitializeMOP::UPedestrianInitializeMOP()
{
	// Set the observed type fragments of this observer processor
	ObservedType = FEntityInfoFragment::StaticStruct();
	Operation = EMassObservedOperation::Add;

	bRequiresGameThreadExecution = false;
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
	
	
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this, &EntityIndexOffset, CurrentTimeStep, &UniqueZValues](FMassExecutionContext& Context) {

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
		if (SharedAgentMovement.SimulationData.Num() - 1 < CurrentTimeStep)
		{
			// log
			UE_LOG(LogTemp, Warning, TEXT("PedestrianInitializeMOP::Execute CurrentTimeStep not valid"));
			return;
		}

		// Get the size of the data
		//int32 DataSize = SharedAgentMovement.SimulationData[CurrentTimeStep].Num();

		// Get the first Shared Movement Sample for all entities
		TArray<FSimMovementSample> AllAgentMovementSamples = SharedAgentMovement.SimulationData[CurrentTimeStep];

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
			EntityMovement.CurrentLocation = AllAgentMovementSamples[EntityIndexOffset].Position;
			EntityMovement.CurrentRotation = AllAgentMovementSamples[EntityIndexOffset].Rotation;
			EntityMovement.CurrentSpeed = AllAgentMovementSamples[EntityIndexOffset].Speed;

			auto& EntityRendering = EntityRenderingFragment[i];
			
			UAgentDataSubsystem* AgentDataSubsystem = GetWorld()->GetSubsystem<UAgentDataSubsystem>();			
			AgentDataSubsystem->SetEntityRenderingByIndex(EntityIndexOffset, EntityRendering);

			// check all movement samples so we can get all unique Z values
			float ZValue = AllAgentMovementSamples[EntityIndexOffset].Position.Z;
			if (!UniqueZValues.Contains(ZValue))
			{
				UniqueZValues.Add(ZValue);
			}

			// Initialize the collision fragment
			FVector SpawnLocation = AllAgentMovementSamples[EntityIndexOffset].Position;
			//FRotator SpawnRotation = AllAgentMovementSamples[EntityIndexOffset].Rotation;
			EntityCollisions[i].Capsule = NewObject<UCapsuleComponent>();
			EntityCollisions[i].Capsule->SetCapsuleHalfHeight(95.0f);
			EntityCollisions[i].Capsule->SetCapsuleRadius(40.0f);
			EntityCollisions[i].Capsule->SetCollisionProfileName(TEXT("Pawn"));
			// Set the capsule to only block on the ECC_GameTraceChannel1 channel and ignore all others
			EntityCollisions[i].Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
			EntityCollisions[i].Capsule->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
			EntityCollisions[i].Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

			
			SpawnLocation.Z = SpawnLocation.Z + 95.0f;
			EntityCollisions[i].Capsule->SetWorldLocation(SpawnLocation);
			EntityCollisions[i].Capsule->SetGenerateOverlapEvents(true);
			
			EntityCollisions[i].Capsule->RegisterComponentWithWorld(GetWorld());
			
			EntityIndexOffset++;
		}
		
		
	}));


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
	// Get AgentDataSubsystem
	//auto& SubProcessorReqSubBits = ProcessorRequirements.GetRequiredConstSubsystems(); // TODO figure this out
	UAgentDataSubsystem* AgentDataSubsystem = GetWorld()->GetSubsystem<UAgentDataSubsystem>();
	
	AgentDataSubsystem->SetEntityInfoByIndex(InEntityID, EntityInfoToAssign);

	// // log the InEntityID
	// UE_LOG(LogTemp, Warning, TEXT("InEntityID: %d"), InEntityID);
	//
	// // Log the entity info
	// UE_LOG(LogTemp, Warning, TEXT("Entity ID: %d"), EntityInfoToAssign.EntityID);
	// UE_LOG(LogTemp, Warning, TEXT("Entity Name: %s"), *EntityInfoToAssign.EntityName);
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

//void UPedestrianInitializeMOP::SetEntitiesPedestrianMovement(FPedestrianMovementFragment& PedestrianMovementToAssign, FSimMovementSample InSharedMovementData)
//{
//	PedestrianMovementToAssign.CurrentLocation = InSharedMovementData.Position;
//	PedestrianMovementToAssign.CurrentRotation = InSharedMovementData.Rotation;
//
//	//TODO: Review fragment codes as this is a mess and needs to be cleaned up
//	//PedestrianMovementToAssign.TargetLocation;
//}
