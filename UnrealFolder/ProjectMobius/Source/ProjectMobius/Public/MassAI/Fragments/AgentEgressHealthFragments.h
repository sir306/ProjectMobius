// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "AgentEgressHealthFragments.generated.h"

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

	uint16 AvailableChannels = 0;
	bool bHasRoomSample = false;
	bool bUpperLayer = false;
};

/** Result of the egress-health model consumed by UI and future gameplay systems. */
USTRUCT()
struct PROJECTMOBIUS_API FAgentEgressHealthFragment : public FMassFragment
{
	GENERATED_BODY()

	float Health = 1.0f;
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
	}

	inline void ResetAccumulatedExposure(
		FAgentBRiskExposureFragment& Exposure,
		FAgentEgressHealthFragment& Health)
	{
		Exposure.CumulativeSmokeDose = 0.0f;
		Exposure.CumulativeHeatDose = 0.0f;
		Exposure.CumulativeCarbonMonoxideDose = 0.0f;
		Exposure.CumulativeCarbonDioxideDose = 0.0f;
		Exposure.CumulativeHydrogenCyanideDose = 0.0f;
		Exposure.CumulativeOxygenDeficitDose = 0.0f;
		Exposure.CurrentDirectGasFed = 0.0f;
		Exposure.CurrentDirectThermalFed = 0.0f;
		Health.Health = 1.0f;
		Health.CombinedHazardDose = 0.0f;
		Health.InstantaneousHazard = 0.0f;
		Health.DeathTimeSeconds = -1.0f;
		Health.DeathLocation = FVector::ZeroVector;
		Health.DeathRotation = FRotator::ZeroRotator;
		Health.bIsDead = false;
	}
}
