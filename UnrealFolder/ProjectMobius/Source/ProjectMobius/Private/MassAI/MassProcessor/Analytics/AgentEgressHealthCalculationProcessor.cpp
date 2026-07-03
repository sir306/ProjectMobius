// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "MassAI/MassProcessor/Analytics/AgentEgressHealthCalculationProcessor.h"

#include "MassCommonTypes.h"
#include "BRisk/BRiskEgressSubsystem.h"
#include "MassExecutionContext.h"
#include "MassAI/Fragments/AgentEgressTenabilityFragments.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Tags/MassAITags.h"
#include "Subsystems/TimeDilationSubSystem.h"

UAgentEgressHealthCalculationProcessor::UAgentEgressHealthCalculationProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);
}

void UAgentEgressHealthCalculationProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FEntityMovementFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FEntityRenderingFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAgentBRiskExposureFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FAgentEgressTenabilityFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::None);
	EntityQuery.AddTagRequirement<FMassEntityRepresentationTag>(EMassFragmentPresence::All);
	EntityQuery.RegisterWithProcessor(*this);

	ProcessorRequirements.AddSubsystemRequirement<UBRiskEgressSubsystem>(EMassFragmentAccess::ReadWrite);
	ProcessorRequirements.AddSubsystemRequirement<UTimeDilationSubSystem>(EMassFragmentAccess::ReadOnly);
}

void UAgentEgressHealthCalculationProcessor::Execute(
	FMassEntityManager& EntityManager,
	FMassExecutionContext& ExecutionContext)
{
	UBRiskEgressSubsystem* EgressSubsystem =
		ExecutionContext.GetMutableSubsystem<UBRiskEgressSubsystem>();
	const UTimeDilationSubSystem* TimeSubsystem =
		ExecutionContext.GetSubsystem<UTimeDilationSubSystem>();
	if (!EgressSubsystem || !TimeSubsystem)
	{
		return;
	}

	const float CurrentSimulationTime = TimeSubsystem->GetCurrentSimTime();
	const uint64 ScenarioGeneration = EgressSubsystem->GetScenarioGeneration();

	// Rebuild analysis settings from the scenario's B-Risk endpoints once per load.
	if (ScenarioGeneration != TenabilitySettingsGeneration)
	{
		TenabilitySettings = EgressSubsystem->BuildTenabilitySettingsFromEndpoints();
		TenabilitySettingsGeneration = ScenarioGeneration;
	}

	EntityQuery.ForEachEntityChunk(
		EntityManager,
		ExecutionContext,
		[this, EgressSubsystem, CurrentSimulationTime, ScenarioGeneration](FMassExecutionContext& Context)
		{
			const TConstArrayView<FEntityMovementFragment> MovementFragments =
				Context.GetFragmentView<FEntityMovementFragment>();
			const TConstArrayView<FEntityRenderingFragment> RenderingFragments =
				Context.GetFragmentView<FEntityRenderingFragment>();
			const TArrayView<FAgentBRiskExposureFragment> ExposureFragments =
				Context.GetMutableFragmentView<FAgentBRiskExposureFragment>();
			const TArrayView<FAgentEgressTenabilityFragment> HealthFragments =
				Context.GetMutableFragmentView<FAgentEgressTenabilityFragment>();

			for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
			{
				const FEntityRenderingFragment& Rendering = RenderingFragments[EntityIndex];
				if (!Rendering.bRenderAgent || Rendering.EntityID < 0)
				{
					continue;
				}

				FAgentBRiskExposureFragment& Exposure = ExposureFragments[EntityIndex];
				FAgentEgressTenabilityFragment& Health = HealthFragments[EntityIndex];

				if (Exposure.SourceScenarioGeneration != ScenarioGeneration)
				{
					UE::Mobius::EgressHealth::ResetAccumulatedExposure(Exposure, Health);
					UE::Mobius::EgressHealth::ClearCurrentHazardSample(Exposure, CurrentSimulationTime);
					Exposure.SourceScenarioGeneration = ScenarioGeneration;
				}

				// Stateless per-frame recompute: evaluate tenability at the CURRENT time
				// only. FED dose is recomputed from room-entry baselines (see
				// UpdateAgentTenability), so scrubbing the timeline forward OR backward
				// yields the correct value with no separate rewind/restore path.
				FAgentBRiskHazardSample HazardSample;
				if (!EgressSubsystem->SampleAgentEnvironment(
					MovementFragments[EntityIndex].CurrentLocation,
					Exposure.BreathingHeightCm,
					HazardSample,
					Exposure.CurrentRoomIndex))
				{
					// Agent is outside every B-Risk zone (no data here). Bank the accrued
					// dose (genuine forward exit only, see ClearCurrentHazardSample) and
					// drop the FED baseline so a later re-entry re-baselines.
					UE::Mobius::EgressHealth::ClearCurrentHazardSample(Exposure, CurrentSimulationTime);
					Health.InstantaneousHazard = 0.0f;

					// Unless the agent has locked a tenability failure by the current
					// time (its "cause of stop"), clear the bar to empty. Without this a
					// rewound or escaped agent keeps a STALE frozen bar from a time when
					// it was inside a zone (the visible "wrong bar outside the zone" bug).
					const bool bLockedFailure = Health.bTenabilityFailed
						&& CurrentSimulationTime + UE_SMALL_NUMBER >= Health.FirstFailureTimeSeconds;
					if (!bLockedFailure)
					{
						Health.DisplayRisk = 0.0f;
						Health.VisibilityRisk = 0.0f;
						Health.ToxicFEDRisk = 0.0f;
						Health.ThermalFEDRisk = 0.0f;
						Health.TemperatureRisk = 0.0f;
						Health.LayerHeightRisk = 0.0f;
						Health.CurrentDominantCriterion = ETenabilityCriterion::None;
						Health.Health = 1.0f;
					}
					continue;
				}

				UE::Mobius::EgressHealth::ApplyCurrentHazardSample(Exposure, HazardSample);

				UE::Mobius::Tenability::UpdateAgentTenability(
					Health,
					Exposure,
					HazardSample,
					TenabilitySettings,
					CurrentSimulationTime);

				// Legacy death-marker fields: "death" = first tenability failure (the
				// reported ASET cause), not biological death.
				Health.InstantaneousHazard = Health.DisplayRisk;
				Health.bIsDead = Health.bTenabilityFailed;
				if (Health.bTenabilityFailed && Health.DeathTimeSeconds < 0.0f)
				{
					Health.DeathTimeSeconds = Health.FirstFailureTimeSeconds;
					Health.DeathLocation = MovementFragments[EntityIndex].CurrentLocation;
					Health.DeathRotation = MovementFragments[EntityIndex].CurrentRotation;
				}
			}
		});
}

float UAgentEgressHealthCalculationProcessor::NormalizePositiveDose(
	const float Dose,
	const float Budget)
{
	return Budget > UE_KINDA_SMALL_NUMBER ? FMath::Max(Dose, 0.0f) / Budget : 0.0f;
}

float UAgentEgressHealthCalculationProcessor::NormalizeAboveThreshold(
	const float Value,
	const float Threshold,
	const float CriticalValue)
{
	const float Range = CriticalValue - Threshold;
	return Range > UE_KINDA_SMALL_NUMBER
		? FMath::Clamp((Value - Threshold) / Range, 0.0f, 1.0f)
		: 0.0f;
}
