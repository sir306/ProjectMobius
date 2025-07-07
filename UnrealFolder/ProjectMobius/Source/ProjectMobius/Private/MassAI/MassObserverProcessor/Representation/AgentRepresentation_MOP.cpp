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
//#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "Subsystems/TimeDilationSubSystem.h"
// Niagara
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraDataInterface.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentNiagaraDataFrag.h"

class UTimeDilationSubSystem;

UAgentRepresentation_MOP::UAgentRepresentation_MOP()
{
	ObservedType = FMassEntityRepresentationTag::StaticStruct();
	Operation = EMassObservedOperation::Add;
	bRequiresGameThreadExecution = true;

	// log processor name
	UE_LOG(LogTemp, Warning, TEXT("AgentRepresentation_MOP %s"), *GetProcessorName());

}

void UAgentRepresentation_MOP::ConfigureQueries()
{
	// Shared frags
	EntityQuery.AddSharedRequirement<FSimulationFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::All);
	// Add the shared Niagara representation fragment
	EntityQuery.AddSharedRequirement<FAgentNiagaraDataFrag>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	EntityQuery.AddSharedRequirement<FNiagaraStatsFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	
	// Entity frags
	EntityQuery.AddRequirement<FEntityMovementFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FEntityRenderingFragment>(EMassFragmentAccess::ReadWrite);
	
	// Subsystems
	EntityQuery.AddSubsystemRequirement<UMRS_RepresentationSubsystem>(EMassFragmentAccess::ReadWrite);
	// Tags
	EntityQuery.AddTagRequirement<FMassEntityRepresentationTag>(EMassFragmentPresence::All); // If all entities have tag, do process
	//EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::None); // If entity has delete tag, do not process

	EntityQuery.RegisterWithProcessor(*this);
}

