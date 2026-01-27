// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hdf5DataExampleTest.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#include "hdf5.h"

DEFINE_LOG_CATEGORY_STATIC(LogHdf5DataExampleTest, Log, All);

namespace
{
	herr_t LogLinkCallback(hid_t group, const char* name, const H5L_info_t* info, void* opData)
	{
		UE_LOG(LogHdf5DataExampleTest, Log, TEXT("HDF5 entry: %hs"), name);
		return 0;
	}
}

void FHdf5DataExampleTest::RunExampleFile()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Hdf5DataPlugin"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogHdf5DataExampleTest, Error, TEXT("Hdf5DataPlugin not found."));
		return;
	}

	const FString ExamplePath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Source"),
		TEXT("ThirdParty"),
		TEXT("hdf5-2.0.0"),
		TEXT("HDF5Examples"),
		TEXT("C"),
		TEXT("H5G"),
		TEXT("h5ex_g_iterate.h5"));

	if (!FPaths::FileExists(ExamplePath))
	{
		UE_LOG(LogHdf5DataExampleTest, Warning, TEXT("Example HDF5 file not found: %s"), *ExamplePath);
		return;
	}

	FTCHARToUTF8 ExamplePathUtf8(*ExamplePath);

	H5open();

	const hid_t FileId = H5Fopen(ExamplePathUtf8.Get(), H5F_ACC_RDONLY, H5P_DEFAULT);
	if (FileId < 0)
	{
		UE_LOG(LogHdf5DataExampleTest, Error, TEXT("Failed to open HDF5 file: %s"), *ExamplePath);
		H5close();
		return;
	}

	UE_LOG(LogHdf5DataExampleTest, Log, TEXT("Opened HDF5 file: %s"), *ExamplePath);

	hsize_t Index = 0;
	const herr_t IterateStatus = H5Literate(FileId, H5_INDEX_NAME, H5_ITER_NATIVE, &Index, &LogLinkCallback, nullptr);
	if (IterateStatus < 0)
	{
		UE_LOG(LogHdf5DataExampleTest, Error, TEXT("Failed to iterate HDF5 root links for: %s"), *ExamplePath);
	}

	H5Fclose(FileId);
	H5close();
}
