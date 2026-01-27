// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FHdf5DataExampleTest
{
public:
	/** Run the original HDF5 example file test (group iteration) */
	static void RunExampleFile();

	/** Test the simulation reader with the generated test HDF5 file */
	static void TestSimulationReader();
};
