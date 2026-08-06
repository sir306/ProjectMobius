// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BRiskDataImporter.h"
#include "BRisk/BRiskSmokeVisualState.h"
#include "BRisk/BRiskVentFlow.h"
#include "BRiskDataSubsystem.generated.h"

/**
 * Broadcast on the game thread when a B-Risk scenario load attempt finishes.
 * @param bSuccess  true if the scenario was fully parsed; false on any error.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBRiskScenarioLoaded, bool, bSuccess);

/** Broadcast on the game thread when the active B-Risk rooms have procedural geometry ready. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBRiskGeometryReady);

/** Broadcast when active B-Risk scenario data is discarded or replaced. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBRiskScenarioCleared);

class ABRiskHazardVisualizer;
class ABRiskSmokeVisualizer;
class UTimeDilationSubSystem;

/** World subsystem that owns the active B-Risk scenario. */
UCLASS(Config = Game, DefaultConfig)
class PROJECTMOBIUS_API UBRiskDataSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Binds to the game instance OnBRiskFileChanged delegate. */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Unbinds all delegates. */
	virtual void Deinitialize() override;

	/**
	 * Kick off an asynchronous load of the B-Risk scenario identified by SmvFilePath.
	 * Fires OnBRiskScenarioLoaded on the game thread when done.
	 * If another load is already running, only the newest request may commit data.
	 *
	 * @param SmvFilePath  Absolute path to the .smv manifest.
	 */
	UFUNCTION(BlueprintCallable, Category = "B-Risk")
	void LoadScenarioFromSmv(const FString& SmvFilePath);

	/**
	 * Discard the currently cached scenario data and reset to an empty state.
	 * Does not fire OnBRiskScenarioLoaded.
	 */
	UFUNCTION(BlueprintCallable, Category = "B-Risk")
	void ClearScenario();

	/** Returns true if the newest requested generation is still loading. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	bool IsLoading() const { return bIsLoading; }

	/** Returns true if scenario data is available and has at least one zone table. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	bool HasScenarioData() const;

	/** Last load error. Empty after a successful load or clear. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	FString GetLastError() const { return LastError; }

	/** Active .smv path for the committed scenario, or the newest pending request while loading. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	FString GetActiveSmvPath() const { return ActiveSmvPath; }

	/** Absolute paths referenced by the loaded .smv. Valid after a successful load. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	TArray<FString> GetReferencedFiles() const { return ScenarioData.ReferencedFiles; }

	/** Number of parsed ROOM entries in the loaded scenario. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	int32 GetRoomCount() const { return ScenarioData.Rooms.Num(); }

	/** Number of parsed FIRE entries in the loaded scenario. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	int32 GetFireCount() const { return ScenarioData.Fires.Num(); }

	/** Number of parsed VENTGEOM entries in the loaded scenario. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	int32 GetVentCount() const { return ScenarioData.Vents.Num(); }

	/** Number of parsed ZONE CSV tables in the loaded scenario. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	int32 GetZoneTableCount() const { return ScenarioData.ZoneTables.Num(); }

	/** Number of time samples in a parsed zone table. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	int32 GetZoneTimeSampleCount(int32 ZoneTableIndex = 0) const;

	/** Number of non-Time series in a parsed zone table. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	int32 GetZoneSeriesCount(int32 ZoneTableIndex = 0) const;

	/** Read one parsed room as Blueprint-friendly values. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	bool GetRoomGeometry(int32 RoomIndex, int32& RoomId, FVector& Origin, FVector& Size, FString& Label) const;

	/** Read one parsed fire source as Blueprint-friendly values. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	bool GetFireGeometry(int32 FireIndex, int32& RoomId, FVector& Location) const;

	/** Read one parsed vent/opening as Blueprint-friendly values. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	bool GetVentGeometry(
		int32 VentIndex,
		int32& FromRoomId,
		int32& ToRoomId,
		int32& Face,
		float& Width,
		float& Offset,
		float& SillHeight,
		float& Height) const;

	/** Return all non-Time series names for one zone table. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	TArray<FString> GetZoneSeriesNames(int32 ZoneTableIndex = 0) const;

	/** Return the first and last Time values for one zone table. */
	UFUNCTION(BlueprintPure, Category = "B-Risk")
	bool GetZoneTimeRange(int32 ZoneTableIndex, double& StartTimeSeconds, double& EndTimeSeconds) const;

	/** Sample a named series at a time value from Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk")
	bool SampleSeriesValue(int32 ZoneTableIndex, const FString& SeriesName, double TimeSeconds, double& OutValue) const;

	/** Enable or disable automatic room mesh generation after a successful .smv load. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Geometry")
	void SetAutoGenerateRoomGeometryOnLoad(bool bEnabled) { bAutoGenerateRoomGeometryOnLoad = bEnabled; }

	/** Returns true when successful .smv loads automatically rebuild B-Risk room geometry. */
	UFUNCTION(BlueprintPure, Category = "B-Risk|Geometry")
	bool GetAutoGenerateRoomGeometryOnLoad() const { return bAutoGenerateRoomGeometryOnLoad; }

	/** Allow B-Risk to own the shared play-bar duration and timestep. Disabled when agent playback owns the timeline. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Playback")
	void SetConfigureSharedPlaybackOnLoad(bool bEnabled) { bConfigureSharedPlaybackOnLoad = bEnabled; }

	UFUNCTION(BlueprintPure, Category = "B-Risk|Playback")
	bool GetConfigureSharedPlaybackOnLoad() const { return bConfigureSharedPlaybackOnLoad; }

	/** Set the scale used when converting B-Risk room metres into Unreal units. Default is 100 cm per metre. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Geometry")
	void SetRoomGeometryScale(float InScale);

	/** Scale used when converting B-Risk room metres into Unreal units. */
	UFUNCTION(BlueprintPure, Category = "B-Risk|Geometry")
	float GetRoomGeometryScale() const { return RoomGeometryScale; }

	/** Build procedural mesh arrays from the currently loaded ROOM data. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Geometry")
	bool BuildRoomGeometryMeshData(
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals) const;

	/** Build B-Risk room mesh data and send it to the first ARuntimeMeshBuilder in the world. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Geometry")
	bool GenerateAndLoadRoomGeometry();

	/**
	 * Tear down B-Risk room geometry from the shared RuntimeMeshBuilder. Only clears geometry
	 * B-Risk itself generated (gated on bRoomGeometryActive), so it never removes a separately
	 * imported building mesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Geometry")
	void ClearRoomGeometry();

	/**
	 * Live toggle for room geometry. Sets the load-time flag AND acts immediately when a scenario
	 * is already loaded: generates the room walls when enabled, tears them down when disabled.
	 */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Geometry")
	void SetRoomGeometryEnabled(bool bEnabled);

	/**
	 * Live toggle for B-Risk playback timing. Sets the flag AND re-evaluates the active timeline
	 * source immediately (without reloading), preserving the current play position and play/pause
	 * state. See ApplyActiveTimeline.
	 */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Playback")
	void SetUseBRiskTiming(bool bEnabled);

	/**
	 * Re-evaluate which data source owns the shared playback clock and reconfigure it live.
	 *
	 * Source priority: B-Risk when its timing is enabled AND it has zone-time data; otherwise the
	 * agent trajectory when present (detected via the spawn subsystem's cached duration); otherwise
	 * nothing (the clock is left untouched). Agent data is always parsed/loaded on its own grid
	 * regardless of which source owns the clock.
	 *
	 * @param bResetToStart  true on a fresh load (restart at t=0, paused); false for a live toggle
	 *                       (clamp the current position into the new range, preserve play/pause).
	 */
	void ApplyActiveTimeline(bool bResetToStart);

	/** Enable or disable automatic smoke volume generation after a successful .smv load. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Smoke")
	void SetAutoGenerateSmokeVolumesOnLoad(bool bEnabled) { bAutoGenerateSmokeVolumesOnLoad = bEnabled; }

	/** Returns true when successful .smv loads automatically rebuild B-Risk smoke volumes. */
	UFUNCTION(BlueprintPure, Category = "B-Risk|Smoke")
	bool GetAutoGenerateSmokeVolumesOnLoad() const { return bAutoGenerateSmokeVolumesOnLoad; }

	/** Spawn or rebuild room-sized B-Risk smoke volume actors from the active scenario. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Smoke")
	bool GenerateAndLoadSmokeVolumes();

	/** Update all generated smoke volumes for a simulation time in seconds. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Smoke")
	bool UpdateSmokeAtTime(float TimeSeconds);

	/** Destroy generated B-Risk smoke volumes and unbind playback updates. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Smoke")
	void ClearSmokeVolumes();

	/** Spawn or rebuild simple B-Risk fire and sprinkler indicators from the active scenario. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Hazards")
	bool GenerateAndLoadHazardVisuals();

	/** Update all generated B-Risk fire and sprinkler indicators for a simulation time in seconds. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Hazards")
	bool UpdateHazardVisualsAtTime(float TimeSeconds);

	/** Destroy generated B-Risk fire and sprinkler indicators. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Hazards")
	void ClearHazardVisuals();

	/** Log the active scenario. Toggle categories to inspect parsed data from Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Debug")
	void LogScenarioSummary(
		bool bLogReferencedFiles = true,
		bool bLogRooms = false,
		bool bLogFires = false,
		bool bLogVents = false,
		bool bLogZoneTables = true,
		bool bLogSeries = false,
		bool bLogSamples = false) const;

	/** Synchronously import and log a .smv without replacing the active async subsystem state. */
	UFUNCTION(BlueprintCallable, Category = "B-Risk|Debug")
	bool DebugImportScenarioFromSmv(
		const FString& SmvFilePath,
		bool bLogReferencedFiles = true,
		bool bLogRooms = false,
		bool bLogFires = false,
		bool bLogVents = false,
		bool bLogZoneTables = true,
		bool bLogSeries = false,
		bool bLogSamples = false);

	/** Fired on the game thread when LoadScenarioFromSmv completes. */
	UPROPERTY(BlueprintAssignable, Category = "B-Risk|Delegates")
	FOnBRiskScenarioLoaded OnBRiskScenarioLoaded;

	/** Fired after room geometry has been generated and submitted to the runtime mesh builder. */
	UPROPERTY(BlueprintAssignable, Category = "B-Risk|Delegates")
	FOnBRiskGeometryReady OnBRiskGeometryReady;

	/** Fired when the active scenario cache is cleared, including before replacement loads. */
	UPROPERTY(BlueprintAssignable, Category = "B-Risk|Delegates")
	FOnBRiskScenarioCleared OnBRiskScenarioCleared;

	/** Returns the full parsed scenario. Valid after OnBRiskScenarioLoaded fires with bSuccess = true. */
	const FBRiskScenarioData& GetScenarioData() const { return ScenarioData; }

	/** All rooms declared in the ROOM blocks of the loaded .smv. */
	const TArray<FBRiskRoomGeometry>& GetRooms() const { return ScenarioData.Rooms; }

	/**
	 * The coordinate frame every consumer of this scenario's geometry must convert through.
	 *
	 * Single source of truth for the whole pipeline: pass it to BRiskCoord::MakeRoomFootprint,
	 * BRiskCoord::WorldToUnreal, ABRiskSmokeVisualizer::ConfigureFromRooms and
	 * ABRiskHazardVisualizer::ConfigureFromScenario. Never re-derive it locally.
	 */
	BRiskCoord::ERoomFrame GetRoomFrame() const { return ScenarioData.RoomFrame; }

	/** All fire-source locations declared in the FIRE blocks of the loaded .smv. */
	const TArray<FBRiskFireGeometry>& GetFires() const { return ScenarioData.Fires; }

	/** All horizontal vents/openings declared in VENTGEOM blocks of the loaded .smv. */
	const TArray<FBRiskVentGeometry>& GetVents() const { return ScenarioData.Vents; }

	/** All parsed zone CSV tables. */
	const TArray<FBRiskZoneTable>& GetZoneTables() const { return ScenarioData.ZoneTables; }

	/** B-Risk calculated tenability output tables (FEDSum/FEDRadSum/Visibility/...), one per room. */
	const TArray<FBRiskTenabilityRoomTable>& GetTenabilityTables() const { return ScenarioData.TenabilityTables; }

	/** Analysis endpoints parsed from input1.xml (monitor height, endpoint_FED, ...). */
	const FBRiskTenabilityEndpoints& GetTenabilityEndpoints() const { return ScenarioData.TenabilityEndpoints; }

	/** True when the scenario shipped a parseable output1.xml with at least one room table. */
	bool HasTenabilityData() const { return ScenarioData.TenabilityTables.Num() > 0; }

	/**
	 * Find a named series inside a specific zone table.
	 *
	 * @param ZoneTableIndex  0-based index into GetZoneTables().
	 * @param SeriesName      Column name, e.g. "ULT_1", "HRR_1". Case-insensitive.
	 * @param OutSeries       Set to a pointer into the subsystem's data on success.
	 * @return true if the zone table exists and the series was found.
	 */
	bool GetSeriesByName(int32 ZoneTableIndex, const FString& SeriesName,
	                     const FBRiskSeries*& OutSeries) const;

	/**
	 * Sample a named time-series at an arbitrary simulation time using linear interpolation.
	 *
	 * Values outside the recorded time range are clamped to the nearest endpoint.
	 *
	 * @param ZoneTableIndex  0-based index into GetZoneTables().
	 * @param SeriesName      Column name, e.g. "ULT_1", "HGT_1". Case-insensitive.
	 * @param TimeSeconds     Simulation time to sample, in seconds.
	 * @param OutValue        Receives the interpolated value on success.
	 * @return true if the zone table and series were found and a value could be returned.
	 */
	bool SampleSeriesAtTime(int32 ZoneTableIndex, const FString& SeriesName,
	                        double TimeSeconds, double& OutValue) const;

	/**
	 * Build mesh arrays for a set of B-Risk rooms. Intended for C++ tests and non-Blueprint callers.
	 * Frame must be the scenario's RoomFrame (see GetRoomFrame).
	 *
	 * @param bCutLeakageOpenings  Whether leakage vents are cut as holes. Off by default: a leakage
	 *   vent models porosity, or the gap round a closed door leaf, so there is no aperture in the
	 *   building to cut. In the 12-room model 15 of the 18 sit inside a door's span anyway, and the
	 *   three that do not are the wall-leakage vents whose centre the add-in derives from a
	 *   different reference. Exposed so a test can turn them on and check the slits appear - the
	 *   wall decomposition unions overlapping openings, so this is safe to flip.
	 */
	static bool BuildRoomMeshDataFromRooms(
		const TArray<FBRiskRoomGeometry>& Rooms,
		const TArray<FBRiskVentGeometry>& Vents,
		float Scale,
		BRiskCoord::ERoomFrame Frame,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals,
		FString* OutError = nullptr,
		bool bCutLeakageOpenings = false);

	/** Convert a B-Risk smoke layer height and room height into the material's RoomSmoke scalar. */
	static float ComputeRoomSmokeScalar(double LayerHeight, double RoomHeight);

	/** Convert B-Risk CSV smoke channels into the runtime material visual state. */
	static FBRiskSmokeVisualState ComputeSmokeVisualState(
		double LayerHeight,
		double RoomHeight,
		double RoomOriginZMeters,
		float GeometryScaleCmPerMeter,
		double UpperOpticalDensity,
		double LowerOpticalDensity,
		double UpperTemperatureC,
		double LowerTemperatureC);

	/**
	 * Smokeview-style derived natural flow through one wall vent at a single time, computed
	 * from the two sides' two-layer hydrostatic pressure profiles (CCFM.VENTS/CFAST algorithm,
	 * SR282 §7.11.1). Qualitative fallback used because B-Risk does not export per-vent flow.
	 * Pure; pass pre-sampled side state + vent geometry.
	 */
	static FBRiskVentFlow ComputeWallVentFlow(
		const FBRiskVentSideState& From,
		const FBRiskVentSideState& To,
		const FBRiskVentGeometry& Vent);

