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

#include "MassAI/MassProcessor/Representation/NiagaraAgentRepProcessor.h"
// Required headers for processing entities and there fragments
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassExternalSubsystemTraits.h" // This is needed so we can use subsystems and have no compile errors
// Fragments to include with this processor
#include "MassAI/Fragments/EntityInfoFragment.h"
// Shared Fragments to include with the processor
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentRepresenatationFragment.h"
// Tags
#include "MassAI/Tags/MassAITags.h"
// Subsystems to include with the processor
#include "NiagaraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/Actors/NiagaraAgentRepActor.h"
#include "SubSystems/TimeDilationSubSystem.h"
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
// Niagara
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentNiagaraDataFrag.h"



UNiagaraAgentRepProcessor::UNiagaraAgentRepProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);

	bRequiresGameThreadExecution = true;
}

void UNiagaraAgentRepProcessor::ConfigureQueries()
{
	// The Entity Query Required fragments for this processor;
	EntityQuery.AddRequirement<FEntityMovementFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FEntityRenderingFragment>(EMassFragmentAccess::ReadWrite);

	// Add the shared Niagara representation fragment
	EntityQuery.AddSharedRequirement<FAgentNiagaraDataFrag>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	EntityQuery.AddSharedRequirement<FNiagaraStatsFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);

	/* Add subsystem requirements */
	// Representation subsystem
	EntityQuery.AddSubsystemRequirement<UMRS_RepresentationSubsystem>(EMassFragmentAccess::ReadOnly);

	// Required Query Tags
	EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::None);

	EntityQuery.AddTagRequirement<FMassEntityRepresentationTag>(EMassFragmentPresence::All); // If all entities have tag, do process

	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);

	// Register requirements for the processor
	ProcessorRequirements.AddSubsystemRequirement<UMRS_RepresentationSubsystem>(EMassFragmentAccess::ReadOnly);

	// Time Dilation Subsystem
	ProcessorRequirements.AddSubsystemRequirement<UTimeDilationSubSystem>(EMassFragmentAccess::ReadOnly);
	
}

void UNiagaraAgentRepProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context)
	{
		//TODO: need to look at mass ai signals and how to use them -> this should be the equivalent to delegates and events -> and the reloading the shared fragments should only occur then
		// Get the Niagara agent representation frag for the system
		AgentNiagaraRepSharedFrag = Context.GetMutableSharedFragment<FAgentNiagaraDataFrag>();
		// We should only assign the properties once
		if (!bRegisteredProperties)
		{
			RegisterProperties(Context);
		}
		//TODO: this check does not work at all, it never returns true despite data changes!!
		// Check that the array sizes are correct -> this happens when agent data changes
		else if (!CheckAgentCountArraySize(Context.GetMutableSharedFragment<FNiagaraStatsFragment>()))
		{
			// reset the registered properties bool
			bRegisteredProperties = false;
			
			// attempt registering the properties again
			RegisterProperties(Context);
		}
		else
		{
			// Check we using correct Niagara System based on the current scalability setting
			CheckAndUpdateNiagaraRenderSpec(Context);
			
			// We only want to update the pause state if it has changed
			if (bLastPauseLoop != TimeDilationSubSystem->bIsPaused)
			{
				bLastPauseLoop = TimeDilationSubSystem->bIsPaused;
				PauseResumeAnimations(bLastPauseLoop);
			}
			ExtractAgentData(Context);
		}
		
	}));
	UNiagaraComponent* NiagaraComp = NiagaraAgentRepActor ? NiagaraAgentRepActor->GetNiagaraComponent() : nullptr;

	SetNiagaraAgentData(NiagaraComp, TEXT("MaleAdultAgent"), MaleAdultAgentLocationAndScales, MaleAdultAgentRotations, MaleAnimationStates);
	SetNiagaraAgentData(NiagaraComp, TEXT("ElderlyMaleAgent"), ElderlyMaleAdultAgentLocationAndScales, ElderlyMaleAdultAgentRotations, ElderlyMaleAnimationStates);
	SetNiagaraAgentData(NiagaraComp, TEXT("FemaleAdultAgent"), FemaleAdultAgentLocationAndScales, FemaleAdultAgentRotations, FemaleAnimationStates);
	SetNiagaraAgentData(NiagaraComp, TEXT("ElderlyFemaleAgent"), ElderlyFemaleAdultAgentLocationAndScales, ElderlyFemaleAdultAgentRotations, ElderlyFemaleAnimationStates);
	SetNiagaraAgentData(NiagaraComp, TEXT("ChildAgent"), ChildrenAgentLocationAndScales, ChildrenAgentRotations, ChildrenAnimationStates);
	
}

