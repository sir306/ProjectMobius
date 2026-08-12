// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/MassProcessor/Representation/DisplayAgentUIStatsProcessor.h"

#include "MassExecutionContext.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Fragments/SharedFragments/RepresentationFragments/AgentRepresentationFragment.h"
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
#include "MassAI/Tags/MassAITags.h"
#include "Subsystems/MobiusControllerSubsystem.h"
#include "Subsystems/StatisticSubsystem.h"

// Lookup table for agent heights based on age demographic and gender
constexpr float AgentHeightTable[3][2] = {
	{ 131.0f, 131.0f },  // Child: Male/Female (gender ignored)
	{ 173.0f, 159.0f },  // Adult: Male/Female
	{ 170.0f, 158.0f }   // Elderly: Male/Female
};

namespace
{
	/** Seated total height of a wheelchair user, cm. The occupant's mesh renders seated, so their
	 *  standing height from AgentHeightTable is wrong here — and this value is not cosmetic: it feeds
	 *  FAgentMeshViewer::AgentHeight, which SAgentFollowIndicator/SPedestrianAgentHoverMeshWidget use
	 *  as the Z offset for the in-world hover card. Get it wrong and the card floats at standing
	 *  height above a seated agent. Placeholder until the seated VAT pose is baked and a real figure
	 *  can be measured off it. */
	constexpr float GWheelchairSeatedHeightCm = 130.0f;

	/**
	 * Single source of truth for an agent's DISPLAY demographic string and display height.
	 *
	 * This exists because the two callers below — the selected-agent path and the hovered-agent path —
	 * were verbatim copies of the same if/else chain. Editing one and not the other makes the stats
	 * panel and the hover tooltip disagree about the same agent, silently. Route both through here;
	 * do not re-inline it.
	 */
	void ResolveAgentDisplay(const FEntityRenderingFragment& Rendering,
		FString& OutDemographicText, float& OutHeightCm)
	{
		int32 AgeIndex;
		if (Rendering.AgeDemographic == EAgeDemographic::Ead_Child)
		{
			OutDemographicText = TEXT("Child");
			AgeIndex = 0;
		}
		else if (Rendering.AgeDemographic == EAgeDemographic::Ead_Elderly)
		{
			OutDemographicText = TEXT("Elderly");
			AgeIndex = 2;
		}
		else
		{
			OutDemographicText = TEXT("Adult");
			AgeIndex = 1;
		}

		const int32 GenderIndex = Rendering.bIsMale ? 0 : 1;
		OutHeightCm = AgentHeightTable[AgeIndex][GenderIndex];

		if (Rendering.MobilityAid == EMobilityAid::Ema_Wheelchair)
		{
			// The age is APPENDED to, never replaced: an agent labelled just "Adult" while a
			// wheelchair renders on screen reads as an able-bodied pedestrian, which is the whole
			// reason EMobilityAid is orthogonal to EAgeDemographic rather than a value inside it.
			OutDemographicText += TEXT(" (wheelchair)");
			OutHeightCm = GWheelchairSeatedHeightCm;
		}
	}
}

UDisplayAgentUIStatsProcessor::UDisplayAgentUIStatsProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);

	bRequiresGameThreadExecution = true;
}

void UDisplayAgentUIStatsProcessor::ConfigureQueries()
{
	// The Entity Query Required fragments for this processor;
	EntityQuery.AddRequirement<FEntityInfoFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FEntityMovementFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FEntityRenderingFragment>(EMassFragmentAccess::ReadOnly);

	/* Add subsystem requirements */

	// Required Query Tags
	EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::None);
	EntityQuery.AddTagRequirement<FDisplayEntityDetailsTag>(EMassFragmentPresence::Any); // If any entities have tag, do process
	EntityQuery.AddTagRequirement<FMassEntityRepresentationTag>(EMassFragmentPresence::All); // If all entities have tag, do process

	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);

	// Register requirements for the processor
	ProcessorRequirements.AddSubsystemRequirement<UMRS_RepresentationSubsystem>(EMassFragmentAccess::ReadOnly);
	ProcessorRequirements.AddSubsystemRequirement<UStatisticSubsystem>(EMassFragmentAccess::ReadWrite);
	ProcessorRequirements.AddSubsystemRequirement<UMobiusControllerSubsystem>(EMassFragmentAccess::ReadOnly);
	
}

void UDisplayAgentUIStatsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	auto StatisticSubsystem = ExecutionContext.GetWorld()->GetSubsystem<UStatisticSubsystem>();
	auto MobiusControllerSubsystem = ExecutionContext.GetWorld()->GetSubsystem<UMobiusControllerSubsystem>();
	AgentData.Empty();

	
	if (MobiusControllerSubsystem && MobiusControllerSubsystem->GetCapsuleComponent() != nullptr)
	{
		SelectedAgentData.AgentID = -2; // Set this to -2 to indicate agent completed sim
	}
	else
	{
		SelectedAgentData.AgentID = -1; // Set this to -1 to indicate no agent selected
	}
	
	HoveredAgentData = FAgentMeshViewer(); // Reset the hovered agent data
	
	EntityQuery.ParallelForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context)
	{
		// Get the entity info fragment
		const TConstArrayView<FEntityInfoFragment> EntityInfoFragments = Context.GetFragmentView<FEntityInfoFragment>();

		// Get the entity movement fragment
		const TConstArrayView<FEntityMovementFragment> EntityMovements = Context.GetFragmentView<FEntityMovementFragment>();

		// Get the entity rendering fragment
		const TConstArrayView<FEntityRenderingFragment> EntityRenderingFragments = Context.GetFragmentView<FEntityRenderingFragment>();

		for (int32 i = 0; i < EntityInfoFragments.Num(); ++i)
		{
			const FEntityInfoFragment& EntityInfo = EntityInfoFragments[i];
			const FEntityRenderingFragment& EntityRendering = EntityRenderingFragments[i];
			const FEntityMovementFragment& EntityMovement = EntityMovements[i];

			if (EntityRendering.showPedestrianStats == 1)
			{
				// Update the UI stats with the entity info
				UpdateUIStats(EntityInfo, EntityMovement, EntityRendering);
			}
			else if (EntityRendering.showPedestrianStats == 2)
			{
				// we only want to add the agent data if it is set to render
				if (EntityRendering.bRenderAgent)
				{
					// make text based on agent gender
					FString AgentGenderText = EntityRendering.bIsMale ? "Male" : "Female";
					// make text based on agent age demographic
					FString AgentAgeText;
					
					// As no heights are provided in the JSON data, defaults are used (see the table above).
					// we may want to scale the height in a range based on known pedestrian movement bands for demographics and max speed or introduce a field
					float AgentHeight = 0.0f;
					ResolveAgentDisplay(EntityRendering, AgentAgeText, AgentHeight);

					SelectedAgentData.AgentID = EntityRendering.EntityID;
					SelectedAgentData.AgentName = FText::FromString(EntityInfo.EntityName);
					SelectedAgentData.Demographic = FText::FromString(AgentAgeText);
					SelectedAgentData.Gender = FText::FromString(AgentGenderText);
					SelectedAgentData.AgentWorldPosition = FVector(EntityMovement.CurrentLocation.X, EntityMovement.CurrentLocation.Y, EntityMovement.CurrentLocation.Z);
					SelectedAgentData.AgentSpeed = EntityMovement.CurrentSpeed;
					SelectedAgentData.GaitDirectionalSpeed = EntityMovement.GaitDirectionalSpeed;// todo: no gait speed implemented yet
					SelectedAgentData.AgentHeight = AgentHeight;
					SelectedAgentData.SpeedFractionOfMax = EntityMovement.CurrentSpeed / EntityInfo.EntityMaxSpeed;
				}
			}
		}
	}));

	if (StatisticSubsystem)
	{
		if (AgentData.Num() > 0)
		{
			HoveredAgentData = AgentData[0]; // Set the first agent data as the selected agent data -> Currently hovering on a new agent
		}
		// Update mesh info data
		//StatisticSubsystem->UpdateAgentInfoMeshData(AgentData);
		StatisticSubsystem->UpdateHoveredAgentData(HoveredAgentData);
		StatisticSubsystem->UpdateSelectedAgentData(SelectedAgentData);
	}
	
}

void UDisplayAgentUIStatsProcessor::UpdateUIStats(const FEntityInfoFragment& EntityInfo,
                                                  const FEntityMovementFragment& EntityMovement, const FEntityRenderingFragment& EntityRendering)
{

	// we only want to add the agent data if it is set to render
	if (EntityRendering.bRenderAgent)
	{
		// make text based on agent gender
		FString AgentGenderText = EntityRendering.bIsMale ? "Male" : "Female";
		// make text based on agent age demographic
		FString AgentAgeText;
					
		// As no heights are provided in the JSON data, defaults are used (see the table above).
		// we may want to scale the height in a range based on known pedestrian movement bands for demographics and max speed or introduce a field
		float AgentHeight = 0.0f;
		ResolveAgentDisplay(EntityRendering, AgentAgeText, AgentHeight);

		//TODO: Moving away from the agent mesh viewer, we will need to update this to use the new system ->once we have collisions
		// make new agent data
		FAgentMeshViewer NewAgentData;
		NewAgentData.AgentID = EntityRendering.EntityID;
		NewAgentData.AgentName = FText::FromString(EntityInfo.EntityName);
		NewAgentData.Demographic = FText::FromString(AgentAgeText);
		NewAgentData.Gender = FText::FromString(AgentGenderText);
		NewAgentData.AgentWorldPosition = FVector(EntityMovement.CurrentLocation.X, EntityMovement.CurrentLocation.Y, EntityMovement.CurrentLocation.Z);
		NewAgentData.AgentSpeed = EntityMovement.CurrentSpeed;
		NewAgentData.GaitDirectionalSpeed = EntityMovement.GaitDirectionalSpeed;
		NewAgentData.AgentHeight = AgentHeight;
		NewAgentData.SpeedFractionOfMax = EntityMovement.CurrentSpeed / EntityInfo.EntityMaxSpeed;
		AgentData.Add(NewAgentData);
	}
}
