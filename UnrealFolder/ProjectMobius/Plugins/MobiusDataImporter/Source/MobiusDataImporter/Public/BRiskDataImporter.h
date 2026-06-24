// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

#include "CoreMinimal.h"

/**
 * Geometry descriptor for a single room parsed from a B-Risk SMV manifest.
 * Dimensions and origin are stored in the B-Risk coordinate space (metres).
 * The Label is populated from the companion LABEL block that follows each ROOM block.
 */
struct MOBIUSDATAIMPORTER_API FBRiskRoomGeometry
{
	/** 1-based room identifier as declared in the ROOM keyword block. */
	int32 RoomId = INDEX_NONE;

	/** Full extents of the room along each axis (X=width, Y=depth, Z=height) in metres. */
	FVector Size = FVector::ZeroVector;

	/** Lower-corner world origin of the room in metres. */
	FVector Origin = FVector::ZeroVector;

	/** Human-readable room name from the LABEL block immediately following this ROOM block. */
	FString Label;
};

/**
 * Describes the location of a fire source inside a specific room.
 * Parsed from the FIRE keyword block in the B-Risk SMV manifest.
 */
struct MOBIUSDATAIMPORTER_API FBRiskFireGeometry
{
	/** Room that contains this fire source (matches FBRiskRoomGeometry::RoomId). */
	int32 RoomId = INDEX_NONE;

	/** Position of the fire base within the room, in metres (B-Risk room-local space). */
	FVector Location = FVector::ZeroVector;
};

/** Sprinkler location and response metadata parsed from a B-Risk sprinklers.xml companion file. */
struct MOBIUSDATAIMPORTER_API FBRiskSprinklerGeometry
{
	/** Sprinkler identifier from sprinklers.xml. */
	int32 SprinklerId = INDEX_NONE;

	/** Room that contains this sprinkler. */
	int32 RoomId = INDEX_NONE;

	/** Sprinkler head position in room-local metres. Z is measured from the room floor. */
	FVector Location = FVector::ZeroVector;

	/** Activation/response time in seconds. Negative means no known activation time. */
	double ActivationTimeSeconds = -1.0;

	/** Nominal spray radius in metres. */
	double SprayRadius = 0.0;

	/** Nominal sprinkler density. */
	double SprayDensity = 0.0;

	/** Actuation temperature in Celsius. */
	double ActuationTemperatureC = 0.0;
};

/** Horizontal vent/opening geometry parsed from a B-Risk VENTGEOM block. */
struct MOBIUSDATAIMPORTER_API FBRiskVentGeometry
{
	/** Room containing this side of the opening. */
	int32 FromRoomId = INDEX_NONE;

	/** Connected room id. B-Risk uses external/outside room ids for exterior openings. */
	int32 ToRoomId = INDEX_NONE;

	/** B-Risk wall face id from VENTGEOM. */
	int32 Face = INDEX_NONE;

	/** Opening width along the wall, in metres. */
	double Width = 0.0;

	/** Opening offset along the wall, in metres. */
	double Offset = 0.0;

	/** Height of the bottom of the opening above the room floor, in metres. */
	double SillHeight = 0.0;

	/** Opening height, in metres. */
	double Height = 0.0;
};

/**
 * A single named time-series channel from a B-Risk zone CSV.
 * Each element of Values corresponds to the matching index in FBRiskZoneTable::TimeSeconds.
 * Example channels: ULT_1 (upper-layer temperature), HRR_1 (heat-release rate), HGT_1 (layer height).
 */
struct MOBIUSDATAIMPORTER_API FBRiskSeries
{
	/** Column header name as written in the CSV header row (e.g. "ULT_1", "HRR_1"). */
	FString Name;

	/** Physical unit string from the units row directly above the header (e.g. "C", "kW", "m"). */
	FString Unit;

	/** Sampled values aligned 1-to-1 with FBRiskZoneTable::TimeSeconds. */
	TArray<double> Values;
};

/**
 * All time-series data parsed from a single B-Risk zone CSV file.
 * Row 0 of the CSV holds unit strings; row 1 holds column names; rows 2+ are numeric samples.
 * The Time column is identified by name and stored separately in TimeSeconds.
 */
struct MOBIUSDATAIMPORTER_API FBRiskZoneTable
{
	/** Absolute path to the source CSV file on disk. */
	FString SourceCsvPath;

	/** Simulation time (seconds) for each sample row, in ascending order. */
	TArray<double> TimeSeconds;

	/** All non-Time series channels, in column order (excluding the Time column itself). */
	TArray<FBRiskSeries> Series;
};

/**
 * One room/time row of B-Risk's *calculated* tenability output (output1.xml).
 *
 * Unlike the raw zone CSV channels, these are values B-Risk itself computed at
 * the configured monitor height / egress path: FEDSum and FEDRadSum are
 * cumulative-since-t0 dose curves for the whole room; Visibility, layer height,
 * heat release and the two layer temperatures are instantaneous. Each bHas*
 * flag records whether the source tag was present so consumers never substitute
 * a fabricated value for a missing one.
 */
struct MOBIUSDATAIMPORTER_API FBRiskTenabilitySample
{
	/** Simulation time of this output sample, in seconds. */
	double SampleTimeSeconds = 0.0;

	/** Room heat release rate (kW) from <HeatRelease>. */
	double HeatReleaseKW = 0.0;

	/** Smoke layer interface height above the floor (m) from <layerheight>. */
	double LayerHeightM = 0.0;

