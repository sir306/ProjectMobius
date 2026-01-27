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
 * Metadata read from an HDF5 simulation file
 */
struct FHdf5SimulationMetadata
{
	float Duration = 0.0f;
	float SamplingRate = 0.0f;
	int32 MaxNumEntities = 0;
	bool bIsSI = true;
	bool bIsDeg = true;
	
	// Comparison operator to see that two metadata structs are equal
	bool operator==(const FHdf5SimulationMetadata& Other) const
	{
		return FMath::IsNearlyEqual(Duration, Other.Duration) &&
		       FMath::IsNearlyEqual(SamplingRate, Other.SamplingRate) &&
		       MaxNumEntities == Other.MaxNumEntities &&
		       bIsSI == Other.bIsSI &&
		       bIsDeg == Other.bIsDeg;
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
 * Reader class for HDF5 simulation files created by the json_to_hdf5_converter.py script.
 * Uses the HDF5 C API for maximum portability.
 *
 * HDF5 File Schema:
 *   /metadata (Group with attributes: duration, sampling_rate, max_num_entities, is_si, is_deg)
 *   /entities (Dataset - compound type)
 *   /simulation/timesteps (Dataset - float array)
 *   /simulation/samples (Dataset - compound type)
 *   /simulation/samples_per_timestep (Dataset - int array)
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
	 * Read all sample data from the HDF5 file.
	 * @param OutSamples - Output array of sample data
	 * @return true if samples were read successfully
	 */
	bool ReadAllSamples(TArray<FHdf5SampleData>& OutSamples);

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

private:
	/** HDF5 file handle */
	hid_t FileId = -1;

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