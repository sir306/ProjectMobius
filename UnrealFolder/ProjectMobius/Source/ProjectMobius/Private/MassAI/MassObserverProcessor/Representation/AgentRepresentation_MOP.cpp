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

#include "MassAI/MassObserverProcessor/Representation/AgentRepresentation_MOP.h"
// Required headers for processing entities and there fragments
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassExternalSubsystemTraits.h" // This is needed so we can use subsystems and have no compile errors
# include "MassEntityView.h"
// Fragments
#include "MassAI/Fragments/EntityInfoFragment.h"
// Shared Fragments
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentRepresenatationFragment.h"
// Subsystems
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
// Tags
#include "MassAI/Tags/MassAITags.h"
// Instancing Static mesh components, materials, actors
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/Actors/AgentRepresentationActorISM.h"
#include "MassAI/Actors/NiagaraAgentRepActor.h"
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "Subsystems/TimeDilationSubSystem.h"
// Niagara
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraDataInterface.h"
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentNiagaraRepSharedFrag.h"

class UTimeDilationSubSystem;

UAgentRepresentation_MOP::UAgentRepresentation_MOP()
{
	ObservedType = FEntityInfoFragment::StaticStruct();
	Operation = EMassObservedOperation::Add;
	bRequiresGameThreadExecution = true;
	ProcessingPhase = EMassProcessingPhase::PrePhysics;

	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);
	
	// log processor name
	UE_LOG(LogTemp, Warning, TEXT("AgentRepresentation_MOP %s"), *GetProcessorName());

}

void UAgentRepresentation_MOP::ConfigureQueries()
{
	// Shared frags
	EntityQuery.AddSharedRequirement<FAgentRepresentationFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	EntityQuery.AddSharedRequirement<FSimulationFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::All);

	// Add the shared Niagara representation fragment
	EntityQuery.AddSharedRequirement<FAgentNiagaraRepSharedFrag>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	
	// Entity frags
	EntityQuery.AddRequirement<FEntityInfoFragment>(EMassFragmentAccess::ReadWrite);
	// Subsystems
	EntityQuery.AddSubsystemRequirement<UMRS_RepresentationSubsystem>(EMassFragmentAccess::ReadWrite);
	// Tags
	//EntityQuery.AddTagRequirement<FMassEntityRepresentationTag>(EMassFragmentPresence::Any); // If entity has tag, do process
	//EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::None); // If entity has delete tag, do not process

	EntityQuery.RegisterWithProcessor(*this);
}

