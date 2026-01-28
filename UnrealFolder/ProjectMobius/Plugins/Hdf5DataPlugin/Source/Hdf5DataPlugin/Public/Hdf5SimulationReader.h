/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include "CoreMinimal.h"
#include "hdf5.h"

/**
 * Enum for detected HDF5 file format type
 */
enum class EHdf5FormatType : uint8
{
	Unknown,
	Mobius,    // Our format: /metadata, /entities, /simulation/*
	Juelich    // External format: /trajectory with root @wkt_geometry, @fps
};

/**
 * @brief Metadata read from an HDF5 simulation file.
 *
 * Contains simulation-wide parameters extracted from either Mobius or Juelich format HDF5 files.
 * Also includes flags indicating whether certain data fields (rotation, speed) are present
 * in the source file or need to be calculated from position data.
 */
struct FHdf5SimulationMetadata
{
	/** @brief Total duration of the simulation in seconds */
	float Duration = 0.0f;

	/** @brief Time interval between samples in seconds (e.g., 0.1 for 10 Hz sampling) */
	float SamplingRate = 0.0f;

	/** @brief Maximum number of entities that appear at any single timestep */
	int32 MaxNumEntities = 0;

	/** @brief True if position values are in SI units (meters), false otherwise */
	bool bIsSI = true;

	/** @brief True if rotation values are in degrees, false if radians */
	bool bIsDeg = true;

	/**
	 * @brief Indicates whether rotation data is present in the source HDF5 file.
	 *
	 * When false, rotation must be calculated from movement direction using
	 * FProcessSimulationDataRunnable::CalculateRotationFromMovement().
	 * This is typically the case for Juelich format files which only contain position data.
	 */
	bool bHasRotationData = true;

	/**
	 * @brief Indicates whether speed data is present in the source HDF5 file.
	 *
	 * When false, speed must be calculated from position deltas using
	 * FProcessSimulationDataRunnable::CalculateSpeedFromMovement().
	 * This is typically the case for Juelich format files which only contain position data.
	 */
	bool bHasSpeedData = true;

	/**
	 * @brief Equality comparison operator for metadata structs.
	 * @param Other The metadata to compare against
	 * @return True if all fields are equal (using near-equality for floats)
	 */
	bool operator==(const FHdf5SimulationMetadata& Other) const
	{
		return FMath::IsNearlyEqual(Duration, Other.Duration) &&
		       FMath::IsNearlyEqual(SamplingRate, Other.SamplingRate) &&
		       MaxNumEntities == Other.MaxNumEntities &&
		       bIsSI == Other.bIsSI &&
		       bIsDeg == Other.bIsDeg &&
		       bHasRotationData == Other.bHasRotationData &&
		       bHasSpeedData == Other.bHasSpeedData;
	}
};

/**
 * Entity data read from an HDF5 simulation file
 */
struct FHdf5EntityData
{
	int32 Id = 0;
	FString Name;
	float SimTimeS = 0.0f;
	float MaxSpeed = 0.0f;
	FString MPlane;
	int32 Map = 0;
};

/**
 * Sample data for a single entity at a single timestep
 */
struct FHdf5SampleData
{
	int32 TimestepIndex = 0;
	int32 EntityId = 0;
	float PositionX = 0.0f;
	float PositionY = 0.0f;
	float PositionZ = 0.0f;
	float Rotation = 0.0f;
	float Speed = 0.0f;
	FString Mode;
};

/**
 * Raw trajectory record from Juelich HDF5 format
 */
struct FHdf5JuelichTrajectoryRecord
{
	int64 Id = 0;
	int64 Frame = 0;
	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;
};

/**
 * @brief Metadata extracted from Juelich HDF5 format root attributes.
 *
 * This struct captures the metadata attributes stored at the root level of Juelich-format
 * HDF5 trajectory files. It also includes detection flags for optional data fields that
 * may or may not be present in the /trajectory dataset's compound type.
 *
 * @see FHdf5SimulationReader::ReadJuelichMetadata() for how this data is populated
 * @see FHdf5SimulationReader::ConvertJuelichToMobiusFormat() for conversion to unified format
 */
struct FHdf5JuelichMetadata
{
	/** @brief Well-Known Text (WKT) geometry string defining the simulation boundary/area */
	FString WktGeometry;

	/** @brief Frame rate of the trajectory recording in frames per second (default: 25.0 Hz) */
	float Fps = 25.0f;

	/** @brief Human-readable name/identifier for this recording run */
	FString RunName;

