// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "AgentEgressTenabilityFragments.generated.h"

/**
 * Independent egress-tenability failure categories. "Tenability" is deliberate:
 * these track loss of safe-evacuation conditions, NOT biological health. A
 * visibility failure means evacuation is no longer effective, not that the agent
 * died.
 */
UENUM(BlueprintType)
enum class ETenabilityCriterion : uint8
{
	None UMETA(DisplayName = "None"),
	Visibility UMETA(DisplayName = "Visibility"),
	ToxicFED UMETA(DisplayName = "Toxic FED"),
	ThermalFED UMETA(DisplayName = "Thermal FED"),
	Temperature UMETA(DisplayName = "Temperature"),
	LayerHeight UMETA(DisplayName = "Layer Height")
};

/** Bit flags so simultaneous tenability failures in one frame are all preserved. */
namespace UE::Mobius::TenabilityFailureFlags
{
	constexpr uint8 None = 0;
	constexpr uint8 Visibility = 1 << 0;
	constexpr uint8 ToxicFED = 1 << 1;
	constexpr uint8 ThermalFED = 1 << 2;
	constexpr uint8 Temperature = 1 << 3;
	constexpr uint8 LayerHeight = 1 << 4;
}

/**
 * Configurable tenability analysis settings. Defaults are documented fallbacks;
 * where a B-Risk input1.xml provides the value (monitor_height, endpoint_FED,
 * endpoint_visibility, endpoint_radiation) it should override these at load.
 *
 * Temperature and layer-height criteria default OFF: B-Risk's endpoint_temp is
 * not a Celsius layer-temperature threshold, so it must not be wired in blindly.
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FTenabilityAnalysisSettings
{
	GENERATED_BODY()

	/** Monitor/egress-path height (m) the B-Risk calculated values correspond to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tenability")
	float MonitorHeightM = 2.0f;

	/** Visibility at or below this (m) is a tenability failure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tenability")
	float EndpointVisibilityM = 10.0f;

	/**
	 * Clear-air visibility (m) used as the 0-risk reference for the visibility bar.
	 * Visibility risk ramps 0 -> 1 as visibility falls from this value to the
	 * endpoint, so the bar shows closeness to failure rather than only severity
	 * past it. B-Risk typically caps clear visibility at ~20-30 m.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tenability")
	float ReferenceVisibilityM = 20.0f;

	/** Accumulated toxic FED at or above this (dimensionless) is a failure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tenability")
	float EndpointToxicFED = 0.3f;

	/** Accumulated thermal FED at or above this (dimensionless) is a failure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tenability")
	float EndpointThermalFED = 1.0f;

	/** Temperature at or above this (C) is a failure when the temperature criterion is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tenability")
	float EndpointTemperatureC = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tenability")
	bool bUseVisibilityCriterion = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tenability")
	bool bUseToxicFEDCriterion = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tenability")
	bool bUseThermalFEDCriterion = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tenability")
	bool bUseTemperatureCriterion = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tenability")
	bool bUseLayerHeightCriterion = false;
};

namespace UE::Mobius::BRiskHazardChannels
{
	constexpr uint16 LayerHeight = 1 << 0;
	constexpr uint16 OpticalDensity = 1 << 1;
	constexpr uint16 Temperature = 1 << 2;
	constexpr uint16 CarbonMonoxide = 1 << 3;
	constexpr uint16 CarbonDioxide = 1 << 4;
	constexpr uint16 HydrogenCyanide = 1 << 5;
	constexpr uint16 Oxygen = 1 << 6;
	constexpr uint16 Soot = 1 << 7;
	constexpr uint16 DirectGasFed = 1 << 8;
	constexpr uint16 DirectThermalFed = 1 << 9;
}

/** Current B-Risk conditions sampled at an agent's breathing height. */
USTRUCT()
struct PROJECTMOBIUS_API FAgentBRiskHazardSample
{
	GENERATED_BODY()

