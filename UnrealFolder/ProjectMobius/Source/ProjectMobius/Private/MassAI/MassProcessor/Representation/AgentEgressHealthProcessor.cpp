// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "MassAI/MassProcessor/Representation/AgentEgressHealthProcessor.h"

#include "MassExecutionContext.h"
#include "MassAI/Fragments/AgentEgressTenabilityFragments.h"
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
	EntityQuery.AddRequirement<FAgentEgressTenabilityFragment>(EMassFragmentAccess::ReadOnly);
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
			const TConstArrayView<FAgentEgressTenabilityFragment> HealthFragments =
				Context.GetFragmentView<FAgentEgressTenabilityFragment>();

			for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
			{
				const FEntityRenderingFragment& Rendering = RenderingFragments[EntityIndex];
				if (!Rendering.bRenderAgent || Rendering.EntityID < 0)
				{
					continue;
				}

				const FEntityMovementFragment& Movement = MovementFragments[EntityIndex];
				const FAgentEgressTenabilityFragment& Tenability = HealthFragments[EntityIndex];

				FAgentEgressTenabilityViewer& Viewer = AgentEgressHealthData.Emplace_GetRef(
					Rendering.EntityID,
					Movement.CurrentLocation,
					Tenability.Health);

				// Publish the full per-criterion tenability state, not just one float.
				Viewer.DisplayRisk = Tenability.DisplayRisk;
				Viewer.VisibilityRisk = Tenability.VisibilityRisk;
				Viewer.ToxicFEDRisk = Tenability.ToxicFEDRisk;
				Viewer.ThermalFEDRisk = Tenability.ThermalFEDRisk;
				Viewer.TemperatureRisk = Tenability.TemperatureRisk;
				Viewer.LayerHeightRisk = Tenability.LayerHeightRisk;
				Viewer.AccumulatedToxicFED = Tenability.AccumulatedToxicFED;
				Viewer.AccumulatedThermalFED = Tenability.AccumulatedThermalFED;
				Viewer.CurrentVisibilityM = Tenability.CurrentVisibilityM;
				Viewer.WorstVisibilityM = Tenability.WorstVisibilityM;
				Viewer.CurrentTemperatureC = Tenability.CurrentTemperatureC;
				Viewer.WorstTemperatureC = Tenability.WorstTemperatureC;
				Viewer.CurrentLayerHeightM = Tenability.CurrentLayerHeightM;
				Viewer.CurrentHeatReleaseKW = Tenability.CurrentHeatReleaseKW;
				Viewer.CurrentDominantCriterion = static_cast<uint8>(Tenability.CurrentDominantCriterion);
				Viewer.FirstFailureCriterion = static_cast<uint8>(Tenability.FirstFailureCriterion);
				Viewer.FailureMask = Tenability.FailureMask;
				Viewer.FirstFailureTimeSeconds = Tenability.FirstFailureTimeSeconds;
				Viewer.bTenabilityFailed = Tenability.bTenabilityFailed;
			}
		});

	StatisticSubsystem->PublishAgentEgressHealthData(AgentEgressHealthData);
}
