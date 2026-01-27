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

#include "Hdf5SimulationReader.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogHdf5SimulationReader, Log, All);

FHdf5SimulationReader::FHdf5SimulationReader()
{
}

FHdf5SimulationReader::~FHdf5SimulationReader()
{
	CloseFile();
}

bool FHdf5SimulationReader::OpenFile(const FString& FilePath)
{
	// Close any previously open file
	CloseFile();

	// Check file exists
	if (!FPaths::FileExists(FilePath))
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("HDF5 file not found: %s"), *FilePath);
		return false;
	}

	// Convert to UTF8 for HDF5
	FTCHARToUTF8 FilePathUtf8(*FilePath);

	// Initialize HDF5 library
	H5open();

	// Open the file
	FileId = H5Fopen(FilePathUtf8.Get(), H5F_ACC_RDONLY, H5P_DEFAULT);
	if (FileId < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("Failed to open HDF5 file: %s"), *FilePath);
		H5close();
		return false;
	}

	UE_LOG(LogHdf5SimulationReader, Log, TEXT("Opened HDF5 file: %s"), *FilePath);

	// Populate cached counts
	if (!PopulateCachedCounts())
	{
		UE_LOG(LogHdf5SimulationReader, Warning, TEXT("Could not read file structure counts"));
	}

	return true;
}

void FHdf5SimulationReader::CloseFile()
{
	if (FileId >= 0)
	{
		H5Fclose(FileId);
		H5close();
		FileId = -1;
		TimestepCount = 0;
		TotalSampleCount = 0;
		EntityCount = 0;
		SampleOffsets.Empty();
	}
}

bool FHdf5SimulationReader::PopulateCachedCounts()
{
	if (FileId < 0)
	{
		return false;
	}

	// Get timestep count
	hid_t TimestepsDataset = H5Dopen(FileId, "/simulation/timesteps", H5P_DEFAULT);
	if (TimestepsDataset >= 0)
	{
		hid_t Space = H5Dget_space(TimestepsDataset);
		if (Space >= 0)
		{
			hsize_t Dims[1];
			H5Sget_simple_extent_dims(Space, Dims, nullptr);
			TimestepCount = static_cast<int32>(Dims[0]);
			H5Sclose(Space);
		}
		H5Dclose(TimestepsDataset);
	}

	// Get sample count
	hid_t SamplesDataset = H5Dopen(FileId, "/simulation/samples", H5P_DEFAULT);
	if (SamplesDataset >= 0)
	{
		hid_t Space = H5Dget_space(SamplesDataset);
		if (Space >= 0)
		{
			hsize_t Dims[1];
			H5Sget_simple_extent_dims(Space, Dims, nullptr);
			TotalSampleCount = static_cast<int32>(Dims[0]);
			H5Sclose(Space);
		}
		H5Dclose(SamplesDataset);
	}

	// Get entity count
	hid_t EntitiesDataset = H5Dopen(FileId, "/entities", H5P_DEFAULT);
	if (EntitiesDataset >= 0)
	{
		hid_t Space = H5Dget_space(EntitiesDataset);
		if (Space >= 0)
		{
			hsize_t Dims[1];
			H5Sget_simple_extent_dims(Space, Dims, nullptr);
			EntityCount = static_cast<int32>(Dims[0]);
			H5Sclose(Space);
		}
		H5Dclose(EntitiesDataset);
	}

	UE_LOG(LogHdf5SimulationReader, Log, TEXT("HDF5 file contains: %d timesteps, %d samples, %d entities"),
		TimestepCount, TotalSampleCount, EntityCount);

	return true;
}

template<typename T>
bool FHdf5SimulationReader::ReadAttribute(hid_t GroupId, const char* AttrName, hid_t DataType, T& OutValue)
{
	if (!H5Aexists(GroupId, AttrName))
	{
		return false;
	}

	hid_t AttrId = H5Aopen(GroupId, AttrName, H5P_DEFAULT);
	if (AttrId < 0)
	{
		return false;
	}

	herr_t Status = H5Aread(AttrId, DataType, &OutValue);
	H5Aclose(AttrId);

	return Status >= 0;
}