void UNiagaraAgentRepProcessor::ExtractAgentData(FMassExecutionContext& Context)
{
	// Get the entity Rendering fragment
	const TArrayView<FEntityRenderingFragment>& EntityRenderingFragment = Context.GetMutableFragmentView<FEntityRenderingFragment>();
	TConstArrayView<FEntityMovementFragment> EntityMovementFragment = Context.GetFragmentView<FEntityMovementFragment>();

	auto Entities = Context.GetEntities();
		
	for (int i = 0; i < Entities.Num(); i++)
	{
		auto EntityMovement = EntityMovementFragment[i];
		auto& EntityRendering = EntityRenderingFragment[i];

		// Get the entity instance index
		int32 EntityInstanceID = EntityRendering.InstanceID;

		// check if the entity is a child
		if (EntityRendering.AgeDemographic == EAgeDemographic::Ead_Child)
		{
			SetAgentData(EntityInstanceID, EntityMovement, EntityRendering, ChildrenAgentLocationAndScales, ChildrenAgentRotations, ChildrenAnimationStates);
		}
		// check if elderly
		else if (EntityRendering.AgeDemographic == EAgeDemographic::Ead_Elderly)
		{
			if (EntityRendering.bIsMale)
			{
				SetAgentData(EntityInstanceID, EntityMovement, EntityRendering, ElderlyMaleAdultAgentLocationAndScales, ElderlyMaleAdultAgentRotations, ElderlyMaleAnimationStates);
			}
			else
			{
				SetAgentData(EntityInstanceID, EntityMovement, EntityRendering, ElderlyFemaleAdultAgentLocationAndScales, ElderlyFemaleAdultAgentRotations, ElderlyFemaleAnimationStates);
			}
			
		}
		else // entity is an adult
		{
			if (EntityRendering.bIsMale)
			{
				SetAgentData(EntityInstanceID, EntityMovement, EntityRendering, MaleAdultAgentLocationAndScales, MaleAdultAgentRotations, MaleAnimationStates);
			}
			else
			{
				SetAgentData(EntityInstanceID, EntityMovement, EntityRendering, FemaleAdultAgentLocationAndScales, FemaleAdultAgentRotations, FemaleAnimationStates);
			}
		}
	}
	
}

void UNiagaraAgentRepProcessor::SetAgentData(int32 Index, const FEntityMovementFragment EntityMovementFragment, FEntityRenderingFragment& EntityRenderingFragment, TArray<FVector4>& LocationAndScales, TArray<FQuat>& Rotations, TArray<int32>& AnimationStates)
{
	LocationAndScales[Index] = FVector4(EntityMovementFragment.CurrentLocation.X,EntityMovementFragment.CurrentLocation.Y,EntityMovementFragment.CurrentLocation.Z, EntityRenderingFragment.bRenderAgent ? 1.0f : 0.0f);
	Rotations[Index] = EntityMovementFragment.CurrentRotation.Quaternion();
	AnimationStates[Index] = GetIntAnimState(EntityMovementFragment.CurrentMovementBracket);

	// update entity destroy state
	EntityRenderingFragment.bReadyToDestroy = !EntityRenderingFragment.bRenderAgent;
}

int32 UNiagaraAgentRepProcessor::GetIntAnimState(EPedestrianMovementBracket AnimState)
{
	int32 AnimationStateInt = 5; // default to error state
	switch (AnimState)
	{
	case EPedestrianMovementBracket::Emb_NotMoving:
		AnimationStateInt = 0;
		break;
	case EPedestrianMovementBracket::Emb_Shuffle:
		AnimationStateInt = 1;
		break;
	case EPedestrianMovementBracket::Emb_SlowWalk:
		AnimationStateInt = 2;
		break;
	case EPedestrianMovementBracket::Emb_Walk:
		AnimationStateInt = 3;
		break;
	case EPedestrianMovementBracket::Emb_BriskWalk:
		AnimationStateInt = 4;
		break;
	case EPedestrianMovementBracket::Emb_Error:
		AnimationStateInt = 5;
		break;
	}
	return AnimationStateInt;
}