	int32 RoomIndex = INDEX_NONE;
	int32 RoomId = INDEX_NONE;
	float SampleTimeSeconds = 0.0f;
	float LayerHeightWorldCm = 0.0f;
	float BreathingWorldZCm = 0.0f;
	float TemperatureC = 24.0f;
	float OpticalDensityPerMeter = 0.0f;
	float CarbonMonoxidePpm = 0.0f;
	float CarbonDioxidePercent = 0.05f;
	float HydrogenCyanidePpm = 0.0f;
	float OxygenPercent = 20.9f;
	float SootKgPerCubicMeter = 0.0f;
	float DirectGasFed = 0.0f;
	float DirectThermalFed = 0.0f;

	// B-Risk calculated tenability output for this room at the sample time (Track A).
	// These are monitor-height room/path values; FEDSum/FEDRadSum are cumulative
	// since t0. bHas* records presence so consumers never use a fabricated value.
	float CalcFEDSum = 0.0f;
	float CalcFEDRadSum = 0.0f;
	float CalcVisibilityM = 20.0f;
	float CalcHeatReleaseKW = 0.0f;
	float CalcLayerHeightM = 0.0f;
	float CalcUpperTemperatureC = 24.0f;
	float CalcLowerTemperatureC = 24.0f;
	bool bHasCalcFEDSum = false;
	bool bHasCalcFEDRadSum = false;
	bool bHasCalcVisibility = false;
	bool bHasCalcLayerHeight = false;
	bool bHasCalcTemperature = false;

	uint16 AvailableChannels = 0;
	bool bUpperLayer = false;
};

/**
 * Persistent per-agent exposure state.
 *
 * Raw cumulative fields deliberately retain physical units so the health model
 * can be replaced without changing B-Risk sampling or entity initialization.
 */
USTRUCT()
struct PROJECTMOBIUS_API FAgentBRiskExposureFragment : public FMassFragment
{
	GENERATED_BODY()

	float BreathingHeightCm = 160.0f;
	float LastSampleTimeSeconds = -1.0f;
	float IntegratedThroughTimeSeconds = -1.0f;
	uint64 SourceScenarioGeneration = 0;

	int32 CurrentRoomIndex = INDEX_NONE;
	int32 CurrentRoomId = INDEX_NONE;
	float CurrentLayerHeightWorldCm = 0.0f;
	float CurrentTemperatureC = 24.0f;
	float CurrentOpticalDensityPerMeter = 0.0f;
	float CurrentCarbonMonoxidePpm = 0.0f;
	float CurrentCarbonDioxidePercent = 0.05f;
	float CurrentHydrogenCyanidePpm = 0.0f;
	float CurrentOxygenPercent = 20.9f;
	float CurrentSootKgPerCubicMeter = 0.0f;
	float CurrentDirectGasFed = 0.0f;
	float CurrentDirectThermalFed = 0.0f;

	float CumulativeSmokeDose = 0.0f;
	float CumulativeHeatDose = 0.0f;
	float CumulativeCarbonMonoxideDose = 0.0f;
	float CumulativeCarbonDioxideDose = 0.0f;
	float CumulativeHydrogenCyanideDose = 0.0f;
	float CumulativeOxygenDeficitDose = 0.0f;