void UAgentRepresentation_MOP::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	// check if execution context is in world
	if (!ExecutionContext.GetWorld())
	{
		return;
	}
	
	//EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, [this, &AgentRepresentationInstanceComp](FMassExecutionContext& Context)
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, [this](FMassExecutionContext& Context)
	{
		//UE_LOG(LogTemp, Warning, TEXT("UAgentRepresentation_MOP::Execute"));
		// Get the agent representation fragment
		FAgentRepresentationFragment& AgentRepresentationFragment = Context.GetMutableSharedFragment<FAgentRepresentationFragment>();

		// Get the pedestrian movement fragment
		const TArrayView<FEntityInfoFragment>& EntityInfoFragment = Context.GetMutableFragmentView<FEntityInfoFragment>();

		// Get the Niagara agent representation frag for the system
		FAgentNiagaraRepSharedFrag& AgentNiagaraRepSharedFrag = Context.GetMutableSharedFragment<FAgentNiagaraRepSharedFrag>();


		TObjectPtr<AAgentRepresentationActorISM> AgentRepresentationActor;
		
		//TODO: need to do index size and offset check here
		int32 CurrentInstanceTotal = AgentRepresentationFragment.NumberOfMaleAdults + AgentRepresentationFragment.NumberOfFemaleAdults +
			AgentRepresentationFragment.NumberOfChildren + AgentRepresentationFragment.NumberOfMaleElderly +
			AgentRepresentationFragment.NumberOfFemaleElderly;

		// TODO: this check will always fail on data swap as the frags are reset so sizes will be 0 like the offset
		// if the index offset is greater or less than the total then there is a miss match with current data the indexing should always be the same
		if (!EntityIndexOffset == CurrentInstanceTotal)
		{
			ResetDataInNiagaraSystem(AgentRepresentationFragment, AgentNiagaraRepSharedFrag);

			Context.GetMutableSharedFragment<FAgentRepresentationFragment>() = AgentRepresentationFragment;
			Context.GetMutableSharedFragment<FAgentNiagaraRepSharedFrag>() = AgentNiagaraRepSharedFrag;
		}
		
		// to avoid spawning multiple ISMs check that it has not already been spawned
		if (bHasSpawned)
		{
			// Get the shared actor component from the shared fragment
			AgentRepresentationActor = AgentRepresentationFragment.ActorRepresentationClass;
			
			TArray<AActor*> AgentRepresentationActors;
			
			UGameplayStatics::GetAllActorsOfClass(GetWorld(), AAgentRepresentationActorISM::StaticClass(), AgentRepresentationActors);
			
			// Get the Niagara actor
			auto FoundActor = UGameplayStatics::GetActorOfClass(GetWorld(), ANiagaraAgentRepActor::StaticClass());
			ANiagaraAgentRepActor* NiagaraAgentRepActor;
			
			if (FoundActor == nullptr) 
			{
				// Spawn the Niagara actor
				NiagaraAgentRepActor = Context.GetWorld()->SpawnActor<ANiagaraAgentRepActor>(FVector(0, 0, 0), FRotator(0, 0, 0));
			}
			else
			{
				NiagaraAgentRepActor = Cast<ANiagaraAgentRepActor>(FoundActor);
			}

			NiagaraAgentRepActor = GetOrCreateNiagaraRepActor(GetWorld());
			
			// Set the shared actor component in the shared fragment
			AgentRepresentationFragment.NiagaraAgentRepActor = NiagaraAgentRepActor;

			// DEACTIVATE THE NIAGARA SYSTEM
			AgentRepresentationFragment.NiagaraAgentRepActor->GetNiagaraComponent()->DeactivateImmediate();


			// get time dilation subsystem current time step
			int32 CurrentTimeStep = GetWorld()->GetSubsystem<UTimeDilationSubSystem>()->GetCurrentTimeStep();

			
			
			for(FEntityInfoFragment& EntityInfo : EntityInfoFragment)
			{
				// Set the Agent Info
				InitializeEntityInfoAgent(EntityIndexOffset, EntityInfo);
				
				for(FSimMovementSample MovementSample : Context.GetSharedFragment<FSimulationFragment>().SimulationData[CurrentTimeStep])
				{
					if(MovementSample.EntityID == EntityInfo.EntityID)
					{
						EntityInfo.CurrentLocation = MovementSample.Position;
						EntityInfo.CurrentRotation = MovementSample.Rotation;
						break;
					}
				}
				
				
				EntityIndexOffset++;
				// log index
				//UE_LOG(LogTemp, Warning, TEXT("UAgentRepresentation_MOP::EntityIndexOffset: %d"), EntityIndexOffset);

			
				// TODO
				// Get the float value for the agent variation
				float AgentVariationFloat = FMath::FRandRange(0.0f, 20.0f);

				// Process the entity and set up the corresponding niagara system for the demographic of this entity
				ProcessEntity(EntityInfo, AgentRepresentationFragment, AgentNiagaraRepSharedFrag);
				
			}
			
		}
		else if (!bHasSpawned)
		{
			// Spawn the Niagara actor
			ANiagaraAgentRepActor* NiagaraAgentRepActor = Context.GetWorld()->SpawnActor<ANiagaraAgentRepActor>(FVector(0, 0, 0), FRotator(0, 0, 0));

			//TODO: this is temp fix we already have this set in the subsystem we just need to spawn it + need to sort the number to be the same for genders etc -> Refactor this:repeated code for this actor so make function
			// Create the Niagara System
			UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(StaticLoadObject(UNiagaraSystem::StaticClass(), NULL, TEXT("NiagaraSystem'/Game/01_Dev/PedestrianMovement/NiagaraConversion/NS_InstancedPedestrianAgent.NS_InstancedPedestrianAgent'")));
			
			// Set the Niagara System
			NiagaraAgentRepActor->GetNiagaraComponent()->SetAsset(NiagaraSystem);
			
			// Set the shared actor component in the shared fragment
			AgentRepresentationFragment.NiagaraAgentRepActor = NiagaraAgentRepActor;
			
			// flag to see if the initial spawn has been done
			bHasSpawned = true;

			for(FEntityInfoFragment& EntityInfo : EntityInfoFragment)
			{
				// Set the Agent Info
				InitializeEntityInfoAgent(EntityIndexOffset, EntityInfo);
				EntityIndexOffset++;
				// log index
				//UE_LOG(LogTemp, Warning, TEXT("UAgentRepresentation_MOP::EntityIndexOffset: %d"), EntityIndexOffset);

				
				// TODO
				// Get the float value for the agent variation
				float AgentVariationFloat = FMath::FRandRange(0.0f, 20.0f);

				// Process the entity and set up the corresponding niagara system for the demographic of this entity
				ProcessEntity(EntityInfo, AgentRepresentationFragment, AgentNiagaraRepSharedFrag);
				
			}
			
		}

		AgentRepresentationFragment.NiagaraAgentRepActor->GetNiagaraComponent()->ClearSimCache();

		// get the niagara variables for number of agents
		
		// Activate the Niagara System
		AgentRepresentationFragment.NiagaraAgentRepActor->GetNiagaraComponent()->Activate(true);

		// Set the number of agents in the system
		AgentRepresentationFragment.NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableInt(TEXT("MaleAdultAgentNumber"), AgentRepresentationFragment.NumberOfMaleAdults);
		AgentRepresentationFragment.NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableInt(TEXT("ElderlyMaleAgentNumber"), AgentRepresentationFragment.NumberOfMaleElderly);
		AgentRepresentationFragment.NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableInt(TEXT("FemaleAdultAgentNumber"), AgentRepresentationFragment.NumberOfFemaleAdults);
		AgentRepresentationFragment.NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableInt(TEXT("ElderlyFemaleAgentNumber"), AgentRepresentationFragment.NumberOfFemaleElderly);
		AgentRepresentationFragment.NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableInt(TEXT("ChildNumberOfAgents"), AgentRepresentationFragment.NumberOfChildren);
		

		// log the number of agents
		//UE_LOG(LogTemp, Warning, TEXT("MaleNumberOfAgents: %d"), AgentRepresentationFragment.NumberOfMaleAdults);
		//UE_LOG(LogTemp, Warning, TEXT("FemaleNumberOfAgents: %d"), AgentRepresentationFragment.NumberOfFemaleAdults);
		//UE_LOG(LogTemp, Warning, TEXT("ChildNumberOfAgents: %d"), AgentRepresentationFragment.NumberOfChildren);
		
		
    	
		//UE_LOG(LogTemp, Warning, TEXT("UAgentRepresentation_MOP::Finished"));
	});
	
}

