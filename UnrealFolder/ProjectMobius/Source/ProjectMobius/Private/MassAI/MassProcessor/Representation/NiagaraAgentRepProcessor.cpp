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
#include "MassAI/Fragments/AgentEgressTenabilityFragments.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/MassProcessor/Analytics/AgentEgressHealthCalculationProcessor.h"
// Shared Fragments to include with the processor
#include "MassAI/Fragments/SharedFragments/RepresentationFragments/AgentRepresentationFragment.h"
// Tags
#include "MassAI/Tags/MassAITags.h"
// Subsystems to include with the processor
#include "NiagaraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/Actors/NiagaraAgentRepActor.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "SubSystems/TimeDilationSubSystem.h"
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
// Niagara
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "MassAI/Fragments/SharedFragments/RepresentationFragments/AgentNiagaraDataFrag.h"
#include "Subsystems/StatisticSubsystem.h"
#include "Async/ParallelFor.h"
#include "HAL/IConsoleManager.h"

// The header keeps a plain literal to preserve its forward declarations — hold the two in sync here
static_assert(UNiagaraAgentRepProcessor::DemographicSlotCount == MobiusNiagaraDemographics::NumSlots,
	"DemographicSlotCount must match MobiusNiagaraDemographics::NumSlots");

// Gates the per-demographic Niagara array uploads on content changes; 0 = legacy upload-every-frame.
// Default stays 0 until the scripted camera+scrub A/B verifies identical rendered output (PRD B8).
static TAutoConsoleVariable<int32> CVarMobiusChangedOnlyUpload(
	TEXT("mobius.Render.ChangedOnlyUpload"),
	0,
	TEXT("1 = re-upload a demographic's Niagara agent arrays only when their content changed since its last upload (paused/idle frames skip all 15 SetNiagaraArray marshals). 0 = upload every frame (legacy)."),
	ECVF_Default);


UNiagaraAgentRepProcessor::UNiagaraAgentRepProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);
	ExecutionOrder.ExecuteAfter.Add(UAgentEgressHealthCalculationProcessor::StaticClass()->GetFName());

	bRequiresGameThreadExecution = true;
}

void UNiagaraAgentRepProcessor::ConfigureQueries()
{
	// The Entity Query Required fragments for this processor;
	EntityQuery.AddRequirement<FEntityMovementFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FEntityRenderingFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FAgentEgressTenabilityFragment>(EMassFragmentAccess::ReadOnly);

	// Add the shared Niagara representation fragment
	EntityQuery.AddSharedRequirement<FAgentNiagaraDataFrag>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	EntityQuery.AddSharedRequirement<FNiagaraStatsFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);

	/* Add subsystem requirements */
	// Representation subsystem
	EntityQuery.AddSubsystemRequirement<UMRS_RepresentationSubsystem>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UStatisticSubsystem>(EMassFragmentAccess::ReadWrite);

	// Required Query Tags
	EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::None);
	EntityQuery.AddTagRequirement<FDisplayEntityDetailsTag>(EMassFragmentPresence::Optional);
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
	//TODO: Make flag for this so no constant gets etc

	if (!IsValid(NiagaraAgentRepActor))
	{

		// Get the agent representation actor //TODO: this works but it could be better
		NiagaraAgentRepActor = Cast<ANiagaraAgentRepActor>(UGameplayStatics::GetActorOfClass(GetWorld(), ANiagaraAgentRepActor::StaticClass()));
	}

	if (TimeDilationSubSystem == nullptr || RepresentationSubsystem == nullptr)
	{
		// Get the Time Dilation Subsystem
		TimeDilationSubSystem = ExecutionContext.GetWorld()->GetSubsystem<UTimeDilationSubSystem>();

		// Get the representation subsystem
		RepresentationSubsystem = ExecutionContext.GetWorld()->GetSubsystem<UMRS_RepresentationSubsystem>();
	}

	// check we got the subsystems -> if not then we need to return
	if (TimeDilationSubSystem == nullptr || RepresentationSubsystem == nullptr ||
		NiagaraAgentRepActor == nullptr)
	{
		return;
	}

	//EntityQuery.ParallelForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context)
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

	// Changed-only upload (mobius.Render.ChangedOnlyUpload): skip a demographic's three array
	// re-marshals while nothing in it changed since its last upload. Off (0) = legacy every-frame path.
	const bool bChangedOnlyUpload = CVarMobiusChangedOnlyUpload.GetValueOnGameThread() != 0;
	auto UploadAgentData = [&](const int32 Slot, const TCHAR* BaseName,
		const TArray<FVector4>& Locations, const TArray<FQuat>& Rotations, const TArray<int32>& AnimationStates)
	{
		if (bChangedOnlyUpload && !DemographicDirty[Slot].load(std::memory_order_relaxed))
		{
			return;
		}
		SetNiagaraAgentData(NiagaraComp, BaseName, Locations, Rotations, AnimationStates);
		// Mirror SetNiagaraAgentData's early-out: only mark clean when the upload actually ran
		if (NiagaraComp && Locations.Num() > 0)
		{
			DemographicDirty[Slot].store(false, std::memory_order_relaxed);
		}
	};

	// Slot indices follow MobiusNiagaraDemographics::ComputeSlot; call order preserved from the legacy path
	UploadAgentData(0, TEXT("MaleAdultAgent"), MaleAdultAgentLocationAndScales, MaleAdultAgentRotations, MaleAnimationStates);
	UploadAgentData(2, TEXT("ElderlyMaleAgent"), ElderlyMaleAdultAgentLocationAndScales, ElderlyMaleAdultAgentRotations, ElderlyMaleAnimationStates);
	UploadAgentData(1, TEXT("FemaleAdultAgent"), FemaleAdultAgentLocationAndScales, FemaleAdultAgentRotations, FemaleAnimationStates);
	UploadAgentData(3, TEXT("ElderlyFemaleAgent"), ElderlyFemaleAdultAgentLocationAndScales, ElderlyFemaleAdultAgentRotations, ElderlyFemaleAnimationStates);
	UploadAgentData(4, TEXT("ChildAgent"), ChildrenAgentLocationAndScales, ChildrenAgentRotations, ChildrenAnimationStates);

}

