// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "MassAI/MassProcessor/Analytics/AgentEgressHealthCalculationProcessor.h"

#include "BRisk/BRiskEgressSubsystem.h"
#include "MassExecutionContext.h"
#include "MassAI/Fragments/AgentEgressHealthFragments.h"
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
	EntityQuery.AddRequirement<FAgentEgressHealthFragment>(EMassFragmentAccess::ReadWrite);
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
			const TArrayView<FAgentEgressHealthFragment> HealthFragments =
				Context.GetMutableFragmentView<FAgentEgressHealthFragment>();

			for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
			{
				const FEntityRenderingFragment& Rendering = RenderingFragments[EntityIndex];
				if (!Rendering.bRenderAgent || Rendering.EntityID < 0)
				{
					continue;
				}

				FAgentBRiskExposureFragment& Exposure = ExposureFragments[EntityIndex];
				FAgentEgressHealthFragment& Health = HealthFragments[EntityIndex];
				const int32 AgentId = Rendering.EntityID;

				if (Exposure.SourceScenarioGeneration != ScenarioGeneration)
				{
					UE::Mobius::EgressHealth::ResetAccumulatedExposure(Exposure, Health);
					UE::Mobius::EgressHealth::ClearCurrentHazardSample(Exposure);
					Exposure.LastSampleTimeSeconds = -1.0f;
					Exposure.IntegratedThroughTimeSeconds = -1.0f;
					Exposure.SourceScenarioGeneration = ScenarioGeneration;
				}

				if (Exposure.IntegratedThroughTimeSeconds >= 0.0f
					&& CurrentSimulationTime
						<= Exposure.IntegratedThroughTimeSeconds + UE_KINDA_SMALL_NUMBER)
				{
					if (!EgressSubsystem->RestoreAgentHealth(AgentId, CurrentSimulationTime, Health))
					{
						EgressSubsystem->RecordAgentHealth(
							AgentId,
							Exposure.IntegratedThroughTimeSeconds,
							Health);
					}
					Exposure.LastSampleTimeSeconds = CurrentSimulationTime;
					continue;
				}

				if (Health.DeathTimeSeconds >= 0.0f)
				{
					Health.bIsDead = true;
					Health.Health = 0.0f;
					continue;
				}

				const float IntegrationStartTime = FMath::Max(
					Exposure.LastSampleTimeSeconds,
					Exposure.IntegratedThroughTimeSeconds);
				const float DeltaSeconds = IntegrationStartTime >= 0.0f
					? FMath::Clamp(
						CurrentSimulationTime - IntegrationStartTime,
						0.0f,
						MaximumIntegrationStepSeconds)
					: 0.0f;
				Exposure.LastSampleTimeSeconds = CurrentSimulationTime;
				if (DeltaSeconds > 0.0f)
				{
					Exposure.IntegratedThroughTimeSeconds = FMath::Max(
						Exposure.IntegratedThroughTimeSeconds,
						IntegrationStartTime + DeltaSeconds);
				}
				else if (Exposure.IntegratedThroughTimeSeconds < 0.0f)
				{
					Exposure.IntegratedThroughTimeSeconds = CurrentSimulationTime;
				}

				FAgentBRiskHazardSample HazardSample;
				if (!EgressSubsystem->SampleAgentEnvironment(
					MovementFragments[EntityIndex].CurrentLocation,
					Exposure.BreathingHeightCm,
					HazardSample,
					Exposure.CurrentRoomIndex))
				{
					UE::Mobius::EgressHealth::ClearCurrentHazardSample(Exposure);
					Health.InstantaneousHazard = 0.0f;
					EgressSubsystem->RecordAgentHealth(
						AgentId,
						Exposure.IntegratedThroughTimeSeconds,
						Health);
					continue;
				}

				UE::Mobius::EgressHealth::ApplyCurrentHazardSample(Exposure, HazardSample);

				const float SmokeExcess = FMath::Max(
					Exposure.CurrentOpticalDensityPerMeter - OpticalDensityThresholdPerMeter,
					0.0f);
				const float HeatExcess = FMath::Max(Exposure.CurrentTemperatureC - HeatThresholdC, 0.0f);
				const float CarbonMonoxideExcess = FMath::Max(
					Exposure.CurrentCarbonMonoxidePpm - CarbonMonoxideThresholdPpm,
					0.0f);
				const float CarbonDioxideExcess = FMath::Max(
					Exposure.CurrentCarbonDioxidePercent - CarbonDioxideThresholdPercent,
					0.0f);
				const float HydrogenCyanideExcess = FMath::Max(
					Exposure.CurrentHydrogenCyanidePpm - HydrogenCyanideThresholdPpm,
					0.0f);
				const float OxygenDeficit = FMath::Max(
					OxygenThresholdPercent - Exposure.CurrentOxygenPercent,
					0.0f);

				Exposure.CumulativeSmokeDose += SmokeExcess * DeltaSeconds;
				Exposure.CumulativeHeatDose += HeatExcess * DeltaSeconds;
				Exposure.CumulativeCarbonMonoxideDose += CarbonMonoxideExcess * DeltaSeconds;
				Exposure.CumulativeCarbonDioxideDose += CarbonDioxideExcess * DeltaSeconds;
				Exposure.CumulativeHydrogenCyanideDose += HydrogenCyanideExcess * DeltaSeconds;
				Exposure.CumulativeOxygenDeficitDose += OxygenDeficit * DeltaSeconds;

				const float SmokeDose = NormalizePositiveDose(
					Exposure.CumulativeSmokeDose,
					SmokeDoseBudget);
				const float ThermalDose =
					(Exposure.AvailableChannels
						& UE::Mobius::BRiskHazardChannels::DirectThermalFed) != 0
					? Exposure.CurrentDirectThermalFed
					: NormalizePositiveDose(Exposure.CumulativeHeatDose, HeatDoseBudget);
				const float ComputedGasDose =
					NormalizePositiveDose(
						Exposure.CumulativeCarbonMonoxideDose,
						CarbonMonoxideDoseBudget)
					+ NormalizePositiveDose(
						Exposure.CumulativeCarbonDioxideDose,
						CarbonDioxideDoseBudget)
					+ NormalizePositiveDose(
						Exposure.CumulativeHydrogenCyanideDose,
						HydrogenCyanideDoseBudget)
					+ NormalizePositiveDose(
						Exposure.CumulativeOxygenDeficitDose,
						OxygenDeficitDoseBudget);
				const float GasDose =
					(Exposure.AvailableChannels
						& UE::Mobius::BRiskHazardChannels::DirectGasFed) != 0
					? Exposure.CurrentDirectGasFed
					: ComputedGasDose;

				Health.CombinedHazardDose = FMath::Max(
					Health.CombinedHazardDose,
					SmokeDose + ThermalDose + GasDose);
				Health.Health = 1.0f - FMath::Clamp(Health.CombinedHazardDose, 0.0f, 1.0f);
				if (Health.Health <= UE_KINDA_SMALL_NUMBER)
				{
					Health.Health = 0.0f;
					Health.bIsDead = true;
					Health.DeathTimeSeconds = Exposure.IntegratedThroughTimeSeconds;
					Health.DeathLocation = MovementFragments[EntityIndex].CurrentLocation;
					Health.DeathRotation = MovementFragments[EntityIndex].CurrentRotation;
				}

				const float SmokeSeverity = NormalizeAboveThreshold(
					Exposure.CurrentOpticalDensityPerMeter,
					OpticalDensityThresholdPerMeter,
					2.0f);
				const float HeatSeverity = NormalizeAboveThreshold(
					Exposure.CurrentTemperatureC,
					HeatThresholdC,
					180.0f);
				const float CarbonMonoxideSeverity = NormalizeAboveThreshold(
					Exposure.CurrentCarbonMonoxidePpm,
					CarbonMonoxideThresholdPpm,
					1200.0f);
				const float HydrogenCyanideSeverity = NormalizeAboveThreshold(
					Exposure.CurrentHydrogenCyanidePpm,
					HydrogenCyanideThresholdPpm,
					200.0f);
				const float CarbonDioxideSeverity = NormalizeAboveThreshold(
					Exposure.CurrentCarbonDioxidePercent,
					CarbonDioxideThresholdPercent,
					10.0f);
				const float OxygenSeverity = OxygenThresholdPercent > 10.0f
					? FMath::Clamp(
						(OxygenThresholdPercent - Exposure.CurrentOxygenPercent)
						/ (OxygenThresholdPercent - 10.0f),
						0.0f,
						1.0f)
					: 0.0f;

				Health.InstantaneousHazard = FMath::Max(
					FMath::Max(SmokeSeverity, HeatSeverity),
					FMath::Max(
						FMath::Max(
							FMath::Max(CarbonMonoxideSeverity, CarbonDioxideSeverity),
							HydrogenCyanideSeverity),
						OxygenSeverity));
				EgressSubsystem->RecordAgentHealth(
					AgentId,
					Exposure.IntegratedThroughTimeSeconds,
					Health);
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