bool FHdf5SimulationReader::ReadMetadata(FHdf5SimulationMetadata& OutMetadata)
{
	if (FileId < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("No file is open"));
		return false;
	}

	// Open metadata group
	hid_t MetaGroup = H5Gopen(FileId, "/metadata", H5P_DEFAULT);
	if (MetaGroup < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("Could not open /metadata group"));
		return false;
	}

	// Read attributes
	double Duration = 0.0;
	double SamplingRate = 0.1;
	int64_t MaxNumEntities = 0;
	hbool_t IsSI = 1;
	hbool_t IsDeg = 1;

	ReadAttribute(MetaGroup, "duration", H5T_NATIVE_DOUBLE, Duration);
	ReadAttribute(MetaGroup, "sampling_rate", H5T_NATIVE_DOUBLE, SamplingRate);
	ReadAttribute(MetaGroup, "max_num_entities", H5T_NATIVE_INT64, MaxNumEntities);
	ReadAttribute(MetaGroup, "is_si", H5T_NATIVE_HBOOL, IsSI);
	ReadAttribute(MetaGroup, "is_deg", H5T_NATIVE_HBOOL, IsDeg);

	H5Gclose(MetaGroup);

	OutMetadata.Duration = static_cast<float>(Duration);
	OutMetadata.SamplingRate = static_cast<float>(SamplingRate);
	OutMetadata.MaxNumEntities = static_cast<int32>(MaxNumEntities);
	OutMetadata.bIsSI = IsSI != 0;
	OutMetadata.bIsDeg = IsDeg != 0;

	UE_LOG(LogHdf5SimulationReader, Log, TEXT("Read metadata: duration=%.2f, sampling_rate=%.2f, max_entities=%d, SI=%d, Deg=%d"),
		OutMetadata.Duration, OutMetadata.SamplingRate, OutMetadata.MaxNumEntities,
		OutMetadata.bIsSI ? 1 : 0, OutMetadata.bIsDeg ? 1 : 0);

	return true;
}

bool FHdf5SimulationReader::ReadEntities(TArray<FHdf5EntityData>& OutEntities)
{
	if (FileId < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("No file is open"));
		return false;
	}

	// Open entities dataset
	hid_t Dataset = H5Dopen(FileId, "/entities", H5P_DEFAULT);
	if (Dataset < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("Could not open /entities dataset"));
		return false;
	}

	// Get dataspace and dimensions
	hid_t Space = H5Dget_space(Dataset);
	hsize_t Dims[1];
	H5Sget_simple_extent_dims(Space, Dims, nullptr);
	int32 NumEntities = static_cast<int32>(Dims[0]);

	// Get the file's compound type to determine string sizes
	hid_t FileType = H5Dget_type(Dataset);

	// Get the string sizes from the file type
	// Fields: id(0), name(1), sim_time_s(2), max_speed(3), m_plane(4), map(5)
	hid_t NameType = H5Tget_member_type(FileType, 1);
	hid_t MPlaneType = H5Tget_member_type(FileType, 4);

	size_t NameSize = H5Tget_size(NameType);
	size_t MPlaneSize = H5Tget_size(MPlaneType);

	H5Tclose(NameType);
	H5Tclose(MPlaneType);

	UE_LOG(LogHdf5SimulationReader, Log, TEXT("Entity string sizes: name=%zu, m_plane=%zu"), NameSize, MPlaneSize);

	// Allocate a raw buffer that matches the file's compound layout
	// We'll read the entire dataset using the file's native type, then extract fields
	size_t FileTypeSize = H5Tget_size(FileType);

	TArray<uint8> RawBuffer;
	RawBuffer.SetNum(NumEntities * FileTypeSize);

	herr_t Status = H5Dread(Dataset, FileType, H5S_ALL, H5S_ALL, H5P_DEFAULT, RawBuffer.GetData());

	if (Status < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("H5Dread failed for entities (status=%d)"), Status);
		H5Ewalk2(H5E_DEFAULT, H5E_WALK_DOWNWARD, [](unsigned n, const H5E_error2_t* err, void* data) -> herr_t {
			UE_LOG(LogHdf5SimulationReader, Error, TEXT("  HDF5 Error: %hs - %hs"), err->desc, err->func_name);
			return 0;
		}, nullptr);
	}
	else
	{
		// Get field offsets from the file type
		size_t OffsetId = H5Tget_member_offset(FileType, 0);
		size_t OffsetName = H5Tget_member_offset(FileType, 1);
		size_t OffsetSimTimeS = H5Tget_member_offset(FileType, 2);
		size_t OffsetMaxSpeed = H5Tget_member_offset(FileType, 3);
		size_t OffsetMPlane = H5Tget_member_offset(FileType, 4);
		size_t OffsetMap = H5Tget_member_offset(FileType, 5);

		// Convert to output format
		OutEntities.SetNum(NumEntities);
		for (int32 i = 0; i < NumEntities; ++i)
		{
			uint8* RecordPtr = RawBuffer.GetData() + i * FileTypeSize;

			OutEntities[i].Id = *reinterpret_cast<int32_t*>(RecordPtr + OffsetId);

			// Fixed-size strings are null-terminated or padded
			const char* NamePtr = reinterpret_cast<const char*>(RecordPtr + OffsetName);
			OutEntities[i].Name = FString(UTF8_TO_TCHAR(NamePtr));

			OutEntities[i].SimTimeS = *reinterpret_cast<float*>(RecordPtr + OffsetSimTimeS);
			OutEntities[i].MaxSpeed = *reinterpret_cast<float*>(RecordPtr + OffsetMaxSpeed);

			const char* MPlanePtr = reinterpret_cast<const char*>(RecordPtr + OffsetMPlane);
			OutEntities[i].MPlane = FString(UTF8_TO_TCHAR(MPlanePtr));

			OutEntities[i].Map = *reinterpret_cast<int32_t*>(RecordPtr + OffsetMap);
		}

		UE_LOG(LogHdf5SimulationReader, Log, TEXT("Read %d entities"), NumEntities);
	}

	// Cleanup
	H5Tclose(FileType);
	H5Sclose(Space);
	H5Dclose(Dataset);

	return Status >= 0;
}