void UNiagaraAgentRepProcessor::ExtractAgentData(FMassExecutionContext& Context)
{
	// Get the entity Rendering fragment
	const TArrayView<FEntityRenderingFragment>& EntityRenderingFragment = Context.GetMutableFragmentView<FEntityRenderingFragment>();
	TConstArrayView<FEntityMovementFragment> EntityMovementFragment = Context.GetFragmentView<FEntityMovementFragment>();
	const TConstArrayView<FAgentEgressTenabilityFragment> AgentHealthFragments =
		Context.GetFragmentView<FAgentEgressTenabilityFragment>();

	auto Entities = Context.GetEntities();

	// Slot-indexed dispatch tables replace the legacy per-agent gender/age branch; the slot was
	// routed once at spawn (AgentRepresentation_MOP). Table order = MobiusNiagaraDemographics::ComputeSlot.
	TArray<FVector4>* const LocationsBySlot[MobiusNiagaraDemographics::NumSlots] = {
		&MaleAdultAgentLocationAndScales, &FemaleAdultAgentLocationAndScales,
		&ElderlyMaleAdultAgentLocationAndScales, &ElderlyFemaleAdultAgentLocationAndScales,
		&ChildrenAgentLocationAndScales };
	TArray<FQuat>* const RotationsBySlot[MobiusNiagaraDemographics::NumSlots] = {
		&MaleAdultAgentRotations, &FemaleAdultAgentRotations,
		&ElderlyMaleAdultAgentRotations, &ElderlyFemaleAdultAgentRotations,
		&ChildrenAgentRotations };
	TArray<int32>* const AnimationStatesBySlot[MobiusNiagaraDemographics::NumSlots] = {
		&MaleAnimationStates, &FemaleAnimationStates,
		&ElderlyMaleAnimationStates, &ElderlyFemaleAnimationStates,
		&ChildrenAnimationStates };

	// Safe to run parallel: every routed entity writes only its own (slot, InstanceID) array element
	// (InstanceID is unique within a demographic and every routed agent has a distinct one) plus its own
	// rendering-fragment element — all write targets disjoint. Unrouted agents (Slot >= NumSlots, i.e.
	// InvalidSlot) are skipped below, so they can't alias index 0. The Niagara SetNiagaraArray* uploads
	// stay on the game thread after the loop (see Execute).
	ParallelFor(Entities.Num(), [&](const int32 i)
	{
		FEntityRenderingFragment& EntityRendering = EntityRenderingFragment[i];

		const uint8 Slot = EntityRendering.NiagaraDemographicSlot;
		if (Slot >= MobiusNiagaraDemographics::NumSlots)
		{
			// Agent was never routed into a demographic array at spawn (only Ead_Default today,
			// currently unreachable) — nothing to write. Guards the dispatch index too.
			return;
		}

		const FEntityMovementFragment& EntityMovement = EntityMovementFragment[i];
		const bool bIsDead = AgentHealthFragments[i].bIsDead;

		// Get the entity instance index
		const int32 EntityInstanceID = EntityRendering.InstanceID;

		if (SetAgentData(EntityInstanceID, EntityMovement, EntityRendering, bIsDead,
			*LocationsBySlot[Slot], *RotationsBySlot[Slot], *AnimationStatesBySlot[Slot]))
		{
			// relaxed is enough: racing writers all store true; read/clear happens game-thread-only
			DemographicDirty[Slot].store(true, std::memory_order_relaxed);
		}
	});
}