private:
	UFUNCTION()
	void OnSmvFileChanged();

	UFUNCTION()
	void HandleNewSimulationTime(float NewCurrentTime);

	UFUNCTION()
	void HandleSimulationPauseChanged(bool bPaused);

	void ConfigurePlaybackFromScenario();
	UTimeDilationSubSystem* GetTimeDilationSubsystem() const;
	void UnbindSmokeTimeDelegate();

	/**
	 * Sample a per-index channel by building the B-Risk column name
	 * "<BaseName>_<OneBasedIndex>" (e.g. index 2 + "ULOD" -> "ULOD_2") and
	 * searching every loaded zone table. B-Risk packs all rooms/fires into a
	 * single zone CSV with one "_<n>" suffixed column per channel, so the
	 * 1-based id (room id for layer channels, fire-object number for fire
	 * channels) - not the array index into Rooms/Fires - selects the column.
	 */
	bool SampleRoomChannelAtTime(int32 OneBasedIndex, const TCHAR* BaseName, double TimeSeconds, double& OutValue) const;

	FBRiskScenarioData ScenarioData;
	FString LastError;
	FString ActiveSmvPath;

	/**
	 * When enabled, loading an SMV replaces the current RuntimeMeshBuilder mesh
	 * with the simple ROOM/VENT geometry exported by B-Risk. Leave disabled to
	 * preserve a separately loaded high-poly building mesh.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Config,
		Category = "B-Risk|Loading",
		meta = (AllowPrivateAccess = "true", DisplayName = "Use B-Risk Room Geometry On Load"))
	bool bAutoGenerateRoomGeometryOnLoad = false;

	bool bAutoGenerateSmokeVolumesOnLoad = true;
	bool bAutoGenerateHazardVisualsOnLoad = true;

	/**
	 * True while B-Risk-generated room geometry is present in the shared RuntimeMeshBuilder.
	 * Gates ClearRoomGeometry so we only ever tear down geometry B-Risk itself built, never a
	 * separately imported building mesh.
	 */
	bool bRoomGeometryActive = false;

	/**
	 * When enabled, the SMV Time column owns the shared simulation duration,
	 * timestep, current time and pause state. Leave disabled when agent movement
	 * data owns the common playback timeline.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Config,
		Category = "B-Risk|Loading",
		meta = (AllowPrivateAccess = "true", DisplayName = "Use B-Risk Playback Timing On Load"))
	bool bConfigureSharedPlaybackOnLoad = false;

	bool bHasWarnedMissingSmokeSeries = false;
	bool bHasWarnedMissingSmokeComponent = false;
	bool bHasWarnedMissingHazardSeries = false;
	float RoomGeometryScale = 100.0f;
	int32 LoadGeneration = 0;
	FThreadSafeBool bIsLoading;

	UPROPERTY()
	TObjectPtr<ABRiskSmokeVisualizer> SmokeVisualizerActor;

	UPROPERTY()
	TObjectPtr<ABRiskHazardVisualizer> HazardVisualizerActor;
};
