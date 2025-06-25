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
	ObservedType = FMassEntityRepresentationTag::StaticStruct();
	Operation = EMassObservedOperation::Add;
	bRequiresGameThreadExecution = true;

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
	
	//EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, [this, &AgentRepresentationInstanceComp](FMassExecutionContext& Context)
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, [this](FMassExecutionContext& Context)
	{
		//UE_LOG(LogTemp, Warning, TEXT("UAgentRepresentation_MOP::Execute"));
		// Get the agent representation fragment
		FAgentRepresentationFragment& AgentRepresentationFragment = Context.GetMutableSharedFragment<FAgentRepresentationFragment>();

               // Get the entity movement and rendering fragments
		TConstArrayView<FEntityMovementFragment> EntityMovementFragment = Context.GetFragmentView<FEntityMovementFragment>();
		const TArrayView<FEntityRenderingFragment>& EntityRenderingFragment = Context.GetMutableFragmentView<FEntityRenderingFragment>();

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
				ProcessEntity(EntityMovement,EntityRendering, AgentRepresentationFragment, AgentNiagaraRepSharedFrag);
				
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
			
			// Create the Niagara System
			//UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(StaticLoadObject(UNiagaraSystem::StaticClass(), NULL, TEXT("NiagaraSystem'/Game/01_Dev/PedestrianMovement/NiagaraConversion/NS_InstancedPedestrianAgent.NS_InstancedPedestrianAgent'")));
			UNiagaraSystem* NiagaraSystem = MRSSubsystem->LoadNiagaraAgentSystem(AgentRepresentationFragment.bUseLowSpecAgentRenderEffect);

			if (NiagaraSystem == nullptr)
			{
				// Log error if the Niagara System could not be loaded
				UE_LOG(LogTemp, Error, TEXT("Failed to load Niagara System for Agent Representation"));
				return;
			}

			// Set the Niagara System
			NiagaraAgentRepActor->GetNiagaraComponent()->SetAsset(NiagaraSystem);
			
			// Set the shared actor component in the shared fragment
			AgentRepresentationFragment.NiagaraAgentRepActor = NiagaraAgentRepActor;
			
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
				ProcessEntity(EntityMovement,EntityRendering, AgentRepresentationFragment, AgentNiagaraRepSharedFrag);
				
				EntityIndexOffset++;
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

// Consume movement and rendering fragments to fill Niagara arrays and update counts.
void UAgentRepresentation_MOP::ProcessEntity(const FEntityMovementFragment& EntityMovementFrag, FEntityRenderingFragment& EntityRenderingFrag,
                                             FAgentRepresentationFragment& AgentFrag, FAgentNiagaraRepSharedFrag& NiagaraFrag)
{
	FVector4 LocationScale = FVector4(EntityMovementFrag.CurrentLocation, 1.0f);
	FQuat RotationQuat = EntityMovementFrag.CurrentRotation.Quaternion();
	int32 AnimationState = 0;

	if (EntityRenderingFrag.bIsMale)
	{
		switch (EntityRenderingFrag.AgeDemographic)
		{
		case EAgeDemographic::Ead_Child:
			EntityRenderingFrag.InstanceID = AgentFrag.NumberOfChildren++; // ++ on right side of assignment means post increment
			NiagaraFrag.ChildrenAgentLocationAndScales.Add(LocationScale);
			NiagaraFrag.ChildrenAgentRotations.Add(RotationQuat);
			NiagaraFrag.ChildrenAnimationStates.Add(AnimationState);
			break;

		case EAgeDemographic::Ead_Elderly:
	
			EntityRenderingFrag.InstanceID = AgentFrag.NumberOfMaleElderly++;
			NiagaraFrag.ElderlyMaleAdultAgentLocationAndScales.Add(LocationScale);
			NiagaraFrag.ElderlyMaleAdultAgentRotations.Add(RotationQuat);
			NiagaraFrag.ElderlyMaleAdultAnimationStates.Add(AnimationState);
			break;
		case EAgeDemographic::Ead_Adult:
			EntityRenderingFrag.InstanceID = AgentFrag.NumberOfMaleAdults++;
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
		switch (EntityRenderingFrag.AgeDemographic)
		{
		case EAgeDemographic::Ead_Child: // TODO: no female children yet
			EntityRenderingFrag.InstanceID = AgentFrag.NumberOfChildren++; // ++ on right side of assignment means post increment
			NiagaraFrag.ChildrenAgentLocationAndScales.Add(LocationScale);
			NiagaraFrag.ChildrenAgentRotations.Add(RotationQuat);
			NiagaraFrag.ChildrenAnimationStates.Add(AnimationState);
			break;

		case EAgeDemographic::Ead_Elderly:
			EntityRenderingFrag.InstanceID = AgentFrag.NumberOfFemaleElderly++;
			NiagaraFrag.ElderlyFemaleAdultAgentLocationAndScales.Add(LocationScale);
			NiagaraFrag.ElderlyFemaleAdultAgentRotations.Add(RotationQuat);
			NiagaraFrag.ElderlyFemaleAdultAnimationStates.Add(AnimationState);
			break;
		case EAgeDemographic::Ead_Adult:
			EntityRenderingFrag.InstanceID = AgentFrag.NumberOfFemaleAdults++;
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