	// --- Track B per-zone FED accumulation (B-Risk cumulative FED model) ---
	// B-Risk FEDSum/FEDRadSum are cumulative-per-room since t0. A moving agent must
	// accrue only the portion gained while it occupies a room. Rather than summing
	// per-frame deltas (which is wrong under timeline scrubbing/rewind, where a
	// single time jump would dump a whole cumulative step), the agent's dose is
	// RECOMPUTED every frame as: PriorRooms + max(CurrentRoomFED - EntryRoomFED, 0).
	// This is a pure function of the current interpolated room value, so it rises
	// smoothly and decreases correctly when the timeline is rewound.
	int32 LastExposureRoomId = INDEX_NONE;
	int32 LastExposureRoomIndex = INDEX_NONE;
	bool bHasCumulativeFEDBaseline = false;
	// Room cumulative FED captured when the agent entered the current room.
	float EntryRoomToxicFED = 0.0f;
	float EntryRoomThermalFED = 0.0f;
	// Current room cumulative FED as of the last update (banked on room change).
	float LastSeenRoomToxicFED = 0.0f;
	float LastSeenRoomThermalFED = 0.0f;
	// Dose banked from previous rooms the agent has fully passed through.
	float PriorRoomsToxicFED = 0.0f;
	float PriorRoomsThermalFED = 0.0f;
	float LastTenabilitySampleTimeSeconds = -1.0f;

	uint16 AvailableChannels = 0;
	bool bHasRoomSample = false;
	bool bUpperLayer = false;
};

/** Result of the egress-tenability model consumed by UI and future gameplay systems. */
USTRUCT()
struct PROJECTMOBIUS_API FAgentEgressTenabilityFragment : public FMassFragment
{
	GENERATED_BODY()

	// --- Display-only aggregate. DisplayRisk is the MAX of enabled category risks,
	//     never their sum. Health is kept for backwards compatibility only:
	//     Health = 1 - Clamp01(DisplayRisk). It is NOT an analytical health value. ---
	float DisplayRisk = 0.0f;
	float Health = 1.0f;

	// --- Separate normalized per-category risks (0..1, not additive) ---
	float VisibilityRisk = 0.0f;
	float ToxicFEDRisk = 0.0f;
	float ThermalFEDRisk = 0.0f;
	float TemperatureRisk = 0.0f;
	float LayerHeightRisk = 0.0f;

	// --- Separate accumulated agent exposures (Track B deltas) ---
	float AccumulatedToxicFED = 0.0f;
	float AccumulatedThermalFED = 0.0f;

	// --- Current sampled values for UI/debug ---
	float CurrentVisibilityM = 20.0f;
	float CurrentTemperatureC = 24.0f;
	float CurrentLayerHeightM = 0.0f;
	float CurrentHeatReleaseKW = 0.0f;
	float CurrentFEDSum = 0.0f;
	float CurrentFEDRadSum = 0.0f;

	// --- Worst observed values ---
	float WorstVisibilityM = 20.0f;
	float WorstTemperatureC = 24.0f;

	// --- Independent failure flags ---
	bool bTenabilityFailed = false;
	bool bVisibilityFailed = false;
	bool bToxicFEDFailed = false;
	bool bThermalFEDFailed = false;
	bool bTemperatureFailed = false;
	bool bLayerHeightFailed = false;

	// --- Per-criterion failure times (seconds, -1 = not failed) ---
	float FirstFailureTimeSeconds = -1.0f;
	float VisibilityFailureTimeSeconds = -1.0f;
	float ToxicFEDFailureTimeSeconds = -1.0f;
	float ThermalFEDFailureTimeSeconds = -1.0f;
	float TemperatureFailureTimeSeconds = -1.0f;
	float LayerHeightFailureTimeSeconds = -1.0f;

	ETenabilityCriterion CurrentDominantCriterion = ETenabilityCriterion::None;
	ETenabilityCriterion FirstFailureCriterion = ETenabilityCriterion::None;

	/** Bit flags (UE::Mobius::TenabilityFailureFlags) of all criteria failed simultaneously. */
	uint8 FailureMask = 0;

	// --- Backwards-compatibility fields retained for the existing rewind history
	//     and death-marker consumers. DisplayRisk/criteria above are authoritative. ---
	float CombinedHazardDose = 0.0f;
	float InstantaneousHazard = 0.0f;
	float DeathTimeSeconds = -1.0f;
	FVector DeathLocation = FVector::ZeroVector;
	FRotator DeathRotation = FRotator::ZeroRotator;
	bool bIsDead = false;
};