void UNiagaraAgentRepProcessor::RegisterProperties(FMassExecutionContext& Context)
{
	// Get the Niagara agent representation frag for the system
	AgentNiagaraRepSharedFrag = Context.GetMutableSharedFragment<FAgentNiagaraDataFrag>();

	// Get the male agent locations and scales
	MaleAdultAgentLocationAndScales = AgentNiagaraRepSharedFrag.MaleAdultAgentLocationAndScales;

	// Get the male agent rotations
	MaleAdultAgentRotations = AgentNiagaraRepSharedFrag.MaleAdultAgentRotations;

	// Get the male animation states
	MaleAnimationStates = AgentNiagaraRepSharedFrag.MaleAdultAnimationStates;

	// Get the elderly male locations and scales
	ElderlyMaleAdultAgentLocationAndScales = AgentNiagaraRepSharedFrag.ElderlyMaleAdultAgentLocationAndScales;

	// Get the elderly male rotations
	ElderlyMaleAdultAgentRotations = AgentNiagaraRepSharedFrag.ElderlyMaleAdultAgentRotations;

	// Get the elderly male animation states
	ElderlyMaleAnimationStates = AgentNiagaraRepSharedFrag.ElderlyMaleAdultAnimationStates;

	// Get the female locations and scales
	FemaleAdultAgentLocationAndScales = AgentNiagaraRepSharedFrag.FemaleAdultAgentLocationAndScales;

	// Get the female rotations
	FemaleAdultAgentRotations = AgentNiagaraRepSharedFrag.FemaleAdultAgentRotations;

	// Get the female animation states
	FemaleAnimationStates = AgentNiagaraRepSharedFrag.FemaleAdultAnimationStates;

	// Get the elderly female locations and scales
	ElderlyFemaleAdultAgentLocationAndScales = AgentNiagaraRepSharedFrag.ElderlyFemaleAdultAgentLocationAndScales;
	
	// Get the elderly female rotations
	ElderlyFemaleAdultAgentRotations = AgentNiagaraRepSharedFrag.ElderlyFemaleAdultAgentRotations;

	// Get the elderly female animation states
	ElderlyFemaleAnimationStates = AgentNiagaraRepSharedFrag.ElderlyFemaleAdultAnimationStates;

	// Get the children locations and scales
	ChildrenAgentLocationAndScales = AgentNiagaraRepSharedFrag.ChildrenAgentLocationAndScales;

	// Get the children rotations
	ChildrenAgentRotations = AgentNiagaraRepSharedFrag.ChildrenAgentRotations;

	// Get the children animation states
	ChildrenAnimationStates = AgentNiagaraRepSharedFrag.ChildrenAnimationStates;

	// Get the agent representation actor //TODO: this works but it could be better
	NiagaraAgentRepActor = Cast<ANiagaraAgentRepActor>(UGameplayStatics::GetActorOfClass(GetWorld(), ANiagaraAgentRepActor::StaticClass()));

	// Get the Time Dilation Subsystem
	TimeDilationSubSystem = Context.GetWorld()->GetSubsystem<UTimeDilationSubSystem>();

	// Get the representation subsystem
	RepresentationSubsystem = Context.GetWorld()->GetSubsystem<UMRS_RepresentationSubsystem>();

	// Map the agent count to the array
	MapAgentCountToArray(Context.GetMutableSharedFragment<FNiagaraStatsFragment>());

	// check we got the subsystems -> if not then we need to return and not update the register flag
	if (TimeDilationSubSystem == nullptr || RepresentationSubsystem == nullptr ||
		NiagaraAgentRepActor == nullptr)
	{
		return;
	}

	bRegisteredProperties = true;
}

void UNiagaraAgentRepProcessor::PauseResumeAnimations(bool bPause) const
{
	// null ptr check can't set values if the actor is not valid
	if (NiagaraAgentRepActor == nullptr)
	{
		return;
	}
	
	// Set the pause state in the Niagara component
	NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableFloat(TEXT("PauseResumeAnimations"), bPause ? 0.0f : 1.0f);
}

void UNiagaraAgentRepProcessor::MapAgentCountToArray(const FNiagaraStatsFragment& AgentStatsFragment)
{
	NumberOfAgentsArray.Reset(5);

	NumberOfAgentsArray.Add(AgentStatsFragment.NumberOfMaleAdults);
	NumberOfAgentsArray.Add(AgentStatsFragment.NumberOfFemaleAdults);
	NumberOfAgentsArray.Add(AgentStatsFragment.NumberOfMaleElderly);
	NumberOfAgentsArray.Add(AgentStatsFragment.NumberOfFemaleElderly);
	NumberOfAgentsArray.Add(AgentStatsFragment.NumberOfChildren);
}