void UAgentRepresentation_MOP::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	//UE_LOG(LogTemp, Display, TEXT("UAgentRepresentation_MOP::Execute()"));
	// check if execution context is in world
	if (!ExecutionContext.GetWorld())
	{
		return;
	}
	if (Cast<ANiagaraAgentRepActor>(UGameplayStatics::GetActorOfClass(GetWorld(), ANiagaraAgentRepActor::StaticClass())))
	{
		bHasSpawned = true;
	}
	else
	{
		bHasSpawned = false;
	}
	
	//EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, [this, &AgentRepresentationInstanceComp](FMassExecutionContext& Context)
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, [this](FMassExecutionContext& Context)
	{
		//UE_LOG(LogTemp, Warning, TEXT("UAgentRepresentation_MOP::Execute"));
		
               // Get the entity movement and rendering fragments
		TConstArrayView<FEntityMovementFragment> EntityMovementFragment = Context.GetFragmentView<FEntityMovementFragment>();
		const TArrayView<FEntityRenderingFragment>& EntityRenderingFragment = Context.GetMutableFragmentView<FEntityRenderingFragment>();

		// Get the Niagara agent representation frag for the system
		FAgentNiagaraDataFrag& AgentNiagaraDataSharedFrag = Context.GetMutableSharedFragment<FAgentNiagaraDataFrag>();
		FNiagaraStatsFragment& AgentNiagaraStatsSharedFrag = Context.GetMutableSharedFragment<FNiagaraStatsFragment>();
		
		//TODO: need to do index size and offset check here
		int32 CurrentInstanceTotal = AgentNiagaraStatsSharedFrag.NumberOfMaleAdults + AgentNiagaraStatsSharedFrag.NumberOfFemaleAdults +
			AgentNiagaraStatsSharedFrag.NumberOfChildren + AgentNiagaraStatsSharedFrag.NumberOfMaleElderly +
			AgentNiagaraStatsSharedFrag.NumberOfFemaleElderly;

		// TODO: this check will always fail on data swap as the frags are reset so sizes will be 0 like the offset
		// if the index offset is greater or less than the total then there is a miss match with current data the indexing should always be the same
		if (!EntityIndexOffset == CurrentInstanceTotal)
		{
			ResetDataInNiagaraSystem(AgentNiagaraStatsSharedFrag, AgentNiagaraDataSharedFrag);

			Context.GetMutableSharedFragment<FNiagaraStatsFragment>() = AgentNiagaraStatsSharedFrag;
			Context.GetMutableSharedFragment<FAgentNiagaraDataFrag>() = AgentNiagaraDataSharedFrag;
		}
		
		// to avoid spawning multiple ISMs check that it has not already been spawned
		if (bHasSpawned)
		{
			ANiagaraAgentRepActor* NiagaraAgentRepActor;

			// Get the Niagara actor
			NiagaraAgentRepActor = GetOrCreateNiagaraRepActor(GetWorld());
			
			// Set the shared actor component in the shared fragment
			AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor = NiagaraAgentRepActor;

			// DEACTIVATE THE NIAGARA SYSTEM
			AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent()->DeactivateImmediate();
			
			// get time dilation subsystem current time step
			int32 CurrentTimeStep = GetWorld()->GetSubsystem<UTimeDilationSubSystem>()->GetCurrentTimeStep();


			auto Entities = Context.GetEntities();
		
			for (int i = 0; i < Entities.Num(); i++)
			{
				auto Entity = Entities[i];

				// TODO
				// Get the float value for the agent variation
				float AgentVariationFloat = FMath::FRandRange(0.0f, 20.0f);

				auto& EntityMovement = EntityMovementFragment[i];
				auto& EntityRendering = EntityRenderingFragment[i];
				// Process the entity and set up the corresponding niagara system for the demographic of this entity
				ProcessEntity(EntityMovement,EntityRendering, AgentNiagaraStatsSharedFrag, AgentNiagaraDataSharedFrag);
				
				EntityIndexOffset++;
			}
			
		}
		else if (!bHasSpawned)
		{
			// TEST update effect to use low spec effect
			//AgentRepresentationFragment.bUseLowSpecAgentRenderEffect = true;
			
			// Spawn the Niagara actor
			ANiagaraAgentRepActor* NiagaraAgentRepActor = Context.GetWorld()->SpawnActor<ANiagaraAgentRepActor>(FVector(0, 0, 0), FRotator(0, 0, 0));

			// Get the MRS subsystem
			UMRS_RepresentationSubsystem* MRSSubsystem = Context.GetWorld()->GetSubsystem<UMRS_RepresentationSubsystem>();

			// Check if the Niagara Stats frag matches the render effect type
			if (MRSSubsystem->IsCurrentPedestrianAvatarTypeLowSpec() != AgentNiagaraStatsSharedFrag.bUseLowSpecAgentRenderEffect)
			{
				// If the Niagara Stats frag does not match the render effect type, then set it on the Niagara Stats frag
				AgentNiagaraStatsSharedFrag.bUseLowSpecAgentRenderEffect = MRSSubsystem->IsCurrentPedestrianAvatarTypeLowSpec();
			}
			
			// Create the Niagara System
			//UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(StaticLoadObject(UNiagaraSystem::StaticClass(), NULL, TEXT("NiagaraSystem'/Game/01_Dev/PedestrianMovement/NiagaraConversion/NS_InstancedPedestrianAgent.NS_InstancedPedestrianAgent'")));
			UNiagaraSystem* NiagaraSystem = MRSSubsystem->LoadNiagaraAgentSystem(AgentNiagaraStatsSharedFrag.bUseLowSpecAgentRenderEffect);

			if (NiagaraSystem == nullptr)
			{
				// Log error if the Niagara System could not be loaded
				UE_LOG(LogTemp, Error, TEXT("Failed to load Niagara System for Agent Representation"));
				return;
			}

			// Set the Niagara System
			NiagaraAgentRepActor->GetNiagaraComponent()->SetAsset(NiagaraSystem);
			
			// Set the shared actor component in the shared fragment
			AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor = NiagaraAgentRepActor;
			
			// flag to see if the initial spawn has been done
			bHasSpawned = true;

			auto Entities = Context.GetEntities();
		
			for (int i = 0; i < Entities.Num(); i++)
			{
				auto Entity = Entities[i];

				// TODO
				// Get the float value for the agent variation
				float AgentVariationFloat = FMath::FRandRange(0.0f, 20.0f);

				auto& EntityMovement = EntityMovementFragment[i];
				auto& EntityRendering = EntityRenderingFragment[i];
				// Process the entity and set up the corresponding niagara system for the demographic of this entity
				ProcessEntity(EntityMovement,EntityRendering, AgentNiagaraStatsSharedFrag, AgentNiagaraDataSharedFrag);
				
				EntityIndexOffset++;
			}
			
		}

		AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent()->ClearSimCache();

		// get the niagara variables for number of agents
		
		// Activate the Niagara System
		AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent()->Activate(true);

		// Set the number of agents in the system
		AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent()->SetVariableInt(TEXT("MaleAdultAgentNumber"), AgentNiagaraStatsSharedFrag.NumberOfMaleAdults);
		AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent()->SetVariableInt(TEXT("ElderlyMaleAgentNumber"), AgentNiagaraStatsSharedFrag.NumberOfMaleElderly);
		AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent()->SetVariableInt(TEXT("FemaleAdultAgentNumber"), AgentNiagaraStatsSharedFrag.NumberOfFemaleAdults);
		AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent()->SetVariableInt(TEXT("ElderlyFemaleAgentNumber"), AgentNiagaraStatsSharedFrag.NumberOfFemaleElderly);
		AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent()->SetVariableInt(TEXT("ChildNumberOfAgents"), AgentNiagaraStatsSharedFrag.NumberOfChildren);
		

		// log the number of agents
		//UE_LOG(LogTemp, Warning, TEXT("MaleNumberOfAgents: %d"), AgentRepresentationFragment.NumberOfMaleAdults);
		//UE_LOG(LogTemp, Warning, TEXT("FemaleNumberOfAgents: %d"), AgentRepresentationFragment.NumberOfFemaleAdults);
		//UE_LOG(LogTemp, Warning, TEXT("ChildNumberOfAgents: %d"), AgentRepresentationFragment.NumberOfChildren);
		
		//UE_LOG(LogTemp, Warning, TEXT("UAgentRepresentation_MOP::Finished"));
	});
	
}