namespace UE::Mobius::EgressHealth
{
	inline void ApplyCurrentHazardSample(
		FAgentBRiskExposureFragment& Exposure,
		const FAgentBRiskHazardSample& Sample)
	{
		Exposure.CurrentRoomIndex = Sample.RoomIndex;
		Exposure.CurrentRoomId = Sample.RoomId;
		Exposure.CurrentLayerHeightWorldCm = Sample.LayerHeightWorldCm;
		Exposure.CurrentTemperatureC = Sample.TemperatureC;
		Exposure.CurrentOpticalDensityPerMeter = Sample.OpticalDensityPerMeter;
		Exposure.CurrentCarbonMonoxidePpm = Sample.CarbonMonoxidePpm;
		Exposure.CurrentCarbonDioxidePercent = Sample.CarbonDioxidePercent;
		Exposure.CurrentHydrogenCyanidePpm = Sample.HydrogenCyanidePpm;
		Exposure.CurrentOxygenPercent = Sample.OxygenPercent;
		Exposure.CurrentSootKgPerCubicMeter = Sample.SootKgPerCubicMeter;
		Exposure.CurrentDirectGasFed = Sample.DirectGasFed;
		Exposure.CurrentDirectThermalFed = Sample.DirectThermalFed;
		Exposure.AvailableChannels = Sample.AvailableChannels;
		Exposure.bHasRoomSample = true;
		Exposure.bUpperLayer = Sample.bUpperLayer;
	}

	inline void ClearCurrentHazardSample(FAgentBRiskExposureFragment& Exposure)
	{
		Exposure.CurrentRoomIndex = INDEX_NONE;
		Exposure.CurrentRoomId = INDEX_NONE;
		Exposure.CurrentLayerHeightWorldCm = 0.0f;
		Exposure.CurrentTemperatureC = 24.0f;
		Exposure.CurrentOpticalDensityPerMeter = 0.0f;
		Exposure.CurrentCarbonMonoxidePpm = 0.0f;
		Exposure.CurrentCarbonDioxidePercent = 0.05f;
		Exposure.CurrentHydrogenCyanidePpm = 0.0f;
		Exposure.CurrentOxygenPercent = 20.9f;
		Exposure.CurrentSootKgPerCubicMeter = 0.0f;
		Exposure.CurrentDirectGasFed = 0.0f;
		Exposure.CurrentDirectThermalFed = 0.0f;
		Exposure.AvailableChannels = 0;
		Exposure.bHasRoomSample = false;
		Exposure.bUpperLayer = false;

		// Agent occupies no B-Risk room: drop the baseline so a later room entry
		// re-baselines. NOTE: we deliberately do NOT bank the just-left room's dose
		// into PriorRooms here. Banking on every exit corrupts accumulation under
		// timeline scrubbing, because rewinding an agent back across a zone boundary
		// triggers a spurious "exit" and double-banks. Banking for genuine A->B room
		// transitions is handled in UpdateAgentTenability, where both rooms are sampled.
		Exposure.LastExposureRoomId = INDEX_NONE;
		Exposure.LastExposureRoomIndex = INDEX_NONE;
		Exposure.bHasCumulativeFEDBaseline = false;
		Exposure.EntryRoomToxicFED = 0.0f;
		Exposure.EntryRoomThermalFED = 0.0f;
		Exposure.LastSeenRoomToxicFED = 0.0f;
		Exposure.LastSeenRoomThermalFED = 0.0f;
	}

