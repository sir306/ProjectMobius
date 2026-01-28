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

	// Detect file format
	// Check for Juelich format: has /trajectory dataset
	if (H5Lexists(FileId, "/trajectory", H5P_DEFAULT) > 0)
	{
		DetectedFormat = EHdf5FormatType::Juelich;
		UE_LOG(LogHdf5SimulationReader, Log, TEXT("Detected Juelich format (has /trajectory dataset)"));

		// For Juelich format, get trajectory count as entity proxy
		hid_t TrajDataset = H5Dopen(FileId, "/trajectory", H5P_DEFAULT);
		if (TrajDataset >= 0)
		{
			hid_t Space = H5Dget_space(TrajDataset);
			if (Space >= 0)
			{
				hsize_t Dims[1];
				H5Sget_simple_extent_dims(Space, Dims, nullptr);
				TotalSampleCount = static_cast<int32>(Dims[0]);
				H5Sclose(Space);
			}
			H5Dclose(TrajDataset);
		}
		UE_LOG(LogHdf5SimulationReader, Log, TEXT("Juelich file contains: %d trajectory records"), TotalSampleCount);
	}
	// Check for Mobius format: has /metadata AND /simulation/samples
	else if (H5Lexists(FileId, "/metadata", H5P_DEFAULT) > 0 &&
	         H5Lexists(FileId, "/simulation/samples", H5P_DEFAULT) > 0)
	{
		DetectedFormat = EHdf5FormatType::Mobius;
		UE_LOG(LogHdf5SimulationReader, Log, TEXT("Detected Mobius format"));

		// Populate cached counts for Mobius format
		if (!PopulateCachedCounts())
		{
			UE_LOG(LogHdf5SimulationReader, Warning, TEXT("Could not read file structure counts"));
		}
	}
	else
	{
		DetectedFormat = EHdf5FormatType::Unknown;
		UE_LOG(LogHdf5SimulationReader, Warning, TEXT("Unknown HDF5 file format"));
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
		DetectedFormat = EHdf5FormatType::Unknown;
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

bool FHdf5SimulationReader::ReadStringAttribute(hid_t GroupId, const char* AttrName, FString& OutValue)
{
	if (!H5Aexists(GroupId, AttrName))
	{
		// Root attributes are attached to "/" group; if we were given a file id, try the root group.
		if (H5Iget_type(GroupId) == H5I_FILE)
		{
			hid_t RootGroup = H5Gopen(GroupId, "/", H5P_DEFAULT);
			if (RootGroup >= 0)
			{
				bool bRootRead = ReadStringAttribute(RootGroup, AttrName, OutValue);
				H5Gclose(RootGroup);
				return bRootRead;
			}
		}
		return false;
	}

	hid_t AttrId = H5Aopen(GroupId, AttrName, H5P_DEFAULT);
	if (AttrId < 0)
	{
		return false;
	}

	// Get the datatype and size
	hid_t AttrType = H5Aget_type(AttrId);
	H5T_class_t TypeClass = H5Tget_class(AttrType);

	bool bSuccess = false;

	if (TypeClass == H5T_STRING)
	{
		// Check if it's variable-length or fixed-length
		htri_t IsVarStr = H5Tis_variable_str(AttrType);

		if (IsVarStr > 0)
		{
			// Variable-length string
			char* StrData = nullptr;
			hid_t MemType = H5Tcopy(H5T_C_S1);
			H5Tset_size(MemType, H5T_VARIABLE);
			H5Tset_cset(MemType, H5T_CSET_UTF8);

			if (H5Aread(AttrId, MemType, &StrData) >= 0 && StrData != nullptr)
			{
				OutValue = FString(UTF8_TO_TCHAR(StrData));
				H5free_memory(StrData);
				bSuccess = true;
			}
			H5Tclose(MemType);
		}
		else
		{
			// Fixed-length string
			size_t StrSize = H5Tget_size(AttrType);
			TArray<char> Buffer;
			Buffer.SetNumZeroed(StrSize + 1);

			if (H5Aread(AttrId, AttrType, Buffer.GetData()) >= 0)
			{
				OutValue = FString(UTF8_TO_TCHAR(Buffer.GetData()));
				bSuccess = true;
			}
		}
	}

	H5Tclose(AttrType);
	H5Aclose(AttrId);

	return bSuccess;
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

bool FHdf5SimulationReader::ReadAllSamples(TArray<FHdf5SampleData>& OutSamples, bool* OutHasRotationField, bool* OutHasSpeedField)
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

	// Check if compound type has a rotation field
	int RotationFieldIdx = H5Tget_member_index(FileType, "rotation");
	bool bHasRotationField = (RotationFieldIdx >= 0);
	if (OutHasRotationField)
	{
		*OutHasRotationField = bHasRotationField;
	}
	UE_LOG(LogHdf5SimulationReader, Log, TEXT("Mobius samples has rotation field: %s"),
		bHasRotationField ? TEXT("Yes") : TEXT("No"));

	// Check if compound type has a speed field
	int SpeedFieldIdxCheck = H5Tget_member_index(FileType, "speed");
	bool bHasSpeedField = (SpeedFieldIdxCheck >= 0);
	if (OutHasSpeedField)
	{
		*OutHasSpeedField = bHasSpeedField;
	}
	UE_LOG(LogHdf5SimulationReader, Log, TEXT("Mobius samples has speed field: %s"),
		bHasSpeedField ? TEXT("Yes") : TEXT("No"));

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
		// TODO: Need to add a coordinate member variable so we can determine what direction these correspond to
		size_t OffsetPosX = H5Tget_member_offset(FileType, 2);
		size_t OffsetPosY = H5Tget_member_offset(FileType, 3);
		size_t OffsetPosZ = H5Tget_member_offset(FileType, 4);

		// Only get rotation offset if the field exists
		size_t OffsetRotation = bHasRotationField ? H5Tget_member_offset(FileType, RotationFieldIdx) : 0;
		// Speed and mode field indices may shift if rotation is missing - find them by name
		int SpeedFieldIdx = H5Tget_member_index(FileType, "speed");
		int ModeFieldIdx = H5Tget_member_index(FileType, "mode");
		size_t OffsetSpeed = (SpeedFieldIdx >= 0) ? H5Tget_member_offset(FileType, SpeedFieldIdx) : 0;
		size_t OffsetMode = (ModeFieldIdx >= 0) ? H5Tget_member_offset(FileType, ModeFieldIdx) : 0;

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
			OutSamples[i].Rotation = bHasRotationField ? *reinterpret_cast<float*>(RecordPtr + OffsetRotation) : 0.0f;
			OutSamples[i].Speed = (SpeedFieldIdx >= 0) ? *reinterpret_cast<float*>(RecordPtr + OffsetSpeed) : 0.0f;

			if (ModeFieldIdx >= 0)
			{
				const char* ModePtr = reinterpret_cast<const char*>(RecordPtr + OffsetMode);
				OutSamples[i].Mode = FString(UTF8_TO_TCHAR(ModePtr));
			}
			else
			{
				OutSamples[i].Mode = TEXT("walk");
			}
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
	// A valid simulation file is either Mobius or Juelich format
	EHdf5FormatType Format = DetectFormat(FilePath);
	return Format == EHdf5FormatType::Mobius || Format == EHdf5FormatType::Juelich;
}

EHdf5FormatType FHdf5SimulationReader::DetectFormat(const FString& FilePath)
{
	if (!FPaths::FileExists(FilePath))
	{
		return EHdf5FormatType::Unknown;
	}

	FTCHARToUTF8 FilePathUtf8(*FilePath);

	// Check if it's a valid HDF5 file
	htri_t IsHdf5 = H5Fis_hdf5(FilePathUtf8.Get());
	if (IsHdf5 <= 0)
	{
		return EHdf5FormatType::Unknown;
	}

	H5open();
	hid_t TempFileId = H5Fopen(FilePathUtf8.Get(), H5F_ACC_RDONLY, H5P_DEFAULT);
	if (TempFileId < 0)
	{
		H5close();
		return EHdf5FormatType::Unknown;
	}

	EHdf5FormatType Result = EHdf5FormatType::Unknown;

	// Check for Juelich format: has /trajectory dataset
	if (H5Lexists(TempFileId, "/trajectory", H5P_DEFAULT) > 0)
	{
		Result = EHdf5FormatType::Juelich;
	}
	// Check for Mobius format: has /metadata AND /simulation/samples
	else if (H5Lexists(TempFileId, "/metadata", H5P_DEFAULT) > 0 &&
	         H5Lexists(TempFileId, "/simulation/samples", H5P_DEFAULT) > 0)
	{
		Result = EHdf5FormatType::Mobius;
	}

	H5Fclose(TempFileId);
	H5close();

	return Result;
}

// ========== Juelich Format Reading ==========

bool FHdf5SimulationReader::ReadJuelichMetadata(FHdf5JuelichMetadata& OutMetadata)
{
	if (FileId < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("No file is open"));
		return false;
	}

	if (DetectedFormat != EHdf5FormatType::Juelich)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("File is not Juelich format"));
		return false;
	}

	// Read root attributes
	ReadStringAttribute(FileId, "wkt_geometry", OutMetadata.WktGeometry);
	ReadStringAttribute(FileId, "run_name", OutMetadata.RunName);

	// Try to read fps from root first
	double Fps = 25.0;
	if (!ReadAttribute(FileId, "fps", H5T_NATIVE_DOUBLE, Fps))
	{
		// Try to read from /trajectory dataset attribute
		hid_t TrajDataset = H5Dopen(FileId, "/trajectory", H5P_DEFAULT);
		if (TrajDataset >= 0)
		{
			ReadAttribute(TrajDataset, "fps", H5T_NATIVE_DOUBLE, Fps);
			H5Dclose(TrajDataset);
		}
	}
	OutMetadata.Fps = static_cast<float>(Fps);

	// Count unique participants by scanning trajectory data
	// We'll do a quick scan to find unique IDs
	hid_t TrajDataset = H5Dopen(FileId, "/trajectory", H5P_DEFAULT);
	if (TrajDataset >= 0)
	{
		hid_t Space = H5Dget_space(TrajDataset);
		hsize_t Dims[1];
		H5Sget_simple_extent_dims(Space, Dims, nullptr);
		int32 NumRecords = static_cast<int32>(Dims[0]);

		// Get file type to read id field only
		hid_t FileType = H5Dget_type(TrajDataset);
		size_t OffsetId = H5Tget_member_offset(FileType, 0); // id is first field

		// Check if compound type has a rotation field
		int RotationFieldIdx = H5Tget_member_index(FileType, "rotation");
		OutMetadata.bHasRotationField = (RotationFieldIdx >= 0);
		UE_LOG(LogHdf5SimulationReader, Log, TEXT("Juelich trajectory has rotation field: %s"),
			OutMetadata.bHasRotationField ? TEXT("Yes") : TEXT("No"));

		// Check if compound type has a speed field
		int SpeedFieldIdx = H5Tget_member_index(FileType, "speed");
		OutMetadata.bHasSpeedField = (SpeedFieldIdx >= 0);
		UE_LOG(LogHdf5SimulationReader, Log, TEXT("Juelich trajectory has speed field: %s"),
			OutMetadata.bHasSpeedField ? TEXT("Yes") : TEXT("No"));

		// Create a memory type for just the id field
		hid_t IdMemType = H5Tcreate(H5T_COMPOUND, sizeof(int64));
		H5Tinsert(IdMemType, "id", 0, H5T_NATIVE_INT64);

		TArray<int64> Ids;
		Ids.SetNum(NumRecords);
		H5Dread(TrajDataset, IdMemType, H5S_ALL, H5S_ALL, H5P_DEFAULT, Ids.GetData());

		// Count unique IDs
		TSet<int64> UniqueIds;
		for (int64 Id : Ids)
		{
			UniqueIds.Add(Id);
		}
		OutMetadata.NumParticipants = UniqueIds.Num();

		H5Tclose(IdMemType);
		H5Tclose(FileType);
		H5Sclose(Space);
		H5Dclose(TrajDataset);
	}

	UE_LOG(LogHdf5SimulationReader, Log, TEXT("Read Juelich metadata: fps=%.2f, run_name=%s, participants=%d"),
		OutMetadata.Fps, *OutMetadata.RunName, OutMetadata.NumParticipants);

	return true;
}

