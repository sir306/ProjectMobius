// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

#include "CoreMinimal.h"
#include "MassEntitySubsystem.h"
#include "BRiskDataImporter.h"
#include "BRisk/AgentTenabilityTimeline.h" // FAgentTimelineKey/Set/Timeline (Task 1 core)
#include "HAL/ThreadSafeBool.h"            // cancel flag shared with the background build job
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

	/**
	 * Linear-interpolate a tenability room table at a time, clamping to the
	 * recorded range. Public static so the offline timeline builder can back its
	 * FED sampler with the SAME curve evaluation the live room state uses
	 * (scientific-integrity invariant 4).
	 */
	static FBRiskTenabilitySample SampleTenabilityTableAtTime(
		const FBRiskTenabilityRoomTable& Table,
		double TimeSeconds);

	// --- Precomputed per-agent tenability timelines (Task 2: build orchestration + invalidation) ---
	// The timelines are a pure function of the (agent file, B-Risk file, settings) triple, identified by
	// FAgentTimelineKey. The health processor polls AreAgentTimelinesCurrent() every frame (three int
	// compares) and drives an async rebuild on mismatch; stale/building timelines are NEVER displayed
	// (FindCurrentAgentTimeline returns nullptr), so navigation and file swaps can never show old numbers.

	/**
	 * Current timeline key derived from the LIVE generations + settings:
	 *   AgentDataGeneration = FSimulationFragment::DataGeneration (bumped on every agent-file build),
	 *   ScenarioGeneration  = this->ScenarioGeneration (bumped on B-Risk load/clear),
	 *   SettingsHash        = HashTenabilitySettings(BuildTenabilitySettingsFromEndpoints()).
	 * A sentinel key (all zero) is returned when either dataset is absent (no provider / no scenario),
	 * so it never compares equal to a real built key (real agent generations start at 1).
	 */
	UE::Mobius::Tenability::FAgentTimelineKey MakeCurrentTimelineKey() const;

	/**
	 * True when the built timelines match the CURRENT (agent gen, scenario gen, settings) triple.
	 * False while stale, building, or when either dataset is absent — callers must render the no-data
	 * state in every false case.
	 */
	bool AreAgentTimelinesCurrent() const;

	/**
	 * Request an async rebuild for the given key. Debounced: a no-op if the key already matches the
	 * built set OR a build for an equal key is already in flight. Never blocks the game thread.
	 */
	void RequestAgentTimelineRebuild(const UE::Mobius::Tenability::FAgentTimelineKey& Key);

	/**
	 * Timeline for an agent, ONLY when the built set is current — nullptr when stale, building, or the
	 * agent has no timeline. Callers must render the no-data state on nullptr, never stale numbers.
	 */
	const UE::Mobius::Tenability::FAgentTenabilityTimeline* FindCurrentAgentTimeline(int32 EntityID) const;

	/**
	 * Live-side backing for FAgentTenabilityTimeline::DoseAt's FRoomFEDSampler. RoomIndex is the SAME
	 * index a timeline's intervals were built against (an index into RoomStates/GetRoomStates(), set by
	 * the build job from a parallel snapshot — see RequestAgentTimelineRebuild). Resolves RoomIndex ->
	 * RoomId -> the matching tenability table (the same RoomId-keyed lookup ApplyTenabilityCalcToRoom
	 * uses) and samples it via SampleTenabilityTableAtTime, so live dose queries and the offline build use
	 * the identical curve evaluation (scientific-integrity invariant 4). Out params are zeroed and the
	 * function is a no-op when the room index is out of range or has no matching table (never fabricated).
	 */
	void SampleTenabilityDoseAtRoomIndex(
		int32 RoomIndex,
		double TimeSeconds,
		double& OutToxicFED,
		double& OutThermalFED) const;

	/**
	 * Field-by-field hash of the analysis settings (NOT a struct memcpy — padding bytes are
	 * indeterminate). Public/static so tests can lock the mapping and the async apply can re-derive it.
	 */
	static uint32 HashTenabilitySettings(const FTenabilityAnalysisSettings& Settings);

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
	// Room geometry for containment, built PARALLEL to RoomStates in the same RebuildRoomCache loop
	// (an index resolved against this array is used to subscript RoomStates, so the two must never
	// diverge in length or order) and reset alongside it in ClearCachedData. Held rather than built
	// per call because FindRoomStateAtLocation runs once per agent per frame and the polygons would
	// otherwise be reallocated every time. Geometry does not vary with playback time.
	TArray<UE::Mobius::Tenability::FRoomVolume> RoomVolumes;
	TArray<FRoomSeriesCache> RoomSeriesCaches;
	float SampleTimeSeconds = 0.0f;
	// Revision increments on every state change (time scrub, reload, clear).
	// ScenarioGeneration increments only when a new scenario is fully loaded;
	// processors use it to detect when per-agent accumulated exposure must reset.
	uint64 Revision = 0;
	uint64 ScenarioGeneration = 0;

	// --- Agent-timeline build state (Task 2) ---
	// The currently-applied set (built on a worker thread, swapped in on the game thread once its
	// captured key still matches the live key). BuiltKey identifies which triple it was built for.
	UE::Mobius::Tenability::FAgentTimelineSet AgentTimelines;

	// The key of the build currently in flight, and a flag that a build is in flight, so
	// RequestAgentTimelineRebuild can debounce (one build per key at a time). Cleared on the
	// game-thread apply. Read/written only on the game thread.
	UE::Mobius::Tenability::FAgentTimelineKey InFlightKey;
	bool bAgentTimelineBuildInFlight = false;

	// Monotonic id stamped on each dispatched build; the game-thread apply compares it to the latest
	// to drop a superseded job even before the key re-derivation (a second RequestAgentTimelineRebuild
	// for a newer key while the first still runs). Never reset; game-thread only.
	uint32 AgentTimelineBuildSerial = 0;

	// Shared cancel flag handed to the in-flight build job (by TSharedRef so the job keeps it alive).
	// HandleScenarioCleared / a superseding request set it true; the worker checks it each timestep and
	// the apply discards a cancelled result. A fresh flag is minted per dispatch.
	TSharedPtr<FThreadSafeBool> AgentTimelineBuildCancel;

	/** Runs the completed build's result onto the game thread: re-derives MakeCurrentTimelineKey() and
	 *  keeps the set only if it still matches (and the job was not cancelled/superseded). */
	void ApplyAgentTimelineBuildResult(
		uint32 Serial,
		const TSharedRef<FThreadSafeBool>& Cancel,
		UE::Mobius::Tenability::FAgentTimelineSet&& BuiltSet);
};
