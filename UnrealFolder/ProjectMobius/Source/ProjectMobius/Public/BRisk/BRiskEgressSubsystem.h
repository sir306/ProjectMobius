// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

#include "CoreMinimal.h"
#include "MassEntitySubsystem.h"
#include "BRiskDataImporter.h"
#include "MassAI/Fragments/AgentEgressTenabilityFragments.h"
#include "Subsystems/WorldSubsystem.h"
#include <initializer_list>
#include "BRiskEgressSubsystem.generated.h"

class UBRiskDataSubsystem;
class UTimeDilationSubSystem;
class UBRiskEgressSubsystem;

template<>
struct TMassExternalSubsystemTraits<UBRiskEgressSubsystem> final
{
	enum
	{
		ThreadSafeRead = true,
		ThreadSafeWrite = false,
		GameThreadOnly = true,
	};
};

/** One upper or lower B-Risk layer resolved into health-relevant units. */
struct FBRiskEgressLayerState
{
	float TemperatureC = 24.0f;
	float OpticalDensityPerMeter = 0.0f;
	float CarbonMonoxidePpm = 0.0f;
	float CarbonDioxidePercent = 0.05f;
	float HydrogenCyanidePpm = 0.0f;
	float OxygenPercent = 20.9f;
	float SootKgPerCubicMeter = 0.0f;
	uint16 AvailableChannels = 0;
};

/** Current sampled environmental state for one B-Risk room. */
struct FBRiskEgressRoomState
{
	int32 RoomIndex = INDEX_NONE;
	int32 RoomId = INDEX_NONE;
	FBox WorldBounds = FBox(ForceInit);
	float SampleTimeSeconds = 0.0f;
	float LayerHeightWorldCm = 0.0f;
	float DirectGasFed = 0.0f;
	float DirectThermalFed = 0.0f;

	// B-Risk calculated tenability output for this room at SampleTimeSeconds (Track A),
	// interpolated from output1.xml. FEDSum/FEDRadSum are cumulative since t0.
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
	FBRiskEgressLayerState UpperLayer;
	FBRiskEgressLayerState LowerLayer;
};

/** Compact timeline sample used to restore tenability state when playback is rewound. */
struct FAgentEgressHealthHistorySample
{
	float TimeSeconds = 0.0f;
	/** Display risk (max of enabled category risks) at this time; drives the bar fill. */
	float DisplayRisk = 0.0f;
	/** Shown criterion as uint8 (dominant before failure, first-failure after). */
	uint8 ShownCriterion = 0;
};

/**
 * Bridges parsed B-Risk time-series data into a compact per-room snapshot for
 * Mass processors. All B-Risk series are sampled once per room/time update.
 */
UCLASS()
class PROJECTMOBIUS_API UBRiskEgressSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Samples the current room state at an agent's breathing height. */
	bool SampleAgentEnvironment(
		const FVector& AgentFeetWorldLocation,
		float BreathingHeightCm,
		FAgentBRiskHazardSample& OutSample,
		int32 PreferredRoomIndex = INDEX_NONE) const;

	/** Returns the current state for a room containing WorldLocation. */
	const FBRiskEgressRoomState* FindRoomStateAtLocation(
		const FVector& WorldLocation,
		int32 PreferredRoomIndex = INDEX_NONE) const;

	/**
	 * Build tenability analysis settings from the loaded B-Risk input1.xml endpoints,
	 * applying documented fallbacks and warning on missing values. endpoint_radiation
	 * maps to the thermal-FED endpoint; endpoint_temp is deliberately NOT mapped to a
	 * Celsius threshold (it is not a layer temperature), so the temperature criterion
	 * is left disabled unless configured elsewhere.
	 */
	FTenabilityAnalysisSettings BuildTenabilitySettingsFromEndpoints() const;

	TConstArrayView<FBRiskEgressRoomState> GetRoomStates() const { return RoomStates; }
	uint64 GetRevision() const { return Revision; }
	uint64 GetScenarioGeneration() const { return ScenarioGeneration; }
	float GetSampleTimeSeconds() const { return SampleTimeSeconds; }

	/**
	 * Return the 1-based room/object id from a B-Risk series name's trailing
	 * numeric suffix (e.g. "ULOD_2" -> 2, "HGT_1" -> 1), or INDEX_NONE if the
	 * name has no numeric suffix. Public so tests can lock the mapping.
	 */
	static int32 ExtractRoomIdSuffix(const FString& SeriesName);

	/** Records a forward-playback health sample, compressing linear runs. */
	void RecordAgentHealth(
		int32 AgentId,
		float TimeSeconds,
		const FAgentEgressTenabilityFragment& Health);

	/** Restores health/dose at an already evaluated point on the playback timeline. */
	bool RestoreAgentHealth(
		int32 AgentId,
		float TimeSeconds,
		FAgentEgressTenabilityFragment& InOutHealth) const;