bool FHdf5SimulationReader::ReadJuelichTrajectories(TArray<FHdf5JuelichTrajectoryRecord>& OutRecords)
{
	if (FileId < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("No file is open"));
		return false;
	}

	if (DetectedFormat != EHdf5FormatType::Juelich)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("File is not Juelich format"));
		return false;
	}

	hid_t Dataset = H5Dopen(FileId, "/trajectory", H5P_DEFAULT);
	if (Dataset < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("Could not open /trajectory dataset"));
		return false;
	}

	// Get dataspace and dimensions
	hid_t Space = H5Dget_space(Dataset);
	hsize_t Dims[1];
	H5Sget_simple_extent_dims(Space, Dims, nullptr);
	int32 NumRecords = static_cast<int32>(Dims[0]);

	// Get the file's compound type
	hid_t FileType = H5Dget_type(Dataset);
	size_t FileTypeSize = H5Tget_size(FileType);

	// Read using file's native type
	TArray<uint8> RawBuffer;
	RawBuffer.SetNum(NumRecords * FileTypeSize);

	herr_t Status = H5Dread(Dataset, FileType, H5S_ALL, H5S_ALL, H5P_DEFAULT, RawBuffer.GetData());

	if (Status < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("Failed to read trajectory dataset"));
		H5Ewalk2(H5E_DEFAULT, H5E_WALK_DOWNWARD, [](unsigned n, const H5E_error2_t* err, void* data) -> herr_t {
			UE_LOG(LogHdf5SimulationReader, Error, TEXT("  HDF5 Error: %hs - %hs"), err->desc, err->func_name);
			return 0;
		}, nullptr);
	}
	else
	{
		// Get field offsets - Juelich format has: id(0), frame(1), x(2), y(3), z(4), and optionally rotation
		size_t OffsetId = H5Tget_member_offset(FileType, 0);
		size_t OffsetFrame = H5Tget_member_offset(FileType, 1);
		// TODO: Need to add a coordinate member variable so we can determine what direction these correspond to
		size_t OffsetX = H5Tget_member_offset(FileType, 2);
		size_t OffsetY = H5Tget_member_offset(FileType, 3);
		size_t OffsetZ = H5Tget_member_offset(FileType, 4);

		// Convert to output format
		OutRecords.SetNum(NumRecords);
		for (int32 i = 0; i < NumRecords; ++i)
		{
			uint8* RecordPtr = RawBuffer.GetData() + i * FileTypeSize;

			OutRecords[i].Id = *reinterpret_cast<int64*>(RecordPtr + OffsetId);
			OutRecords[i].Frame = *reinterpret_cast<int64*>(RecordPtr + OffsetFrame);
			OutRecords[i].X = *reinterpret_cast<double*>(RecordPtr + OffsetX);
			OutRecords[i].Y = *reinterpret_cast<double*>(RecordPtr + OffsetY);
			OutRecords[i].Z = *reinterpret_cast<double*>(RecordPtr + OffsetZ);
		}

		UE_LOG(LogHdf5SimulationReader, Log, TEXT("Read %d trajectory records"), NumRecords);
	}

	// Cleanup
	H5Tclose(FileType);
	H5Sclose(Space);
	H5Dclose(Dataset);

	return Status >= 0;
}

