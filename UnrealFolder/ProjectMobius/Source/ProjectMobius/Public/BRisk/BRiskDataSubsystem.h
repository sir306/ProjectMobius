// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BRiskDataImporter.h"
#include "BRisk/BRiskSmokeVisualState.h"
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

	/** All fire-source locations declared in the FIRE blocks of the loaded .smv. */
	const TArray<FBRiskFireGeometry>& GetFires() const { return ScenarioData.Fires; }

	/** All horizontal vents/openings declared in VENTGEOM blocks of the loaded .smv. */
	const TArray<FBRiskVentGeometry>& GetVents() const { return ScenarioData.Vents; }

	/** All parsed zone CSV tables. */
	const TArray<FBRiskZoneTable>& GetZoneTables() const { return ScenarioData.ZoneTables; }

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

	/** Build mesh arrays for a set of B-Risk rooms. Intended for C++ tests and non-Blueprint callers. */
	static bool BuildRoomMeshDataFromRooms(
		const TArray<FBRiskRoomGeometry>& Rooms,
		const TArray<FBRiskVentGeometry>& Vents,
		float Scale,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals,
		FString* OutError = nullptr);

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