void UAgentRepresentation_MOP::DefaultEntitySetup(const TArrayView<FEntityInfoFragment>& EntityInfoFrag, FAgentRepresentationFragment& AgentRepresentationFragment, UInstancedStaticMeshComponent* MaleISMComponent, UInstancedStaticMeshComponent* FemaleISMComponent, int32 EntityIndexOffst)
{
	
	// Set the static meshes for the components
	MaleISMComponent->SetStaticMesh(AgentRepresentationFragment.MaleStaticMesh);
	FemaleISMComponent->SetStaticMesh(AgentRepresentationFragment.FemaleStaticMesh);

    	
	for(FEntityInfoFragment& EntityInfo : EntityInfoFrag)
	{
		// Set the Agent Info
		InitializeEntityInfoAgent(EntityIndexOffset, EntityInfo);


		// TODO
		// Get the float value for the agent variation
		float AgentVariationFloat = FMath::FRandRange(0.0f, 20.0f);

		// New Instance Index
		int32 InstanceIndex = 0;

		// is the name Male[n] or Female[n]
		if (!EntityInfo.bIsMale)// We use female as the check so male doesn't return true on females
		{
			InstanceIndex = AddInstanceToISMComponent(FemaleISMComponent, FTransform(EntityInfo.CurrentRotation, EntityInfo.CurrentLocation, FVector(1.0f)));
			EntityInfo.InstanceID = InstanceIndex;
			FemaleISMComponent->SetCustomDataValue(InstanceIndex, 5, AgentVariationFloat); // This is the person variation
		}
		else
		{
			InstanceIndex = AddInstanceToISMComponent(MaleISMComponent, FTransform(EntityInfo.CurrentRotation, EntityInfo.CurrentLocation, FVector(1.0f)));
			EntityInfo.InstanceID = InstanceIndex;
			MaleISMComponent->SetCustomDataValue(InstanceIndex, 5, AgentVariationFloat); // This is the person variation
		}

		EntityIndexOffset++;
	}
}

