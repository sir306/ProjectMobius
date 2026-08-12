// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "MobiusDataImporter.h"
#include "Hdf5DataExampleTest.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FMobiusDataImporterModule"

void FMobiusDataImporterModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	// Run original HDF5 example file open/iterate log test
	FHdf5DataExampleTest::RunExampleFile();

	// Test the simulation reader with generated test file
	FHdf5DataExampleTest::TestSimulationReader();
}

void FMobiusDataImporterModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FMobiusDataImporterModule, MobiusDataImporter)