bool FHdf5SimulationReader::ReadTimesteps(TArray<float>& OutTimesteps)
{
	if (FileId < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("No file is open"));
		return false;
	}

	hid_t Dataset = H5Dopen(FileId, "/simulation/timesteps", H5P_DEFAULT);
	if (Dataset < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("Could not open /simulation/timesteps dataset"));
		return false;
	}

	hid_t Space = H5Dget_space(Dataset);
	hsize_t Dims[1];
	H5Sget_simple_extent_dims(Space, Dims, nullptr);
	int32 Count = static_cast<int32>(Dims[0]);

	OutTimesteps.SetNum(Count);
	herr_t Status = H5Dread(Dataset, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, OutTimesteps.GetData());

	H5Sclose(Space);
	H5Dclose(Dataset);

	if (Status < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("Failed to read timesteps dataset"));
		return false;
	}

	UE_LOG(LogHdf5SimulationReader, Log, TEXT("Read %d timesteps"), Count);
	return true;
}

bool FHdf5SimulationReader::ReadSamplesPerTimestep(TArray<int32>& OutSamplesPerTimestep)
{
	if (FileId < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("No file is open"));
		return false;
	}

	hid_t Dataset = H5Dopen(FileId, "/simulation/samples_per_timestep", H5P_DEFAULT);
	if (Dataset < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("Could not open /simulation/samples_per_timestep dataset"));
		return false;
	}

	hid_t Space = H5Dget_space(Dataset);
	hsize_t Dims[1];
	H5Sget_simple_extent_dims(Space, Dims, nullptr);
	int32 Count = static_cast<int32>(Dims[0]);

	OutSamplesPerTimestep.SetNum(Count);
	herr_t Status = H5Dread(Dataset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, OutSamplesPerTimestep.GetData());

	H5Sclose(Space);
	H5Dclose(Dataset);

	if (Status < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("Failed to read samples_per_timestep dataset"));
		return false;
	}

	// Build sample offsets for efficient random access
	SampleOffsets.SetNum(Count + 1);
	SampleOffsets[0] = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		SampleOffsets[i + 1] = SampleOffsets[i] + OutSamplesPerTimestep[i];
	}

	return true;
}

