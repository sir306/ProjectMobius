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
					
					float AgentHeight = 0.0f; // As no heights are provided in the JSON data, we will use defaults below
					// we may want to scale the height in a range based on known pedestrian movement bands for demographics and max speed or introduce a field

					int32 AgeIndex = 1; // default to Adult
					if (EntityRendering.AgeDemographic == EAgeDemographic::Ead_Child)
					{
						AgentAgeText = "Child";
						AgeIndex = 0;
					}
					else if (EntityRendering.AgeDemographic == EAgeDemographic::Ead_Elderly)
					{
						AgentAgeText = "Elderly";
						AgeIndex = 2;
					}
					else
					{
						AgentAgeText = "Adult";
						AgeIndex = 1;
					}

					int32 GenderIndex = EntityRendering.bIsMale ? 0 : 1;
					AgentHeight = AgentHeightTable[AgeIndex][GenderIndex];
						
					SelectedAgentData.AgentID = EntityRendering.EntityID;
					SelectedAgentData.AgentName = FText::FromString(EntityInfo.EntityName);
					SelectedAgentData.Demographic = FText::FromString(AgentAgeText);
					SelectedAgentData.Gender = FText::FromString(AgentGenderText);
					SelectedAgentData.AgentWorldPosition = FVector(EntityMovement.CurrentLocation.X, EntityMovement.CurrentLocation.Y, EntityMovement.CurrentLocation.Z);
					SelectedAgentData.AgentSpeed = EntityMovement.CurrentSpeed;
					SelectedAgentData.GaitDirectionalSpeed = EntityMovement.GaitDirectionalSpeed;// todo: no gait speed implemented yet
					SelectedAgentData.AgentHeight = AgentHeight;
					SelectedAgentData.AgentSpeedFlux = EntityMovement.CurrentSpeed / EntityInfo.EntityMaxSpeed;
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
					
		float AgentHeight = 0.0f; // As no heights are provided in the JSON data, we will use defaults below
		// we may want to scale the height in a range based on known pedestrian movement bands for demographics and max speed or introduce a field

		int32 AgeIndex = 1; // default to Adult
		if (EntityRendering.AgeDemographic == EAgeDemographic::Ead_Child)
		{
			AgentAgeText = "Child";
			AgeIndex = 0;
		}
		else if (EntityRendering.AgeDemographic == EAgeDemographic::Ead_Elderly)
		{
			AgentAgeText = "Elderly";
			AgeIndex = 2;
		}
		else
		{
			AgentAgeText = "Adult";
			AgeIndex = 1;
		}

		int32 GenderIndex = EntityRendering.bIsMale ? 0 : 1;
		AgentHeight = AgentHeightTable[AgeIndex][GenderIndex];
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
		NewAgentData.AgentSpeedFlux = EntityMovement.CurrentSpeed / EntityInfo.EntityMaxSpeed;
		AgentData.Add(NewAgentData);
	}
}