void UAgentRepresentation_MOP::ProcessEntity(FEntityInfoFragment& EntityInfo,
                                             FAgentRepresentationFragment& AgentFrag, FAgentNiagaraRepSharedFrag& NiagaraFrag)
{
	FVector4 LocationScale = FVector4(EntityInfo.CurrentLocation, 1.0f);
	FQuat RotationQuat = EntityInfo.CurrentRotation.Quaternion();
	int32 AnimationState = 0;

	if (EntityInfo.bIsMale)
	{
		switch (EntityInfo.AgeDemographic)
		{
		case EAgeDemographic::Ead_Child:
			EntityInfo.InstanceID = AgentFrag.NumberOfChildren++; // ++ on right side of assignment means post increment
			NiagaraFrag.ChildrenAgentLocationAndScales.Add(LocationScale);
			NiagaraFrag.ChildrenAgentRotations.Add(RotationQuat);
			NiagaraFrag.ChildrenAnimationStates.Add(AnimationState);
			break;

		case EAgeDemographic::Ead_Elderly:
	
			EntityInfo.InstanceID = AgentFrag.NumberOfMaleElderly++;
			NiagaraFrag.ElderlyMaleAdultAgentLocationAndScales.Add(LocationScale);
			NiagaraFrag.ElderlyMaleAdultAgentRotations.Add(RotationQuat);
			NiagaraFrag.ElderlyMaleAdultAnimationStates.Add(AnimationState);
			break;
		case EAgeDemographic::Ead_Adult:
			EntityInfo.InstanceID = AgentFrag.NumberOfMaleAdults++;
			NiagaraFrag.MaleAdultAgentLocationAndScales.Add(LocationScale);
			NiagaraFrag.MaleAdultAgentRotations.Add(RotationQuat);
			NiagaraFrag.MaleAdultAnimationStates.Add(AnimationState);
			break;
		case EAgeDemographic::Ead_Default:
			break;
		}
	}
	else
	{
		switch (EntityInfo.AgeDemographic)
		{
		case EAgeDemographic::Ead_Child: // TODO: no female children yet
			EntityInfo.InstanceID = AgentFrag.NumberOfChildren++; // ++ on right side of assignment means post increment
			NiagaraFrag.ChildrenAgentLocationAndScales.Add(LocationScale);
			NiagaraFrag.ChildrenAgentRotations.Add(RotationQuat);
			NiagaraFrag.ChildrenAnimationStates.Add(AnimationState);
			break;

		case EAgeDemographic::Ead_Elderly:
			EntityInfo.InstanceID = AgentFrag.NumberOfFemaleElderly++;
			NiagaraFrag.ElderlyFemaleAdultAgentLocationAndScales.Add(LocationScale);
			NiagaraFrag.ElderlyFemaleAdultAgentRotations.Add(RotationQuat);
			NiagaraFrag.ElderlyFemaleAdultAnimationStates.Add(AnimationState);
			break;
		case EAgeDemographic::Ead_Adult:
			EntityInfo.InstanceID = AgentFrag.NumberOfFemaleAdults++;
			NiagaraFrag.FemaleAdultAgentLocationAndScales.Add(LocationScale);
			NiagaraFrag.FemaleAdultAgentRotations.Add(RotationQuat);
			NiagaraFrag.FemaleAdultAnimationStates.Add(AnimationState);
			break;
		case EAgeDemographic::Ead_Default:
			break;
		}
	}
}