	inline void ResetAccumulatedExposure(
		FAgentBRiskExposureFragment& Exposure,
		FAgentEgressTenabilityFragment& Health)
	{
		Exposure.CumulativeSmokeDose = 0.0f;
		Exposure.CumulativeHeatDose = 0.0f;
		Exposure.CumulativeCarbonMonoxideDose = 0.0f;
		Exposure.CumulativeCarbonDioxideDose = 0.0f;
		Exposure.CumulativeHydrogenCyanideDose = 0.0f;
		Exposure.CumulativeOxygenDeficitDose = 0.0f;
		Exposure.CurrentDirectGasFed = 0.0f;
		Exposure.CurrentDirectThermalFed = 0.0f;

		Exposure.LastExposureRoomId = INDEX_NONE;
		Exposure.LastExposureRoomIndex = INDEX_NONE;
		Exposure.bHasCumulativeFEDBaseline = false;
		Exposure.EntryRoomToxicFED = 0.0f;
		Exposure.EntryRoomThermalFED = 0.0f;
		Exposure.LastSeenRoomToxicFED = 0.0f;
		Exposure.LastSeenRoomThermalFED = 0.0f;
		Exposure.PriorRoomsToxicFED = 0.0f;
		Exposure.PriorRoomsThermalFED = 0.0f;
		Exposure.LastTenabilitySampleTimeSeconds = -1.0f;

		Health = FAgentEgressTenabilityFragment();
	}
}

/**
 * Defensible B-Risk tenability model. Each category is tracked independently and
 * the single display value is the MAX of enabled risks, never their sum.
 *
 * Two tracks (see refactor docs):
 *  - Track B (cumulative dose): toxic/thermal FED accrue ONLY the per-frame delta
 *    of the room's cumulative B-Risk FEDSum/FEDRadSum while the agent is present.
 *    A late entrant never inherits the room's earlier exposure.
 *  - Track A (instantaneous): visibility, layer height and temperature are read
 *    from the room's current calculated values at the monitor height. The
 *    instantaneous Temperature criterion is the correct signal for "this zone is
 *    thermally untenable now" — cumulative FEDRadSum saturates at 1.0 in B-Risk
 *    output, so its per-frame delta cannot flag a zone that was already lethal at
 *    entry. Enable the Temperature criterion for scenarios where FEDRadSum saturates.
 */
namespace UE::Mobius::Tenability
{
	// All risks are normalized to the same meaning: 0 = safe, 1 = at the failure
	// endpoint. DisplayRisk is the max of these, so the bar reads as "closeness to
	// tenability failure" and reaches 1 exactly when a criterion fails.

	/** Visibility risk: 0 at the clear-air reference, ramping to 1 at the endpoint. */
	inline float ComputeVisibilityRisk(
		const float CurrentVisibilityM, const float EndpointVisibilityM, const float ReferenceVisibilityM)
	{
		const float Span = ReferenceVisibilityM - EndpointVisibilityM;
		return Span > UE_SMALL_NUMBER
			? FMath::Clamp((ReferenceVisibilityM - CurrentVisibilityM) / Span, 0.0f, 1.0f)
			: (CurrentVisibilityM <= EndpointVisibilityM ? 1.0f : 0.0f);
	}

	/** FED risk: accumulated agent dose as a fraction of the endpoint, clamped to [0,1] for display. */
	inline float ComputeFEDRisk(const float AccumulatedFED, const float EndpointFED)
	{
		return EndpointFED > UE_SMALL_NUMBER
			? FMath::Clamp(FMath::Max(AccumulatedFED, 0.0f) / EndpointFED, 0.0f, 1.0f)
			: 0.0f;
	}

	/** Temperature risk: current monitor-height temperature as a fraction of the endpoint, clamped [0,1]. */
	inline float ComputeTemperatureRisk(const float CurrentTemperatureC, const float EndpointTemperatureC)
	{
		return EndpointTemperatureC > UE_SMALL_NUMBER
			? FMath::Clamp(FMath::Max(CurrentTemperatureC, 0.0f) / EndpointTemperatureC, 0.0f, 1.0f)
			: 0.0f;
	}

