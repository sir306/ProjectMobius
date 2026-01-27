// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hdf5DataPlugin.h"
#include "Hdf5DataExampleTest.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FHdf5DataPluginModule"

void FHdf5DataPluginModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	// Uncomment to run a simple HDF5 example file open/iterate log on startup.
	FHdf5DataExampleTest::RunExampleFile();
}

void FHdf5DataPluginModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FHdf5DataPluginModule, Hdf5DataPlugin)
