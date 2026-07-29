// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "MassAI/MassProcessor/Analytics/AgentEgressHealthCalculationProcessor.h"

#include "MassCommonTypes.h"
#include "BRisk/AgentTenabilityTimeline.h"
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

	// Rebuild analysis settings from the scenario's B-Risk endpoints once per load. Still needed:
	// ComputeInstantaneousTenability's Track-A risks/criteria read these every frame, and the timeline's
	// Layer-2 failure precompute is keyed on a hash of these same settings (SettingsHash).
	if (ScenarioGeneration != TenabilitySettingsGeneration)
	{
		TenabilitySettings = EgressSubsystem->BuildTenabilitySettingsFromEndpoints();
		TenabilitySettingsGeneration = ScenarioGeneration;
	}

	// Stale-timeline gate (three int compares — see FAgentTimelineKey): while the built agent-timeline
	// set doesn't match the CURRENT (agent file, B-Risk file, settings) triple, request a rebuild and
	// render the no-data state for every entity THIS frame. Never display a value computed from a
	// mismatched triple (scientific-integrity invariant 2) — there is no partial/interpolated fallback.
	if (!EgressSubsystem->AreAgentTimelinesCurrent())
	{
		EgressSubsystem->RequestAgentTimelineRebuild(EgressSubsystem->MakeCurrentTimelineKey());

		EntityQuery.ForEachEntityChunk(
			EntityManager,
			ExecutionContext,
			[](FMassExecutionContext& Context)
			{
				const TArrayView<FAgentEgressTenabilityFragment> HealthFragments =
					Context.GetMutableFragmentView<FAgentEgressTenabilityFragment>();

				// EVERY entity, rendered or not: the movement processor's failure-pose freeze keys
				// off DeathTimeSeconds alone (and re-shows hidden agents), so a stale death pose
				// left on ANY entity would snap it frozen during the rebuild window.
				for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
				{
					FAgentEgressTenabilityFragment& Health = HealthFragments[EntityIndex];
					// This is the no-data state the comment above names, and now it can say so: without
					// clearing this, an agent keeps a stale true through the whole rebuild window and its
					// bar keeps drawing from values computed against a mismatched triple.
					Health.bHasTenabilityData = false;
					Health.DisplayRisk = 0.0f;
					Health.VisibilityRisk = 0.0f;
					Health.ToxicFEDRisk = 0.0f;
					Health.ThermalFEDRisk = 0.0f;
					Health.TemperatureRisk = 0.0f;
					Health.LayerHeightRisk = 0.0f;
					Health.CurrentDominantCriterion = ETenabilityCriterion::None;
					Health.Health = 1.0f;
					Health.bVisibilityFailed = false;
					Health.bToxicFEDFailed = false;
					Health.bThermalFEDFailed = false;
					Health.bTemperatureFailed = false;
					Health.bLayerHeightFailed = false;
					Health.FailureMask = UE::Mobius::TenabilityFailureFlags::None;
					Health.bTenabilityFailed = false;
					Health.bIsDead = false;
					Health.FirstFailureTimeSeconds = -1.0f;
					Health.FirstFailureCriterion = ETenabilityCriterion::None;
					Health.DeathTimeSeconds = -1.0f;
					Health.DeathLocation = FVector::ZeroVector;
					Health.DeathRotation = FRotator::ZeroRotator;
					Health.TimelineIntervalCount = 0;
				}
			});
		return;
	}

	EntityQuery.ForEachEntityChunk(
		EntityManager,
		ExecutionContext,
		[this, EgressSubsystem, CurrentSimulationTime](FMassExecutionContext& Context)
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

				// No per-agent reset on scenario swap: dose is a closed-form query over the
				// precomputed timeline, and AreAgentTimelinesCurrent() above already gated on
				// the (agent file, B-Risk file, settings) triple.
				const UE::Mobius::Tenability::FAgentTenabilityTimeline* Timeline =
					EgressSubsystem->FindCurrentAgentTimeline(Rendering.EntityID);

				// Stand-in frame (streaming cold miss): CurrentLocation belongs to a different
				// timestep. Hold the INSTANTANEOUS sample/display at its last-good value — sampling
				// now would fabricate a room change from a wrong-timestep position. The dose and
				// failure projection below still run unconditionally: they are timeline-backed
				// (closed-form DoseAt / precomputed FirstFailureTimeSeconds), built from
				// ForEachTimestep's guaranteed-complete pass, so stand-in frames can never reach
				// them. One accepted side effect: the timeline dose used for DisplayRisk normalisation
				// inside ComputeInstantaneousTenability is also held for this one frame (it lives
				// inside this same skipped block) — self-heals the next frame the exact block lands,
				// same as the instantaneous display.
				if (!MovementFragments[EntityIndex].bSampleApproximate)
				{
					// Stateless per-frame recompute: evaluate tenability at the CURRENT time only.
					// FED dose comes from the timeline's closed-form DoseAt query (Prior + curve delta
					// since room entry), so scrubbing the timeline forward OR backward yields the exact
					// same value with no separate rewind/restore path.
					// Dose is resolved BEFORE the room test because it does not depend on one: DoseAt is
					// keyed on sim time and the agent's precomputed occupancy intervals, so it is just
					// as valid for an agent standing in unmodelled space as for one inside a room. It
					// stays inside the bSampleApproximate guard, though — on a stand-in frame the whole
					// display is held at its last-good value, dose included (see above).
					float ToxicDose = 0.0f;
					float ThermalDose = 0.0f;
					if (Timeline)
					{
						auto RoomSampler = [EgressSubsystem](
							int32 RoomIndex, double TimeSeconds, double& OutToxic, double& OutThermal)
						{
							EgressSubsystem->SampleTenabilityDoseAtRoomIndex(
								RoomIndex, TimeSeconds, OutToxic, OutThermal);
						};
						UE::Mobius::Tenability::FRoomFEDSampler SamplerRef(RoomSampler);
						Timeline->DoseAt(CurrentSimulationTime, SamplerRef, ToxicDose, ThermalDose);
					}
					// No timeline for this agent (never occupied a B-Risk room) -> zero dose.

					FAgentBRiskHazardSample HazardSample;
					if (!EgressSubsystem->SampleAgentEnvironment(
						MovementFragments[EntityIndex].CurrentLocation,
						Exposure.BreathingHeightCm,
						HazardSample,
						Exposure.CurrentRoomIndex))
					{
						// Agent is outside every B-Risk zone: no instantaneous (Track-A) data here, but
						// its DOSE survives — dose belongs to the agent, not to the room it happens to
						// be standing in. Zeroing the bar here (what this branch used to do) discarded a
						// dose already computed and made it jump back on re-entry.
						UE::Mobius::EgressHealth::ClearCurrentHazardSample(Exposure);
						UE::Mobius::Tenability::ComputeDoseOnlyTenability(
							Health, TenabilitySettings, ToxicDose, ThermalDose);
						Health.InstantaneousHazard = Health.DisplayRisk;
					}
					else
					{
						UE::Mobius::EgressHealth::ApplyCurrentHazardSample(Exposure, HazardSample);

						UE::Mobius::Tenability::ComputeInstantaneousTenability(
							Health,
							HazardSample,
							TenabilitySettings,
							CurrentSimulationTime,
							ToxicDose,
							ThermalDose);
					}

					// Is there anything to show? Evaluated AFTER the dose call, because dose is what
					// distinguishes "walked out of smoke carrying a real exposure" from "has never been
					// anywhere B-Risk modelled" — reading it beforehand would hide the bar for one
					// frame on the way out of every room. The failed case is added below, where the
					// timeline projection runs.
					Health.bHasTenabilityData = Exposure.bHasRoomSample
						|| Health.AccumulatedToxicFED > 0.0f
						|| Health.AccumulatedThermalFED > 0.0f;
				}

				// --- Failure state: PROJECTED from the timeline's precomputed Layer-2 fields, not
				// derived from a runtime latch. Navigation-independent by construction: the same
				// TL->FirstFailureTimeSeconds compared against the same CurrentSimulationTime always
				// yields the same bFailedByNow, regardless of how playback reached this time. ---
				const bool bTimelineHasFailure = Timeline && Timeline->FirstFailureTimeSeconds >= 0.0f;
				const bool bFailedByNow = bTimelineHasFailure
					&& CurrentSimulationTime + UE_SMALL_NUMBER >= Timeline->FirstFailureTimeSeconds;

				Health.bTenabilityFailed = bFailedByNow;
				Health.bIsDead = bFailedByNow;
				// Diagnostic (see the field's docs): 0 here is the reading that explains an agent which
				// never fails despite bad-looking conditions. Free — the timeline is already in hand.
				Health.TimelineIntervalCount = Timeline ? Timeline->Intervals.Num() : 0;
				Health.FirstFailureTimeSeconds = Timeline ? Timeline->FirstFailureTimeSeconds : -1.0f;
				Health.FirstFailureCriterion = Timeline ? Timeline->FirstFailureCriterion : ETenabilityCriterion::None;
				Health.FailureMask = bFailedByNow ? Timeline->FirstFailureMask : Health.FailureMask;
				Health.VisibilityFailureTimeSeconds = Timeline ? Timeline->VisibilityFailureTimeSeconds : -1.0f;
				Health.ToxicFEDFailureTimeSeconds = Timeline ? Timeline->ToxicFEDFailureTimeSeconds : -1.0f;
				Health.ThermalFEDFailureTimeSeconds = Timeline ? Timeline->ThermalFEDFailureTimeSeconds : -1.0f;
				Health.TemperatureFailureTimeSeconds = Timeline ? Timeline->TemperatureFailureTimeSeconds : -1.0f;
				Health.LayerHeightFailureTimeSeconds = Timeline ? Timeline->LayerHeightFailureTimeSeconds : -1.0f;
				// Death pose only when the timeline actually records a failure — a no-failure
				// timeline's FailureLocation is a meaningless default, not a pose.
				Health.DeathTimeSeconds = bTimelineHasFailure ? Timeline->FirstFailureTimeSeconds : -1.0f;
				Health.DeathLocation = bTimelineHasFailure ? Timeline->FailureLocation : FVector::ZeroVector;
				Health.DeathRotation = bTimelineHasFailure ? Timeline->FailureRotation : FRotator::ZeroRotator;

				if (bFailedByNow)
				{
					// Freeze the bar on the failure cause: full bar, criterion locked to the
					// first-failure criterion. This is what a reviewer needs at the end of a
					// scenario ("what made this agent stop"), not the conditions afterward.
					// Scrubbing to a time BEFORE the (fixed) failure time falls through to the
					// live pre-failure state computed above instead.
					Health.DisplayRisk = 1.0f;
					Health.CurrentDominantCriterion = Health.FirstFailureCriterion;
					Health.Health = 0.0f;
					// A failed agent always has something to show, wherever it is standing — the bar is
					// locked to the cause of failure and that is precisely what a reviewer needs at the
					// end of a scenario. Hiding it because the failure happened to occur outside a
					// modelled room would be worse than the ambiguity this flag exists to remove.
					Health.bHasTenabilityData = true;
				}

				Health.InstantaneousHazard = Health.DisplayRisk;
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