bool UNiagaraAgentRepProcessor::SetAgentData(
	const int32 Index,
	const FEntityMovementFragment& EntityMovementFragment,
	FEntityRenderingFragment& EntityRenderingFragment,
	const bool bIsDead,
	TArray<FVector4>& LocationAndScales,
	TArray<FQuat>& Rotations,
	TArray<int32>& AnimationStates)
{
	const FVector4 NewLocationAndScale = FVector4(EntityMovementFragment.CurrentLocation.X,EntityMovementFragment.CurrentLocation.Y,EntityMovementFragment.CurrentLocation.Z, EntityRenderingFragment.bRenderAgent ? 1.0f : 0.0f);
	const FQuat NewRotation = EntityMovementFragment.CurrentRotation.Quaternion();
	// TODO: Replace the stopped death animation with hiding the mesh and placing a death marker at the agent location.
	const int32 NewAnimationState = bIsDead
		? GetIntAnimState(EPedestrianMovementBracket::Emb_NotMoving)
		: GetIntAnimState(EntityMovementFragment.CurrentMovementBracket);

	// Exact (bitwise-value) compare before write: an unchanged agent must not dirty its
	// demographic's upload — this is what makes paused/idle frames upload nothing
	const bool bChanged = LocationAndScales[Index] != NewLocationAndScale
		|| Rotations[Index] != NewRotation
		|| AnimationStates[Index] != NewAnimationState;

	LocationAndScales[Index] = NewLocationAndScale;
	Rotations[Index] = NewRotation;
	AnimationStates[Index] = NewAnimationState;

	// update entity destroy state
	EntityRenderingFragment.bReadyToDestroy = !EntityRenderingFragment.bRenderAgent;

	return bChanged;
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

	// Map the agent count to the array
	MapAgentCountToArray(Context.GetMutableSharedFragment<FNiagaraStatsFragment>());

	// Freshly (re)copied arrays must upload regardless of past content compares
	for (std::atomic<bool>& Dirty : DemographicDirty)
	{
		Dirty.store(true, std::memory_order_relaxed);
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

//TODO: FIX THIS - when going low spec to high the animations remain paused and only resume when we pause and unpause
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
	if (!IsValid(AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor.Get()) ||
		AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent() == nullptr)
	{
		return;
	}
	AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent()->DeactivateImmediate();
	AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent()->DestroyInstanceNotComponent();


	// Create the new system
	UNiagaraSystem* NiagaraSystem = RepresentationSubsystem->LoadNiagaraAgentSystem(AgentNiagaraStatsSharedFrag.bUseLowSpecAgentRenderEffect);

	if (NiagaraSystem == nullptr)
	{
		// Log error if the Niagara System could not be loaded
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Representation Error"),
				FText::FromString("Niagara system missing"),
				FText::FromString("Failed to load Niagara System for Agent Representation."),
				FText::FromString("NiagaraAgentRepProcessor"));
		}
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

	// If we are paused then we need to pause the animations and vice versa
	PauseResumeAnimations(bLastPauseLoop);

	// A recreated system instance must get the next regular upload even if agent content is unchanged
	for (std::atomic<bool>& Dirty : DemographicDirty)
	{
		Dirty.store(true, std::memory_order_relaxed);
	}

	// Activate the Niagara System
	AgentNiagaraStatsSharedFrag.NiagaraRepresentationActor->GetNiagaraComponent()->Activate(true);
}