	/** @brief Total number of unique participants/entities in the trajectory data */
	int32 NumParticipants = 0;

	/**
	 * @brief Indicates whether the /trajectory compound type contains a 'rotation' field.
	 *
	 * Detected at file load time using H5Tget_member_index(). When false, rotation values
	 * must be calculated from movement direction after data loading.
	 * @see FProcessSimulationDataRunnable::CalculateRotationFromMovement()
	 */
	bool bHasRotationField = false;

	/**
	 * @brief Indicates whether the /trajectory compound type contains a 'speed' field.
	 *
	 * Detected at file load time using H5Tget_member_index(). When false, speed values
	 * must be calculated from position deltas after data loading.
	 * @see FProcessSimulationDataRunnable::CalculateSpeedFromMovement()
	 */
	bool bHasSpeedField = false;
};

/**
 * Reader class for HDF5 simulation files. Supports multiple formats:
 *
 * Mobius Format (our internal format):
 *   /metadata (Group with attributes: duration, sampling_rate, max_num_entities, is_si, is_deg)
 *   /entities (Dataset - compound type)
 *   /simulation/timesteps (Dataset - float array)
 *   /simulation/samples (Dataset - compound type)
 *   /simulation/samples_per_timestep (Dataset - int array)
 *
 * Juelich Format (external trajectory format):
 *   Root attributes: wkt_geometry, fps, run_name, metadata
 *   /trajectory (Dataset - compound type: id, frame, x, y, z)
 *
 * Uses the HDF5 C API for maximum portability.
 */
class HDF5DATAPLUGIN_API FHdf5SimulationReader
{
public:
	FHdf5SimulationReader();
	~FHdf5SimulationReader();

	// Non-copyable
	FHdf5SimulationReader(const FHdf5SimulationReader&) = delete;
	FHdf5SimulationReader& operator=(const FHdf5SimulationReader&) = delete;

	/**
	 * Open an HDF5 file for reading.
	 * @param FilePath - Path to the HDF5 file
	 * @return true if file was opened successfully
	 */
	bool OpenFile(const FString& FilePath);

	/**
	 * Close the currently open file.
	 */
	void CloseFile();

	/**
	 * Check if a file is currently open.
	 */
	bool IsOpen() const { return FileId >= 0; }

	/**
	 * Read metadata from the HDF5 file.
	 * @param OutMetadata - Output metadata structure
	 * @return true if metadata was read successfully
	 */
	bool ReadMetadata(FHdf5SimulationMetadata& OutMetadata);

	/**
	 * Read all entity data from the HDF5 file.
	 * @param OutEntities - Output array of entity data
	 * @return true if entities were read successfully
	 */
	bool ReadEntities(TArray<FHdf5EntityData>& OutEntities);

	/**
	 * Read all timestep values.
	 * @param OutTimesteps - Output array of timestep values
	 * @return true if timesteps were read successfully
	 */
	bool ReadTimesteps(TArray<float>& OutTimesteps);

	/**
	 * Read the number of samples per timestep (for efficient iteration).
	 * @param OutSamplesPerTimestep - Output array
	 * @return true if read successfully
	 */
	bool ReadSamplesPerTimestep(TArray<int32>& OutSamplesPerTimestep);

	/**
	 * @brief Read all sample data from the HDF5 file.
	 *
	 * Reads the complete samples dataset from either Mobius (/simulation/samples) or
	 * Juelich (/trajectory) format files. For each sample, extracts position data and
	 * optionally rotation/speed if those fields exist in the compound type.
	 *
	 * The method dynamically detects the presence of 'rotation' and 'speed' fields in
	 * the HDF5 compound type using H5Tget_member_index(). If a field is not present,
	 * the corresponding values in FHdf5SampleData will be set to 0.0f and the caller
	 * should calculate these values from position data post-load.
	 *
	 * @param OutSamples Output array populated with sample data for all entities across all timesteps
	 * @param OutHasRotationField Optional pointer to receive whether 'rotation' field exists in the dataset.
	 *        When non-null, set to true if rotation data was read from file, false if it needs calculation.
	 * @param OutHasSpeedField Optional pointer to receive whether 'speed' field exists in the dataset.
	 *        When non-null, set to true if speed data was read from file, false if it needs calculation.
	 * @return true if samples were read successfully, false on HDF5 read error
	 *
	 * @see FProcessSimulationDataRunnable::CalculateRotationFromMovement() for rotation calculation
	 * @see FProcessSimulationDataRunnable::CalculateSpeedFromMovement() for speed calculation
	 */
	bool ReadAllSamples(TArray<FHdf5SampleData>& OutSamples, bool* OutHasRotationField = nullptr, bool* OutHasSpeedField = nullptr);