// Consume movement and rendering fragments to fill Niagara arrays and update counts.
void UAgentRepresentation_MOP::ProcessEntity(const FEntityMovementFragment& EntityMovementFrag, FEntityRenderingFragment& EntityRenderingFrag,
                                             FNiagaraStatsFragment& NiagaraStatsFrag, FAgentNiagaraDataFrag& NiagaraDataFrag)
{
	FVector4 LocationScale = FVector4(EntityMovementFrag.CurrentLocation, 1.0f);
	FQuat RotationQuat = EntityMovementFrag.CurrentRotation.Quaternion();
	int32 AnimationState = 0;

	if (EntityRenderingFrag.bIsMale)
	{
		switch (EntityRenderingFrag.AgeDemographic)
		{
		case EAgeDemographic::Ead_Child:
			EntityRenderingFrag.InstanceID = NiagaraStatsFrag.NumberOfChildren++; // ++ on right side of assignment means post increment
			NiagaraDataFrag.ChildrenAgentLocationAndScales.Add(LocationScale);
			NiagaraDataFrag.ChildrenAgentRotations.Add(RotationQuat);
			NiagaraDataFrag.ChildrenAnimationStates.Add(AnimationState);
			break;

		case EAgeDemographic::Ead_Elderly:
	
			EntityRenderingFrag.InstanceID = NiagaraStatsFrag.NumberOfMaleElderly++;
			NiagaraDataFrag.ElderlyMaleAdultAgentLocationAndScales.Add(LocationScale);
			NiagaraDataFrag.ElderlyMaleAdultAgentRotations.Add(RotationQuat);
			NiagaraDataFrag.ElderlyMaleAdultAnimationStates.Add(AnimationState);
			break;
		case EAgeDemographic::Ead_Adult:
			EntityRenderingFrag.InstanceID = NiagaraStatsFrag.NumberOfMaleAdults++;
			NiagaraDataFrag.MaleAdultAgentLocationAndScales.Add(LocationScale);
			NiagaraDataFrag.MaleAdultAgentRotations.Add(RotationQuat);
			NiagaraDataFrag.MaleAdultAnimationStates.Add(AnimationState);
			break;
		case EAgeDemographic::Ead_Default:
			break;
		}
	}
	else
	{
		switch (EntityRenderingFrag.AgeDemographic)
		{
		case EAgeDemographic::Ead_Child: // TODO: no female children yet
			EntityRenderingFrag.InstanceID = NiagaraStatsFrag.NumberOfChildren++; // ++ on right side of assignment means post increment
			NiagaraDataFrag.ChildrenAgentLocationAndScales.Add(LocationScale);
			NiagaraDataFrag.ChildrenAgentRotations.Add(RotationQuat);
			NiagaraDataFrag.ChildrenAnimationStates.Add(AnimationState);
			break;

		case EAgeDemographic::Ead_Elderly:
			EntityRenderingFrag.InstanceID = NiagaraStatsFrag.NumberOfFemaleElderly++;
			NiagaraDataFrag.ElderlyFemaleAdultAgentLocationAndScales.Add(LocationScale);
			NiagaraDataFrag.ElderlyFemaleAdultAgentRotations.Add(RotationQuat);
			NiagaraDataFrag.ElderlyFemaleAdultAnimationStates.Add(AnimationState);
			break;
		case EAgeDemographic::Ead_Adult:
			EntityRenderingFrag.InstanceID = NiagaraStatsFrag.NumberOfFemaleAdults++;
			NiagaraDataFrag.FemaleAdultAgentLocationAndScales.Add(LocationScale);
			NiagaraDataFrag.FemaleAdultAgentRotations.Add(RotationQuat);
			NiagaraDataFrag.FemaleAdultAnimationStates.Add(AnimationState);
			break;
		case EAgeDemographic::Ead_Default:
			break;
		}
	}
}