bool FHdf5SimulationReader::ReadAllSamples(TArray<FHdf5SampleData>& OutSamples)
{
	if (FileId < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("No file is open"));
		return false;
	}

	hid_t Dataset = H5Dopen(FileId, "/simulation/samples", H5P_DEFAULT);
	if (Dataset < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("Could not open /simulation/samples dataset"));
		return false;
	}

	hid_t Space = H5Dget_space(Dataset);
	hsize_t Dims[1];
	H5Sget_simple_extent_dims(Space, Dims, nullptr);
	int32 NumSamples = static_cast<int32>(Dims[0]);

	// Get the file's compound type
	hid_t FileType = H5Dget_type(Dataset);
	size_t FileTypeSize = H5Tget_size(FileType);

	// Read using file's native type
	TArray<uint8> RawBuffer;
	RawBuffer.SetNum(NumSamples * FileTypeSize);

	herr_t Status = H5Dread(Dataset, FileType, H5S_ALL, H5S_ALL, H5P_DEFAULT, RawBuffer.GetData());

	if (Status < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("Failed to read samples dataset"));
		H5Ewalk2(H5E_DEFAULT, H5E_WALK_DOWNWARD, [](unsigned n, const H5E_error2_t* err, void* data) -> herr_t {
			UE_LOG(LogHdf5SimulationReader, Error, TEXT("  HDF5 Error: %hs - %hs"), err->desc, err->func_name);
			return 0;
		}, nullptr);
	}
	else
	{
		// Get field offsets - fields: timestep_idx(0), entity_id(1), position_x(2), position_y(3), position_z(4), rotation(5), speed(6), mode(7)
		size_t OffsetTimestepIdx = H5Tget_member_offset(FileType, 0);
		size_t OffsetEntityId = H5Tget_member_offset(FileType, 1);
		size_t OffsetPosX = H5Tget_member_offset(FileType, 2);
		size_t OffsetPosY = H5Tget_member_offset(FileType, 3);
		size_t OffsetPosZ = H5Tget_member_offset(FileType, 4);
		size_t OffsetRotation = H5Tget_member_offset(FileType, 5);
		size_t OffsetSpeed = H5Tget_member_offset(FileType, 6);
		size_t OffsetMode = H5Tget_member_offset(FileType, 7);

		// Convert to output format
		OutSamples.SetNum(NumSamples);
		for (int32 i = 0; i < NumSamples; ++i)
		{
			uint8* RecordPtr = RawBuffer.GetData() + i * FileTypeSize;

			OutSamples[i].TimestepIndex = *reinterpret_cast<int32_t*>(RecordPtr + OffsetTimestepIdx);
			OutSamples[i].EntityId = *reinterpret_cast<int32_t*>(RecordPtr + OffsetEntityId);
			OutSamples[i].PositionX = *reinterpret_cast<float*>(RecordPtr + OffsetPosX);
			OutSamples[i].PositionY = *reinterpret_cast<float*>(RecordPtr + OffsetPosY);
			OutSamples[i].PositionZ = *reinterpret_cast<float*>(RecordPtr + OffsetPosZ);
			OutSamples[i].Rotation = *reinterpret_cast<float*>(RecordPtr + OffsetRotation);
			OutSamples[i].Speed = *reinterpret_cast<float*>(RecordPtr + OffsetSpeed);

			const char* ModePtr = reinterpret_cast<const char*>(RecordPtr + OffsetMode);
			OutSamples[i].Mode = FString(UTF8_TO_TCHAR(ModePtr));
		}

		UE_LOG(LogHdf5SimulationReader, Log, TEXT("Read %d samples"), NumSamples);
	}

	// Cleanup
	H5Tclose(FileType);
	H5Sclose(Space);
	H5Dclose(Dataset);

	return Status >= 0;
}

