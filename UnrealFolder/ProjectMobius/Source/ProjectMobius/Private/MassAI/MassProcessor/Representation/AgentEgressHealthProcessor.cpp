// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "MassAI/MassProcessor/Representation/AgentEgressHealthProcessor.h"

#include "MassExecutionContext.h"
#include "MassAI/Fragments/AgentEgressHealthFragments.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/MassProcessor/Analytics/AgentEgressHealthCalculationProcessor.h"
#include "MassAI/Tags/MassAITags.h"
#include "Subsystems/StatisticSubsystem.h"

UAgentEgressHealthProcessor::UAgentEgressHealthProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
	ExecutionOrder.ExecuteAfter.Add(UAgentEgressHealthCalculationProcessor::StaticClass()->GetFName());
}

void UAgentEgressHealthProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FEntityMovementFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FEntityRenderingFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAgentEgressHealthFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::None);
	EntityQuery.AddTagRequirement<FMassEntityRepresentationTag>(EMassFragmentPresence::All);
	EntityQuery.RegisterWithProcessor(*this);

	ProcessorRequirements.AddSubsystemRequirement<UStatisticSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAgentEgressHealthProcessor::Execute(
	FMassEntityManager& EntityManager,
	FMassExecutionContext& ExecutionContext)
{
	UStatisticSubsystem* StatisticSubsystem = ExecutionContext.GetMutableSubsystem<UStatisticSubsystem>();
	if (!StatisticSubsystem)
	{
		return;
	}

	AgentEgressHealthData.Reset();
	AgentEgressHealthData.Reserve(EntityQuery.GetNumMatchingEntities(EntityManager));

	EntityQuery.ForEachEntityChunk(
		EntityManager,
		ExecutionContext,
		[this](FMassExecutionContext& Context)
		{
			const TConstArrayView<FEntityMovementFragment> MovementFragments =
				Context.GetFragmentView<FEntityMovementFragment>();
			const TConstArrayView<FEntityRenderingFragment> RenderingFragments =
				Context.GetFragmentView<FEntityRenderingFragment>();
			const TConstArrayView<FAgentEgressHealthFragment> HealthFragments =
				Context.GetFragmentView<FAgentEgressHealthFragment>();

			for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
			{
				const FEntityRenderingFragment& Rendering = RenderingFragments[EntityIndex];
				if (!Rendering.bRenderAgent || Rendering.EntityID < 0)
				{
					continue;
				}

				const FEntityMovementFragment& Movement = MovementFragments[EntityIndex];
				AgentEgressHealthData.Emplace(
					Rendering.EntityID,
					Movement.CurrentLocation,
					HealthFragments[EntityIndex].Health);
			}
		});

	StatisticSubsystem->PublishAgentEgressHealthData(AgentEgressHealthData);
}