void UAgentRepresentation_MOP::ResetDataInNiagaraSystem(FAgentRepresentationFragment& AgentFrag,
                                                        FAgentNiagaraRepSharedFrag& NiagaraFrag)
{
	AgentFrag.NumberOfFemaleAdults = 0;
	AgentFrag.NumberOfFemaleElderly = 0;
	AgentFrag.NumberOfMaleAdults = 0;
	AgentFrag.NumberOfMaleElderly = 0;
	AgentFrag.NumberOfChildren = 0;
	EntityIndexOffset = 0;

#define RESET_NIAGARA_FRAG(Field) NiagaraFrag.Field.Reset()

	RESET_NIAGARA_FRAG(FemaleAdultAgentRotations);
	RESET_NIAGARA_FRAG(FemaleAdultAgentLocationAndScales);
	RESET_NIAGARA_FRAG(FemaleAdultAnimationStates);
	RESET_NIAGARA_FRAG(ElderlyFemaleAdultAgentRotations);
	RESET_NIAGARA_FRAG(ElderlyFemaleAdultAgentLocationAndScales);
	RESET_NIAGARA_FRAG(ElderlyFemaleAdultAnimationStates);
	RESET_NIAGARA_FRAG(MaleAdultAgentRotations);
	RESET_NIAGARA_FRAG(MaleAdultAgentLocationAndScales);
	RESET_NIAGARA_FRAG(MaleAdultAnimationStates);
	RESET_NIAGARA_FRAG(ElderlyMaleAdultAgentRotations);
	RESET_NIAGARA_FRAG(ElderlyMaleAdultAgentLocationAndScales);
	RESET_NIAGARA_FRAG(ElderlyMaleAdultAnimationStates);
	RESET_NIAGARA_FRAG(ChildrenAgentRotations);
	RESET_NIAGARA_FRAG(ChildrenAgentLocationAndScales);
	RESET_NIAGARA_FRAG(ChildrenAnimationStates);

	if (AgentFrag.NiagaraAgentRepActor && AgentFrag.NiagaraAgentRepActor->GetNiagaraComponent())
	{
		AgentFrag.NiagaraAgentRepActor->GetNiagaraComponent()->Deactivate();
		
	}
}

ANiagaraAgentRepActor* UAgentRepresentation_MOP::GetOrCreateNiagaraRepActor(UWorld* World)
{
	auto* ExistingActor = Cast<ANiagaraAgentRepActor>(UGameplayStatics::GetActorOfClass(World, ANiagaraAgentRepActor::StaticClass()));
	if (ExistingActor)
		return ExistingActor;

	return World->SpawnActor<ANiagaraAgentRepActor>(FVector::ZeroVector, FRotator::ZeroRotator);
}

int32 UAgentRepresentation_MOP::AddInstanceToISMComponent(UInstancedStaticMeshComponent* ISMComponent,
                                                          const FTransform& InstanceTransform)
{
	const int32 InstanceIndex = ISMComponent->AddInstance(InstanceTransform);
	// ISMComponent->SetCustomDataValue(InstanceIndex, 0,340.0f);// FrameOffset
	// ISMComponent->SetCustomDataValue(InstanceIndex, 1, 37.0f);// NumFrames  this number looks right for the current VAT
	// ISMComponent->SetCustomDataValue(InstanceIndex, 2, 1.0f); // playrate
	// ISMComponent->SetCustomDataValue(InstanceIndex, 3, 1.0f); // bIslooping
	// ISMComponent->SetCustomDataValue(InstanceIndex, 4, 0); // this is time offset
	ISMComponent->SetCustomDataValue(InstanceIndex, 1, 1.0f);
	ISMComponent->SetCustomDataValue(InstanceIndex, 2, 2.0f);
	ISMComponent->SetCustomDataValue(InstanceIndex, 3, 14.0f);

	return InstanceIndex;
}

void UAgentRepresentation_MOP::InitializeEntityInfoAgent(int32 InEntityID, FEntityInfoFragment& EntityInfoToAssign)
{
	// Get AgentDataSubsystem
	//auto& SubProcessorReqSubBits = ProcessorRequirements.GetRequiredConstSubsystems(); // TODO figure this out
	UAgentDataSubsystem* AgentDataSubsystem = GetWorld()->GetSubsystem<UAgentDataSubsystem>();
	
	AgentDataSubsystem->SetEntityInfoByIndex(InEntityID, EntityInfoToAssign);
}

void UAgentRepresentation_MOP::InitializeEntityInfoAgent(FEntityInfoFragment& EntityInfoToAssign, int32 InEntityID, FString InEntityName, FString InEntitySimTimeS, float InEntityMaxSpeed, FString InEntityM_Plane, int32 InEntityMap)
{
	EntityInfoToAssign.EntityID = InEntityID;
	EntityInfoToAssign.EntityName = InEntityName;
	EntityInfoToAssign.EntitySimTimeS = InEntitySimTimeS;
	EntityInfoToAssign.EntityMaxSpeed = InEntityMaxSpeed;
	EntityInfoToAssign.EntityM_Plane = InEntityM_Plane;
	EntityInfoToAssign.EntityMap = InEntityMap;
}
