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
#include "MassAI/Subsystems/TimeDilationSubSystem.h"
#include "MassAI/SubSystems/AgentDataSubsystem.h"
// Tags required for the processor
#include "MassAI/Tags/MassAITags.h"
// Other includes
#include <Kismet/KismetMathLibrary.h>

#include "Subsystems/HeatmapSubsystem.h"

UPedestrianInitializeMOP::UPedestrianInitializeMOP()
{
	// Set the observed type fragments of this observer processor
	ObservedType = FEntityInfoFragment::StaticStruct();
	Operation = EMassObservedOperation::Add;
	//ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Avoidance);
	bRequiresGameThreadExecution = false;
}

void UPedestrianInitializeMOP::ConfigureQueries()
{
	// The required fragments for this processor
	EntityQuery.AddSharedRequirement<FSimulationFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);


	// The Entity Query Required fragments for this processor
	EntityQuery.AddRequirement<FEntityInfoFragment>(EMassFragmentAccess::ReadWrite);

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

		
		
		for(FEntityInfoFragment& EntityInfo : EntityInfoFragment)
		{
			// Set the Agent Info
			//InitializeEntityInfoAgent(EntityIndexOffset, EntityInfo);
			//InitializeEntityInfoAgent(EntityInfo, AllAgentMovementSamples[EntityIndexOffset].EntityID, AllAgentMovementSamples[EntityIndexOffset].En, TEXT("0"), 0.0f, TEXT("M_Plane"), 0);
			EntityInfo.CurrentLocation = AllAgentMovementSamples[EntityIndexOffset].Position;
			EntityInfo.CurrentRotation = AllAgentMovementSamples[EntityIndexOffset].Rotation;
			EntityIndexOffset++;
		}

		
		// loop through the movement data at timestep 0 and get the unique Z values
		for (int i = 0; i < SharedAgentMovement.SimulationData[0].Num(); i++)
		{
			float ZValue = AllAgentMovementSamples[i].Position.Z;
			if (!UniqueZValues.Contains(ZValue))
			{
				UniqueZValues.Add(ZValue);
			}
		}

		
		//
		// // Loop through all the entities and assign the pedestrian locations
		// for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); EntityIndex++)
		// {
		// 	FMassEntityHandle EntityHandle = Context.GetEntity(EntityIndex);
		//
		// 	// For now set spawn amount to greater than data size and destroy entities that are not in the data
		// 	if (EntityHandle.Index - 1 > DataSize)
		// 	{
		// 		// log
		// 		// UE_LOG(LogTemp, Warning, TEXT("PedestrianInitializeMOP::Execute Destroying Entity"));
		// 		//
		// 		// // log index and EntityHandle.Index
		// 		// UE_LOG(LogTemp, Warning, TEXT("PedestrianInitializeMOP::Execute EntityIndex: %d"), EntityIndex);
		// 		// UE_LOG(LogTemp, Warning, TEXT("PedestrianInitializeMOP::Execute EntityHandle.Index: %d"), EntityHandle.Index);
		// 		
		// 		// Destroy the entity
		// 		//Context.Defer().DestroyEntity(EntityHandle);
		// 		// TODO: at the momement the entity is being destroyed but the tag method is not working on visualization side
		// 		//Context.Defer().AddTag<FMassEntityDeleteTag>(EntityHandle);
		// 	}
		// 	else
		// 	{
		// 		// Get the first Shared Movement Sample for all entities
		// 		TArray<FSimMovementSample> AllAgentMovementSamples = SharedAgentMovement.SimulationData[0];
		// 		//TODO this was being set here but due to being unable to order the processors it is now being handled in the AgentRepresentation_MOP
		// 		//FSimMovementSample AgentMovementSamples = SharedAgentMovement.SimulationData[0][EntityIndex];
		//
		// 		// Set the Agent Info
		// 		InitializeEntityInfoAgent(EntityHandle.Index - 1, EntityInfoFragment[EntityIndex]);
		//
		// 		//TODO: change to use a lookup so no need to loop
		// 		for (int i = 0; i < AllAgentMovementSamples.Num(); i++)
		// 		{
		// 			if (AllAgentMovementSamples[i].EntityID == EntityInfoFragment[EntityIndex].EntityID)
		// 			{
		// 				EntityInfoFragment[EntityIndex].CurrentLocation = AllAgentMovementSamples[i].Position * 10;
		// 				EntityInfoFragment[EntityIndex].CurrentRotation = AllAgentMovementSamples[i].Rotation;
		// 				// This is an entity we want to render so add tag
		// 				//Context.Defer().AddTag<FMassEntityRepresentationTag>(EntityHandle);
		// 				//break;
		// 			}
		// 		}
		// 		
		// 	}
		// }
		//UE_LOG(LogTemp, Warning, TEXT("PedestrianInitializeMOP::Finished"));
		}));
	//EntityManager.FlushCommands();

	// once all the chunks have been processed we need to set the unique Z values in the heatmap subsystem
	if (UniqueZValues.Num() > 0)
	{
		// Get the heatmap subsystem

		if (UHeatmapSubsystem* HeatmapSubsystem = GetWorld()->GetSubsystem<UHeatmapSubsystem>())
		{
			HeatmapSubsystem->UpdateSpawnHeightLocations(UniqueZValues);
		}
	}
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
	EntityInfoToAssign.EntityID = InEntityID;
	EntityInfoToAssign.EntityName = InEntityName;
	EntityInfoToAssign.EntitySimTimeS = InEntitySimTimeS;
	EntityInfoToAssign.EntityMaxSpeed = InEntityMaxSpeed;
	EntityInfoToAssign.EntityM_Plane = InEntityM_Plane;
	EntityInfoToAssign.EntityMap = InEntityMap;
}

//void UPedestrianInitializeMOP::SetEntitiesPedestrianMovement(FPedestrianMovementFragment& PedestrianMovementToAssign, FSimMovementSample InSharedMovementData)
//{
//	PedestrianMovementToAssign.CurrentLocation = InSharedMovementData.Position;
//	PedestrianMovementToAssign.CurrentRotation = InSharedMovementData.Rotation;
//
//	//TODO: Review fragment codes as this is a mess and needs to be cleaned up
//	//PedestrianMovementToAssign.TargetLocation;
//}