private:
	struct FRoomSeriesCache
	{
		TArray<FName> SeriesNames;
		TArray<FString> SeriesUnits;
		TArray<float> SeriesValues;
		TMap<FName, int32> SeriesLookup;
		// For each cached series, where it lives in the shared zone-table data.
		// B-Risk emits one zone table holding every room's columns suffixed "_<roomId>",
		// so a room's cache references rows of that single table rather than its own table.
		TArray<int32> SourceTableIndex;
		TArray<int32> SourceSeriesIndex;
	};

	UFUNCTION()
	void HandleScenarioLoaded(bool bSuccess);

	UFUNCTION()
	void HandleScenarioCleared();

	UFUNCTION()
	void HandleSimulationTimeChanged(float NewSimulationTime);

	void RebuildRoomCache();
	void RefreshAtTime(float NewSimulationTime);
	void ClearCachedData();
	void ResolveTypedRoomState(int32 RoomIndex);

	/** Interpolate B-Risk calculated tenability output (Track A) into a room state at SampleTimeSeconds. */
	void ApplyTenabilityCalcToRoom(int32 RoomIndex);

	/** Linear-interpolate a tenability room table at a time, clamping to the recorded range. */
	static FBRiskTenabilitySample SampleTenabilityTableAtTime(
		const FBRiskTenabilityRoomTable& Table,
		double TimeSeconds);

	static FName NormalizeSeriesName(const FString& SeriesName);
	static bool SampleAlignedSeries(
		const TArray<double>& Times,
		const TArray<double>& Values,
		double TimeSeconds,
		float& OutValue);

	bool TryGetRawSeries(
		int32 RoomIndex,
		std::initializer_list<const TCHAR*> Aliases,
		float& OutValue,
		const FString*& OutUnit) const;

	bool TryGetConcentrationPpm(
		int32 RoomIndex,
		std::initializer_list<const TCHAR*> Aliases,
		float MolecularWeight,
		float& OutPpm) const;

	bool TryGetConcentrationPercent(
		int32 RoomIndex,
		std::initializer_list<const TCHAR*> Aliases,
		float MolecularWeight,
		float& OutPercent) const;

	UPROPERTY()
	TObjectPtr<UBRiskDataSubsystem> BRiskDataSubsystem;

	UPROPERTY()
	TObjectPtr<UTimeDilationSubSystem> TimeDilationSubsystem;

	TArray<FBRiskEgressRoomState> RoomStates;
	TArray<FRoomSeriesCache> RoomSeriesCaches;
	TMap<int32, TArray<FAgentEgressHealthHistorySample>> AgentHealthHistory;
	float SampleTimeSeconds = 0.0f;
	// Revision increments on every state change (time scrub, reload, clear).
	// ScenarioGeneration increments only when a new scenario is fully loaded;
	// processors use it to detect when per-agent accumulated exposure must reset.
	uint64 Revision = 0;
	uint64 ScenarioGeneration = 0;
};