bool FHdf5SimulationReader::ReadSamplesForTimestepRange(int32 StartTimestep, int32 EndTimestep, TArray<FHdf5SampleData>& OutSamples)
{
	if (FileId < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("No file is open"));
		return false;
	}

	// Need sample offsets to be populated
	if (SampleOffsets.Num() == 0)
	{
		TArray<int32> SamplesPerTs;
		if (!ReadSamplesPerTimestep(SamplesPerTs))
		{
			return false;
		}
	}

	// Validate range
	if (StartTimestep < 0 || EndTimestep >= SampleOffsets.Num() - 1 || StartTimestep > EndTimestep)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("Invalid timestep range: %d to %d"), StartTimestep, EndTimestep);
		return false;
	}

	int32 StartOffset = SampleOffsets[StartTimestep];
	int32 EndOffset = SampleOffsets[EndTimestep + 1];
	int32 Count = EndOffset - StartOffset;

	if (Count == 0)
	{
		OutSamples.Empty();
		return true;
	}

	hid_t Dataset = H5Dopen(FileId, "/simulation/samples", H5P_DEFAULT);
	if (Dataset < 0)
	{
		return false;
	}

	// Get the file's compound type
	hid_t FileType = H5Dget_type(Dataset);
	size_t FileTypeSize = H5Tget_size(FileType);

	// Create memory and file dataspaces for partial read
	hid_t FileSpace = H5Dget_space(Dataset);

	hsize_t Start[1] = { static_cast<hsize_t>(StartOffset) };
	hsize_t CountH[1] = { static_cast<hsize_t>(Count) };
	H5Sselect_hyperslab(FileSpace, H5S_SELECT_SET, Start, nullptr, CountH, nullptr);

	hid_t MemSpace = H5Screate_simple(1, CountH, nullptr);

	// Read using file's native type
	TArray<uint8> RawBuffer;
	RawBuffer.SetNum(Count * FileTypeSize);

	herr_t Status = H5Dread(Dataset, FileType, MemSpace, FileSpace, H5P_DEFAULT, RawBuffer.GetData());

	if (Status >= 0)
	{
		// Get field offsets
		size_t OffsetTimestepIdx = H5Tget_member_offset(FileType, 0);
		size_t OffsetEntityId = H5Tget_member_offset(FileType, 1);
		size_t OffsetPosX = H5Tget_member_offset(FileType, 2);
		size_t OffsetPosY = H5Tget_member_offset(FileType, 3);
		size_t OffsetPosZ = H5Tget_member_offset(FileType, 4);
		size_t OffsetRotation = H5Tget_member_offset(FileType, 5);
		size_t OffsetSpeed = H5Tget_member_offset(FileType, 6);
		size_t OffsetMode = H5Tget_member_offset(FileType, 7);

		OutSamples.SetNum(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			uint8* RecordPtr = RawBuffer.GetData() + i * FileTypeSize;

			OutSamples[i].TimestepIndex = *reinterpret_cast<int32_t*>(RecordPtr + OffsetTimestepIdx);
			OutSamples[i].EntityId = *reinterpret_cast<int32_t*>(RecordPtr + OffsetEntityId);
			OutSamples[i].PositionX = *reinterpret_cast<float*>(RecordPtr + OffsetPosX);
			OutSamples[i].PositionY = *reinterpret_cast<float*>(RecordPtr + OffsetPosY);
			OutSamples[i].PositionZ = *reinterpret_cast<float*>(RecordPtr + OffsetPosZ);
			OutSamples[i].Rotation = *reinterpret_cast<float*>(RecordPtr + OffsetRotation);
			OutSamples[i].Speed = *reinterpret_cast<float*>(RecordPtr + OffsetSpeed);

			const char* ModePtr = reinterpret_cast<const char*>(RecordPtr + OffsetMode);
			OutSamples[i].Mode = FString(UTF8_TO_TCHAR(ModePtr));
		}
	}

	H5Tclose(FileType);
	H5Sclose(MemSpace);
	H5Sclose(FileSpace);
	H5Dclose(Dataset);

	return Status >= 0;
}

bool FHdf5SimulationReader::IsValidSimulationFile(const FString& FilePath)
{
	if (!FPaths::FileExists(FilePath))
	{
		return false;
	}

	FTCHARToUTF8 FilePathUtf8(*FilePath);

	// Check if it's a valid HDF5 file
	htri_t IsHdf5 = H5Fis_hdf5(FilePathUtf8.Get());
	if (IsHdf5 <= 0)
	{
		return false;
	}

	// Open and check for required groups/datasets
	H5open();
	hid_t TempFileId = H5Fopen(FilePathUtf8.Get(), H5F_ACC_RDONLY, H5P_DEFAULT);
	if (TempFileId < 0)
	{
		H5close();
		return false;
	}

	bool bValid = true;

	// Check for required structure
	if (H5Lexists(TempFileId, "/metadata", H5P_DEFAULT) <= 0)
	{
		bValid = false;
	}
	if (bValid && H5Lexists(TempFileId, "/entities", H5P_DEFAULT) <= 0)
	{
		bValid = false;
	}
	if (bValid && H5Lexists(TempFileId, "/simulation/timesteps", H5P_DEFAULT) <= 0)
	{
		bValid = false;
	}
	if (bValid && H5Lexists(TempFileId, "/simulation/samples", H5P_DEFAULT) <= 0)
	{
		bValid = false;
	}

	H5Fclose(TempFileId);
	H5close();

	return bValid;
}