bool FHdf5SimulationReader::ReadWktGeometry(FString& OutWktGeometry)
{
	if (FileId < 0)
	{
		UE_LOG(LogHdf5SimulationReader, Error, TEXT("No file is open"));
		return false;
	}

	// Read wkt_geometry attribute from root
	if (!ReadStringAttribute(FileId, "wkt_geometry", OutWktGeometry))
	{
		UE_LOG(LogHdf5SimulationReader, Warning, TEXT("No wkt_geometry attribute found"));
		return false;
	}

	UE_LOG(LogHdf5SimulationReader, Log, TEXT("Read WKT geometry (%d chars)"), OutWktGeometry.Len());
	return true;
}

bool FHdf5SimulationReader::ConvertJuelichToMobiusFormat(
	const FHdf5JuelichMetadata& JuelichMeta,
	const TArray<FHdf5JuelichTrajectoryRecord>& Trajectories,
	FHdf5SimulationMetadata& OutMetadata,
	TArray<FHdf5EntityData>& OutEntities,
	TArray<FHdf5SampleData>& OutSamples)
{
	if (Trajectories.Num() == 0)
	{
		UE_LOG(LogHdf5SimulationReader, Warning, TEXT("No trajectories to convert"));
		return false;
	}

	// Find unique IDs and frame range
	TSet<int64> UniqueIds;
	int64 MinFrame = TNumericLimits<int64>::Max();
	int64 MaxFrame = TNumericLimits<int64>::Min();
	double MinZ = TNumericLimits<double>::Max();
	double MaxZ = TNumericLimits<double>::Lowest();

	for (const FHdf5JuelichTrajectoryRecord& Record : Trajectories)
	{
		UniqueIds.Add(Record.Id);
		MinFrame = FMath::Min(MinFrame, Record.Frame);
		MaxFrame = FMath::Max(MaxFrame, Record.Frame);
		MinZ = FMath::Min(MinZ, Record.Z);
		MaxZ = FMath::Max(MaxZ, Record.Z);
	}

	// Create entity data from unique IDs
	TArray<int64> SortedIds = UniqueIds.Array();
	SortedIds.Sort();

	// Create ID mapping for quick lookup (maps original Juelich ID to sequential index)
	TMap<int64, int32> IdToEntityIndex;
	OutEntities.SetNum(SortedIds.Num());
	for (int32 i = 0; i < SortedIds.Num(); ++i)
	{
		// Use sequential index as the entity ID for compatibility with downstream code
		// that uses EntityId to index directly into arrays
		OutEntities[i].Id = i;
		OutEntities[i].Name = FString::Printf(TEXT("Entity_%lld"), SortedIds[i]); // Keep original ID in name for reference
		OutEntities[i].SimTimeS = 0.0f; // Will be calculated
		OutEntities[i].MaxSpeed = 0.0f; // Default
		OutEntities[i].MPlane = TEXT("DefaultPlane");
		OutEntities[i].Map = 0;

		IdToEntityIndex.Add(SortedIds[i], i);
	}

	// Calculate metadata
	int32 NumFrames = static_cast<int32>(MaxFrame - MinFrame + 1);
	float FrameDuration = 1.0f / JuelichMeta.Fps;
	float Duration = NumFrames * FrameDuration;

	OutMetadata.Duration = Duration;
	OutMetadata.SamplingRate = FrameDuration;
	OutMetadata.MaxNumEntities = SortedIds.Num();
	OutMetadata.bIsSI = true;  // Juelich format is typically in SI units
	OutMetadata.bIsDeg = true; // Default assumption
	OutMetadata.bHasRotationData = JuelichMeta.bHasRotationField;  // Propagate rotation field presence
	OutMetadata.bHasSpeedData = JuelichMeta.bHasSpeedField;        // Propagate speed field presence

	// If Z is a constant (common in Juelich files: head height), subtract it so agents are on the floor.
	double ZOffset = 0.0;
	if (FMath::IsNearlyEqual(MinZ, MaxZ, 1e-4))
	{
		ZOffset = MinZ;
		UE_LOG(LogHdf5SimulationReader, Log, TEXT("Applying Juelich Z offset: %.4f m"), ZOffset);
	}

	// Track entity first appearance and total time for SimTimeS
	TMap<int64, int64> EntityFirstFrame;
	TMap<int64, int64> EntityLastFrame;

	for (const FHdf5JuelichTrajectoryRecord& Record : Trajectories)
	{
		if (!EntityFirstFrame.Contains(Record.Id))
		{
			EntityFirstFrame.Add(Record.Id, Record.Frame);
		}
		EntityFirstFrame[Record.Id] = FMath::Min(EntityFirstFrame[Record.Id], Record.Frame);

		if (!EntityLastFrame.Contains(Record.Id))
		{
			EntityLastFrame.Add(Record.Id, Record.Frame);
		}
		EntityLastFrame[Record.Id] = FMath::Max(EntityLastFrame[Record.Id], Record.Frame);
	}

	// Update entity SimTimeS based on their appearance duration
	for (int32 i = 0; i < SortedIds.Num(); ++i)
	{
		int64 Id = SortedIds[i];
		if (EntityFirstFrame.Contains(Id) && EntityLastFrame.Contains(Id))
		{
			int64 EntityFrames = EntityLastFrame[Id] - EntityFirstFrame[Id] + 1;
			OutEntities[i].SimTimeS = EntityFrames * FrameDuration;
		}
	}

	// Convert trajectories to samples
	// IMPORTANT: EntityId must be the sequential index (0 to N-1), not the original ID,
	// because downstream code uses EntityId to directly index into AgentDataArray
	OutSamples.SetNum(Trajectories.Num());
	for (int32 i = 0; i < Trajectories.Num(); ++i)
	{
		const FHdf5JuelichTrajectoryRecord& Record = Trajectories[i];

		OutSamples[i].TimestepIndex = static_cast<int32>(Record.Frame - MinFrame);
		OutSamples[i].EntityId = IdToEntityIndex[Record.Id]; // Use sequential index, not original ID
		OutSamples[i].PositionX = static_cast<float>(Record.X);
		OutSamples[i].PositionY = static_cast<float>(Record.Y);
		OutSamples[i].PositionZ = static_cast<float>(Record.Z - ZOffset);
		OutSamples[i].Rotation = 0.0f;  // Not provided in Juelich format
		OutSamples[i].Speed = 0.0f;     // Not provided, could be calculated from consecutive frames
		OutSamples[i].Mode = TEXT("walk"); // Default mode
	}

	// Sort samples by timestep index then entity ID for consistent ordering
	OutSamples.Sort([](const FHdf5SampleData& A, const FHdf5SampleData& B)
	{
		if (A.TimestepIndex != B.TimestepIndex)
		{
			return A.TimestepIndex < B.TimestepIndex;
		}
		return A.EntityId < B.EntityId;
	});

	UE_LOG(LogHdf5SimulationReader, Log,
		TEXT("Converted Juelich to Mobius: %d entities, %d samples, duration=%.2fs, fps=%.2f"),
		OutEntities.Num(), OutSamples.Num(), Duration, JuelichMeta.Fps);

	return true;
}