void UAgentRepresentation_MOP::ResetDataInNiagaraSystem(FNiagaraStatsFragment& NiagaraStatsFrag,
                                                        FAgentNiagaraDataFrag& NiagaraFrag)
{
	NiagaraStatsFrag.NumberOfFemaleAdults = 0;
	NiagaraStatsFrag.NumberOfFemaleElderly = 0;
	NiagaraStatsFrag.NumberOfMaleAdults = 0;
	NiagaraStatsFrag.NumberOfMaleElderly = 0;
	NiagaraStatsFrag.NumberOfChildren = 0;
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

	if (NiagaraStatsFrag.NiagaraRepresentationActor.Get() && NiagaraStatsFrag.NiagaraRepresentationActor->GetNiagaraComponent())
	{
		NiagaraStatsFrag.NiagaraRepresentationActor->GetNiagaraComponent()->Deactivate();
		
	}
}

ANiagaraAgentRepActor* UAgentRepresentation_MOP::GetOrCreateNiagaraRepActor(UWorld* World)
{
	auto* ExistingActor = Cast<ANiagaraAgentRepActor>(UGameplayStatics::GetActorOfClass(World, ANiagaraAgentRepActor::StaticClass()));
	if (ExistingActor)
	{
		ExistingActor->GetNiagaraComponent()->DeactivateImmediate();
		ExistingActor->GetNiagaraComponent()->DestroyInstanceNotComponent();
		return ExistingActor;
	}

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
