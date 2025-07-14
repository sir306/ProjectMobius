// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/MassProcessor/Representation/DisplayAgentUIStatsProcessor.h"

#include "MassExecutionContext.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentRepresenatationFragment.h"
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
#include "MassAI/Tags/MassAITags.h"
#include "Subsystems/StatisticSubsystem.h"


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
	
}

void UDisplayAgentUIStatsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	auto StatisticSubsystem = ExecutionContext.GetWorld()->GetSubsystem<UStatisticSubsystem>();
	AgentData.Empty();
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context)
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
		}
		
	}));

	if (StatisticSubsystem)
	{
		// Update mesh info data
		StatisticSubsystem->UpdateAgentInfoMeshData(AgentData);
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
		if (EntityRendering.AgeDemographic == EAgeDemographic::Ead_Child)
		{
			AgentAgeText = "Child";
		}
		else if (EntityRendering.AgeDemographic == EAgeDemographic::Ead_Elderly)
		{
			AgentAgeText = "Elderly";
		}
		else
		{
			AgentAgeText = "Adult";
		}
		//TODO: Moving away from the agent mesh viewer, we will need to update this to use the new system ->once we have collisions
		// make new agent data
		FAgentMeshViewer NewAgentData;
		NewAgentData.AgentID = EntityRendering.EntityID;
		NewAgentData.AgentName = FText::FromString(FString::Printf(TEXT("Agent %d"), EntityRendering.EntityID));//TODO:getNAME details
		NewAgentData.Demographic = FText::FromString(AgentAgeText);
		NewAgentData.Gender = FText::FromString(AgentGenderText);
		NewAgentData.AgentWorldPosition = FVector(EntityMovement.CurrentLocation.X, EntityMovement.CurrentLocation.Y, EntityMovement.CurrentLocation.Z);
		NewAgentData.AgentSpeed = EntityMovement.CurrentSpeed;
		NewAgentData.GaitDirectionalSpeed = EntityMovement.GaitDirectionalSpeed;
		NewAgentData.AgentHeight = 180.0f;//TODO:work out height later
		AgentData.Add(NewAgentData);

	}
}
