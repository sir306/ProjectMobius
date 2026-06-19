// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

#include "CoreMinimal.h"
#include "MassEntitySubsystem.h"
#include "MassAI/Fragments/AgentEgressHealthFragments.h"
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
	uint16 AvailableChannels = 0;
	FBRiskEgressLayerState UpperLayer;
	FBRiskEgressLayerState LowerLayer;
};

/** Compact timeline sample used to restore health when playback is rewound. */
struct FAgentEgressHealthHistorySample
{
	float TimeSeconds = 0.0f;
	float Health = 1.0f;
	float CombinedHazardDose = 0.0f;
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

	TConstArrayView<FBRiskEgressRoomState> GetRoomStates() const { return RoomStates; }
	uint64 GetRevision() const { return Revision; }
	uint64 GetScenarioGeneration() const { return ScenarioGeneration; }
	float GetSampleTimeSeconds() const { return SampleTimeSeconds; }

	/** Records a forward-playback health sample, compressing linear runs. */
	void RecordAgentHealth(
		int32 AgentId,
		float TimeSeconds,
		const FAgentEgressHealthFragment& Health);

	/** Restores health/dose at an already evaluated point on the playback timeline. */
	bool RestoreAgentHealth(
		int32 AgentId,
		float TimeSeconds,
		FAgentEgressHealthFragment& InOutHealth) const;

private:
	struct FRoomSeriesCache
	{
		TArray<FName> SeriesNames;
		TArray<FString> SeriesUnits;
		TArray<float> SeriesValues;
		TMap<FName, int32> SeriesLookup;
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
	uint64 Revision = 0;
	uint64 ScenarioGeneration = 0;
};
