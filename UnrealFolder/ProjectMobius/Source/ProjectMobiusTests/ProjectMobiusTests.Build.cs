// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
using UnrealBuildTool;

public class ProjectMobiusTests : ModuleRules
{
	public ProjectMobiusTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AutomationController",
			"ProjectMobius",
			"MobiusDataImporter",
			"UeHdf5Library", // Hdf5ImportMatrixTest writes .h5 fixtures via the HDF5 C API (T4)
			"MassEntity",
			"StructUtils",
			"MassCommon",
			"MassSpawner",
			"MassRepresentation", // transitively required: PedestrianMovementProcessor.h -> SimulationFragment.h -> MRS_RepresentationSubsystem.h -> MassRepresentationSubsystem.h
			"Json",
			"JsonUtilities",
			"MobiusCore",
			"Visualization", // TrajectoryHeatmapCalibrationTest drives UDynamicPixelRenderingTexture directly
			"UMG", // SlateVectorArtQuadTest inspects USlateVectorArtData (extent/verts/material)
		});
	}
}