	/**
	 * Read samples for a specific timestep range.
	 * Requires ReadSamplesPerTimestep to be called first to compute offsets.
	 * @param StartTimestep - First timestep index
	 * @param EndTimestep - Last timestep index (inclusive)
	 * @param OutSamples - Output array of sample data
	 * @return true if samples were read successfully
	 */
	bool ReadSamplesForTimestepRange(int32 StartTimestep, int32 EndTimestep, TArray<FHdf5SampleData>& OutSamples);

	/**
	 * Get the total number of timesteps in the file.
	 * File must be open.
	 */
	int32 GetTimestepCount() const { return TimestepCount; }

	/**
	 * Get the total number of samples in the file.
	 * File must be open.
	 */
	int32 GetTotalSampleCount() const { return TotalSampleCount; }

	/**
	 * Get the number of entities in the file.
	 * File must be open.
	 */
	int32 GetEntityCount() const { return EntityCount; }

	/**
	 * Check if a file appears to be a valid simulation HDF5 file.
	 * Does not require OpenFile to be called first.
	 * @param FilePath - Path to check
	 * @return true if file appears to be a valid simulation file
	 */
	static bool IsValidSimulationFile(const FString& FilePath);

	// ========== Format Detection ==========

	/**
	 * Detect the format type of an HDF5 file without keeping it open.
	 * @param FilePath - Path to the HDF5 file
	 * @return The detected format type
	 */
	static EHdf5FormatType DetectFormat(const FString& FilePath);

	/**
	 * Get the detected format of the currently open file.
	 * @return The format type detected when the file was opened
	 */
	EHdf5FormatType GetDetectedFormat() const { return DetectedFormat; }

	// ========== Juelich Format Reading ==========

	/**
	 * Read metadata from a Juelich format HDF5 file.
	 * Only valid if GetDetectedFormat() returns Juelich.
	 * @param OutMetadata - Output metadata structure
	 * @return true if metadata was read successfully
	 */
	bool ReadJuelichMetadata(FHdf5JuelichMetadata& OutMetadata);

	/**
	 * Read all trajectory records from a Juelich format HDF5 file.
	 * Only valid if GetDetectedFormat() returns Juelich.
	 * @param OutRecords - Output array of trajectory records
	 * @return true if trajectories were read successfully
	 */
	bool ReadJuelichTrajectories(TArray<FHdf5JuelichTrajectoryRecord>& OutRecords);

	/**
	 * Read WKT geometry string from an HDF5 file's root attributes.
	 * Can be called on any format that has wkt_geometry attribute.
	 * @param OutWktGeometry - Output WKT string
	 * @return true if WKT geometry was found and read
	 */
	bool ReadWktGeometry(FString& OutWktGeometry);

	/**
	 * Convert Juelich format data to Mobius format for unified downstream processing.
	 * @param JuelichMeta - Juelich metadata (for fps)
	 * @param Trajectories - Raw trajectory records
	 * @param OutMetadata - Output Mobius metadata
	 * @param OutEntities - Output entity array (created from unique IDs)
	 * @param OutSamples - Output sample array (converted from trajectories)
	 * @return true if conversion was successful
	 */
	static bool ConvertJuelichToMobiusFormat(
		const FHdf5JuelichMetadata& JuelichMeta,
		const TArray<FHdf5JuelichTrajectoryRecord>& Trajectories,
		FHdf5SimulationMetadata& OutMetadata,
		TArray<FHdf5EntityData>& OutEntities,
		TArray<FHdf5SampleData>& OutSamples
	);

private:
	/** HDF5 file handle */
	hid_t FileId = -1;

	/** Detected format type (populated on file open) */
	EHdf5FormatType DetectedFormat = EHdf5FormatType::Unknown;

	/** Cached counts (populated on file open) */
	int32 TimestepCount = 0;
	int32 TotalSampleCount = 0;
	int32 EntityCount = 0;

	/** Cached sample offsets for efficient random access */
	TArray<int32> SampleOffsets;

	/** Helper to read a scalar attribute from a group */
	template<typename T>
	bool ReadAttribute(hid_t GroupId, const char* AttrName, hid_t DataType, T& OutValue);

	/** Helper to read a string attribute */
	bool ReadStringAttribute(hid_t GroupId, const char* AttrName, FString& OutValue);

	/** Populate cached counts from the file */
	bool PopulateCachedCounts();
};