	/** Upper-layer temperature (C) from <uppertemp>. */
	double UpperTemperatureC = 24.0;

	/** Lower-layer temperature (C) from <lowertemp>. */
	double LowerTemperatureC = 24.0;

	/** Visibility (m) from <Visibility>. */
	double VisibilityM = 20.0;

	/** Cumulative toxic FED (dimensionless) from <FEDSum>. */
	double FEDSum = 0.0;

	/** Cumulative thermal/radiant FED (dimensionless) from <FEDRadSum>. */
	double FEDRadSum = 0.0;

	bool bHasHeatRelease = false;
	bool bHasLayerHeight = false;
	bool bHasUpperTemperature = false;
	bool bHasLowerTemperature = false;
	bool bHasVisibility = false;
	bool bHasFEDSum = false;
	bool bHasFEDRadSum = false;
};

/**
 * All B-Risk calculated tenability output samples for a single room, parsed
 * from the <room id="..."> block of output1.xml. Samples are stored in
 * ascending time order, matching the <time> sub-blocks.
 */
struct MOBIUSDATAIMPORTER_API FBRiskTenabilityRoomTable
{
	/** Room identifier from <room id="...">, matching FBRiskRoomGeometry::RoomId. */
	int32 RoomId = INDEX_NONE;

	/** One entry per <time> block, ascending by SampleTimeSeconds. */
	TArray<FBRiskTenabilitySample> Samples;
};

/**
 * B-Risk analysis endpoints parsed from input1.xml. These are the tenability
 * limits B-Risk itself was configured with, used as defaults for the egress
 * tenability analysis. A bHas* flag is false when the tag was absent, so the
 * consumer logs a warning and falls back to a documented default rather than
 * silently trusting a zero.
 *
 * NOTE: EndpointTempRaw is the raw <endpoint_temp> value and is NOT a layer
 * temperature in Celsius (observed values are O(1000)); do not map it to a
 * Celsius criterion without resolving its semantics.
 */
struct MOBIUSDATAIMPORTER_API FBRiskTenabilityEndpoints
{
	double MonitorHeightM = 2.0;
	double EndpointVisibilityM = 10.0;
	double EndpointFED = 0.3;
	double EndpointRadiation = 0.3;
	double EndpointTempRaw = 0.0;

	bool bHasMonitorHeight = false;
	bool bHasEndpointVisibility = false;
	bool bHasEndpointFED = false;
	bool bHasEndpointRadiation = false;
	bool bHasEndpointTemp = false;
};

/**
 * Top-level container for everything parsed from a B-Risk scenario.
 * Created by FBRiskDataImporter::ImportScenarioFromSmv and passed to consumers.
 */
struct MOBIUSDATAIMPORTER_API FBRiskScenarioData
{
	/** Absolute path to the .smv manifest that was the import entry point. */
	FString SourceSmvPath;

	/** Absolute paths of every companion file (CSV etc.) resolved and loaded during import. */
	TArray<FString> ReferencedFiles;

	/** All rooms declared in ROOM blocks, in declaration order. */
	TArray<FBRiskRoomGeometry> Rooms;

	/** All fire sources declared in FIRE blocks, in declaration order. */
	TArray<FBRiskFireGeometry> Fires;

	/** All sprinklers declared in the companion sprinklers.xml, if present. */
	TArray<FBRiskSprinklerGeometry> Sprinklers;

	/** All horizontal vents/openings declared in VENTGEOM blocks, in declaration order. */
	TArray<FBRiskVentGeometry> Vents;

	/** One entry per zone CSV referenced by the ZONE block(s) in the SMV manifest. */
	TArray<FBRiskZoneTable> ZoneTables;

	/**
	 * B-Risk calculated tenability output (FEDSum/FEDRadSum/Visibility/...), one
	 * table per room, parsed from the companion output1.xml when present. Empty
	 * when the scenario has no output1.xml sibling.
	 */
	TArray<FBRiskTenabilityRoomTable> TenabilityTables;

	/** Analysis endpoints parsed from companion input1.xml. Defaults when absent. */
	FBRiskTenabilityEndpoints TenabilityEndpoints;
};

/**
 * Stateless facade for importing B-Risk fire-simulation scenario data.
 *
 * Usage:
 *   FBRiskScenarioData Data;
 *   FString Error;
 *   if (FBRiskDataImporter::ImportScenarioFromSmv(TEXT("path/to/scenario.smv"), Data, &Error))
 *   {
 *       // Data.Rooms, Data.Fires, Data.ZoneTables are populated.
 *   }
 */
class MOBIUSDATAIMPORTER_API FBRiskDataImporter
{
public:
	/**
	 * Parse a B-Risk SMV manifest and all companion files it references.
	 *
	 * The function:
	 *  1. Validates that SmvFilePath exists and has a .smv extension.
	 *  2. Reads and iterates keyword blocks (ZONE, ROOM, LABEL, FIRE).
	 *  3. Resolves each ZONE CSV path relative to the .smv directory and parses it.
	 *  4. Populates OutData with rooms, fire geometry, and zone time-series tables.
	 *
	 * @param SmvFilePath   Absolute (or engine-relative) path to the .smv manifest.
	 * @param OutData       Receives the fully parsed scenario on success.
	 * @param OutError      Optional; receives a human-readable error message on failure.
	 * @return true on success with OutData fully populated; false on any parse error.
	 */
	static bool ImportScenarioFromSmv(const FString& SmvFilePath, FBRiskScenarioData& OutData, FString* OutError = nullptr);
};