bool UNiagaraAgentRepProcessor::CheckAgentCountArraySize(const FNiagaraStatsFragment& AgentStatsFragment) const
{
	bool bMaleAdultsCorrect = CheckAgentArraySize(0, AgentStatsFragment.NumberOfMaleAdults);
	bool bFemaleAdultsCorrect = CheckAgentArraySize(1, AgentStatsFragment.NumberOfFemaleAdults);
	bool bMaleElderlyCorrect = CheckAgentArraySize(2, AgentStatsFragment.NumberOfMaleElderly);
	bool bFemaleElderlyCorrect = CheckAgentArraySize(3, AgentStatsFragment.NumberOfFemaleElderly);
	bool bChildrenCorrect = CheckAgentArraySize(4, AgentStatsFragment.NumberOfChildren);

	// if any are false then we need to return false
	if (!bMaleAdultsCorrect || !bFemaleAdultsCorrect || !bMaleElderlyCorrect || !bFemaleElderlyCorrect || !bChildrenCorrect)
	{
		// if any of the checks fail then we need to return false
		return false;
	}
	
	return true;
}

bool UNiagaraAgentRepProcessor::CheckAgentArraySize(int32 Index, int32 ArraySize) const
{
	if (NumberOfAgentsArray.IsValidIndex(Index))
	{
		return NumberOfAgentsArray[Index] == ArraySize;
	}
	
	// if the index is not valid then we need to return false
	return false;
}

void UNiagaraAgentRepProcessor::SetNiagaraAgentData(UNiagaraComponent* NiagaraComp, const FString& BaseName, const TArray<FVector4>& Locations, const TArray<FQuat>& Rotations, const TArray<int32>& AnimationStates)
{
	if (!NiagaraComp || Locations.Num() == 0)
	{
		return;
	}
    FName Location = *(BaseName + TEXT("LocationAndScale"));
	FName Rotation = *(BaseName + TEXT("QuatRotations"));
	FName AnimationState = *(BaseName + TEXT("AnimationStates"));
	
	
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(NiagaraComp, Location, Locations);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayQuat(NiagaraComp, Rotation, Rotations);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(NiagaraComp, AnimationState, AnimationStates);
}

void UNiagaraAgentRepProcessor::CheckAndUpdateNiagaraRenderSpec(FMassExecutionContext& Context)
{
	AgentNiagaraRepSharedFrag = Context.GetMutableSharedFragment<FAgentNiagaraDataFrag>();
	auto& AgentNiagaraStatsSharedFrag = Context.GetMutableSharedFragment<FNiagaraStatsFragment>();
	
	// if RepresentationSubsystem is null then we need to return
	if (RepresentationSubsystem == nullptr)
	{
		return;
	}

	// Check if the Niagara representation actor is valid
	if (NiagaraAgentRepActor == nullptr || NiagaraAgentRepActor->GetNiagaraComponent() == nullptr)
	{
		return;
	}
	// Check if the Niagara representation actor has the correct render spec set
	if (AgentNiagaraStatsSharedFrag.bUseLowSpecAgentRenderEffect == RepresentationSubsystem->IsCurrentPedestrianAvatarTypeLowSpec())
	{
		return; // if the render spec is the same then we don't need to update
	}

	// if we reach here then we need to update the render spec
	AgentNiagaraStatsSharedFrag.bUseLowSpecAgentRenderEffect = RepresentationSubsystem->IsCurrentPedestrianAvatarTypeLowSpec();

	// deactivate and destroy instance
	AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent()->DeactivateImmediate();
	AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent()->DestroyInstanceNotComponent();
	

	// Create the new system
	UNiagaraSystem* NiagaraSystem = RepresentationSubsystem->LoadNiagaraAgentSystem(AgentNiagaraStatsSharedFrag.bUseLowSpecAgentRenderEffect);

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
			

	// once created we need to pass in the shared niagara data before we activate it

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

	//

	SetNiagaraAgentData(AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent(), TEXT("MaleAdultAgent"), MaleAdultAgentLocationAndScales, MaleAdultAgentRotations, MaleAnimationStates);
	SetNiagaraAgentData(AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent(), TEXT("ElderlyMaleAgent"), ElderlyMaleAdultAgentLocationAndScales, ElderlyMaleAdultAgentRotations, ElderlyMaleAnimationStates);
	SetNiagaraAgentData(AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent(), TEXT("FemaleAdultAgent"), FemaleAdultAgentLocationAndScales, FemaleAdultAgentRotations, FemaleAnimationStates);
	SetNiagaraAgentData(AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent(), TEXT("ElderlyFemaleAgent"), ElderlyFemaleAdultAgentLocationAndScales, ElderlyFemaleAdultAgentRotations, ElderlyFemaleAnimationStates);
	SetNiagaraAgentData(AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent(), TEXT("ChildAgent"), ChildrenAgentLocationAndScales, ChildrenAgentRotations, ChildrenAnimationStates);
}