	/**
	 * Advance one agent's tenability state for the current sample.
	 *
	 * @param Tenability        Per-agent result fragment (separate risks + failure state).
	 * @param Exposure          Per-agent exposure fragment (FED delta baselines).
	 * @param Sample            Current B-Risk room sample at the agent (Track A calc values).
	 * @param Settings          Endpoints + which criteria are enabled.
	 * @param CurrentSimTime    Simulation time in seconds (for failure-time stamping).
	 */
	inline void UpdateAgentTenability(
		FAgentEgressTenabilityFragment& Tenability,
		FAgentBRiskExposureFragment& Exposure,
		const FAgentBRiskHazardSample& Sample,
		const FTenabilityAnalysisSettings& Settings,
		const float CurrentSimTime)
	{
		// --- Track B: per-room FED dose, RECOMPUTED from baselines each frame ---
		// Total dose = PriorRooms + max(CurrentRoomFED - EntryRoomFED, 0). Because
		// this is a pure function of the current interpolated room value (not a
		// running sum of per-frame deltas), it rises smoothly with playback and
		// decreases correctly under rewind/scrub. Entering a new room banks the
		// previous room's contribution and re-baselines (no historical exposure).
		const bool bSameRoomSource =
			Exposure.bHasCumulativeFEDBaseline
			&& Exposure.LastExposureRoomId == Sample.RoomId
			&& Exposure.LastExposureRoomIndex == Sample.RoomIndex;

		if (Sample.bHasCalcFEDSum || Sample.bHasCalcFEDRadSum)
		{
			if (!bSameRoomSource)
			{
				if (Exposure.bHasCumulativeFEDBaseline)
				{
					Exposure.PriorRoomsToxicFED +=
						FMath::Max(Exposure.LastSeenRoomToxicFED - Exposure.EntryRoomToxicFED, 0.0f);
					Exposure.PriorRoomsThermalFED +=
						FMath::Max(Exposure.LastSeenRoomThermalFED - Exposure.EntryRoomThermalFED, 0.0f);
				}
				Exposure.EntryRoomToxicFED = Sample.CalcFEDSum;
				Exposure.EntryRoomThermalFED = Sample.CalcFEDRadSum;
				Exposure.LastExposureRoomId = Sample.RoomId;
				Exposure.LastExposureRoomIndex = Sample.RoomIndex;
				Exposure.bHasCumulativeFEDBaseline = true;
			}

			Exposure.LastSeenRoomToxicFED = Sample.CalcFEDSum;
			Exposure.LastSeenRoomThermalFED = Sample.CalcFEDRadSum;

			const float CurrentRoomToxic = FMath::Max(Sample.CalcFEDSum - Exposure.EntryRoomToxicFED, 0.0f);
			const float CurrentRoomThermal = FMath::Max(Sample.CalcFEDRadSum - Exposure.EntryRoomThermalFED, 0.0f);
			Tenability.AccumulatedToxicFED = Exposure.PriorRoomsToxicFED + CurrentRoomToxic;
			Tenability.AccumulatedThermalFED = Exposure.PriorRoomsThermalFED + CurrentRoomThermal;
		}
		Exposure.LastTenabilitySampleTimeSeconds = CurrentSimTime;

		// --- Track A: instantaneous current values for UI/criteria ---
		Tenability.CurrentFEDSum = Sample.CalcFEDSum;
		Tenability.CurrentFEDRadSum = Sample.CalcFEDRadSum;
		Tenability.CurrentVisibilityM = Sample.CalcVisibilityM;
		Tenability.CurrentLayerHeightM = Sample.CalcLayerHeightM;
		Tenability.CurrentHeatReleaseKW = Sample.CalcHeatReleaseKW;

		// Temperature at monitor height: upper layer when the monitor is at/above the
		// smoke-layer interface, else the lower layer. Falls back to the raw layer
		// sample temperature when B-Risk calculated layer temps are unavailable.
		const bool bMonitorInUpperLayer = Sample.bHasCalcLayerHeight
			? Settings.MonitorHeightM >= Sample.CalcLayerHeightM
			: Sample.bUpperLayer;
		Tenability.CurrentTemperatureC = Sample.bHasCalcTemperature
			? (bMonitorInUpperLayer ? Sample.CalcUpperTemperatureC : Sample.CalcLowerTemperatureC)
			: Sample.TemperatureC;

		Tenability.WorstVisibilityM = FMath::Min(Tenability.WorstVisibilityM, Tenability.CurrentVisibilityM);
		Tenability.WorstTemperatureC = FMath::Max(Tenability.WorstTemperatureC, Tenability.CurrentTemperatureC);

		// --- Separate normalized risks (enabled criteria only) ---
		Tenability.VisibilityRisk = 0.0f;
		Tenability.ToxicFEDRisk = 0.0f;
		Tenability.ThermalFEDRisk = 0.0f;
		Tenability.TemperatureRisk = 0.0f;
		Tenability.LayerHeightRisk = 0.0f;

		if (Settings.bUseVisibilityCriterion && Sample.bHasCalcVisibility)
		{
			Tenability.VisibilityRisk = ComputeVisibilityRisk(
				Tenability.CurrentVisibilityM, Settings.EndpointVisibilityM, Settings.ReferenceVisibilityM);
			Tenability.bVisibilityFailed = Tenability.CurrentVisibilityM <= Settings.EndpointVisibilityM;
		}
		if (Settings.bUseToxicFEDCriterion && Sample.bHasCalcFEDSum)
		{
			Tenability.ToxicFEDRisk = ComputeFEDRisk(Tenability.AccumulatedToxicFED, Settings.EndpointToxicFED);
			Tenability.bToxicFEDFailed = Tenability.AccumulatedToxicFED >= Settings.EndpointToxicFED;
		}
		if (Settings.bUseThermalFEDCriterion && Sample.bHasCalcFEDRadSum)
		{
			Tenability.ThermalFEDRisk = ComputeFEDRisk(Tenability.AccumulatedThermalFED, Settings.EndpointThermalFED);
			Tenability.bThermalFEDFailed = Tenability.AccumulatedThermalFED >= Settings.EndpointThermalFED;
		}
		if (Settings.bUseTemperatureCriterion)
		{
			Tenability.TemperatureRisk = ComputeTemperatureRisk(Tenability.CurrentTemperatureC, Settings.EndpointTemperatureC);
			Tenability.bTemperatureFailed = Tenability.CurrentTemperatureC >= Settings.EndpointTemperatureC;
		}
		if (Settings.bUseLayerHeightCriterion && Sample.bHasCalcLayerHeight)
		{
			// Smoke descends toward the monitor height; fail when the interface reaches it.
			Tenability.LayerHeightRisk = Settings.MonitorHeightM > UE_SMALL_NUMBER
				? FMath::Clamp((Settings.MonitorHeightM - Tenability.CurrentLayerHeightM) / Settings.MonitorHeightM, 0.0f, 1.0f)
				: 0.0f;
			Tenability.bLayerHeightFailed = Tenability.CurrentLayerHeightM <= Settings.MonitorHeightM;
		}

		// --- Display risk = MAX of enabled risks (NEVER the sum) ---
		Tenability.DisplayRisk = FMath::Max(
			FMath::Max3(Tenability.VisibilityRisk, Tenability.ToxicFEDRisk, Tenability.ThermalFEDRisk),
			FMath::Max(Tenability.TemperatureRisk, Tenability.LayerHeightRisk));

		// --- Dominant criterion = argmax of enabled risks ---
		{
			float BestRisk = Tenability.VisibilityRisk;
			ETenabilityCriterion Best = ETenabilityCriterion::Visibility;
			const auto Consider = [&BestRisk, &Best](const float Risk, const ETenabilityCriterion Criterion)
			{
				if (Risk > BestRisk)
				{
					BestRisk = Risk;
					Best = Criterion;
				}
			};
			Consider(Tenability.ToxicFEDRisk, ETenabilityCriterion::ToxicFED);
			Consider(Tenability.ThermalFEDRisk, ETenabilityCriterion::ThermalFED);
			Consider(Tenability.TemperatureRisk, ETenabilityCriterion::Temperature);
			Consider(Tenability.LayerHeightRisk, ETenabilityCriterion::LayerHeight);
			Tenability.CurrentDominantCriterion = BestRisk > 0.0f ? Best : ETenabilityCriterion::None;
		}

		// --- Per-criterion failure-time stamping (first crossing per category) ---
		const auto StampFailure = [CurrentSimTime](bool bFailed, float& FailureTime)
		{
			if (bFailed && FailureTime < 0.0f)
			{
				FailureTime = CurrentSimTime;
			}
		};
		StampFailure(Tenability.bVisibilityFailed, Tenability.VisibilityFailureTimeSeconds);
		StampFailure(Tenability.bToxicFEDFailed, Tenability.ToxicFEDFailureTimeSeconds);
		StampFailure(Tenability.bThermalFEDFailed, Tenability.ThermalFEDFailureTimeSeconds);
		StampFailure(Tenability.bTemperatureFailed, Tenability.TemperatureFailureTimeSeconds);
		StampFailure(Tenability.bLayerHeightFailed, Tenability.LayerHeightFailureTimeSeconds);

		// --- Failure bitmask preserves ALL simultaneous failures ---
		Tenability.FailureMask = UE::Mobius::TenabilityFailureFlags::None;
		if (Tenability.bVisibilityFailed) { Tenability.FailureMask |= UE::Mobius::TenabilityFailureFlags::Visibility; }
		if (Tenability.bToxicFEDFailed) { Tenability.FailureMask |= UE::Mobius::TenabilityFailureFlags::ToxicFED; }
		if (Tenability.bThermalFEDFailed) { Tenability.FailureMask |= UE::Mobius::TenabilityFailureFlags::ThermalFED; }
		if (Tenability.bTemperatureFailed) { Tenability.FailureMask |= UE::Mobius::TenabilityFailureFlags::Temperature; }
		if (Tenability.bLayerHeightFailed) { Tenability.FailureMask |= UE::Mobius::TenabilityFailureFlags::LayerHeight; }

		// --- Lock the first failed criterion using the display priority order ---
		const bool bAnyFailed = Tenability.FailureMask != UE::Mobius::TenabilityFailureFlags::None;
		if (bAnyFailed && !Tenability.bTenabilityFailed)
		{
			Tenability.bTenabilityFailed = true;
			Tenability.FirstFailureTimeSeconds = CurrentSimTime;
			Tenability.FirstFailureCriterion =
				Tenability.bVisibilityFailed ? ETenabilityCriterion::Visibility :
				Tenability.bToxicFEDFailed ? ETenabilityCriterion::ToxicFED :
				Tenability.bThermalFEDFailed ? ETenabilityCriterion::ThermalFED :
				Tenability.bTemperatureFailed ? ETenabilityCriterion::Temperature :
				ETenabilityCriterion::LayerHeight;
		}

		// --- Lock to the failure cause once the agent has failed by this time ---
		// After reaching its first tenability failure an agent is treated as stopped,
		// so the bar FREEZES on the cause: first-failure criterion + full bar. This is
		// what a reviewer needs at the end of a scenario ("what made this agent stop"),
		// not the conditions it would have seen afterward. Scrubbing to a time BEFORE
		// the (fixed) failure time shows the live pre-failure state again.
		if (Tenability.bTenabilityFailed
			&& CurrentSimTime + UE_SMALL_NUMBER >= Tenability.FirstFailureTimeSeconds)
		{
			Tenability.DisplayRisk = 1.0f;
			Tenability.CurrentDominantCriterion = Tenability.FirstFailureCriterion;
		}

		// --- Backwards-compatible display-only Health (NOT analytical) ---
		Tenability.Health = 1.0f - FMath::Clamp(Tenability.DisplayRisk, 0.0f, 1.0f);
	}
}
