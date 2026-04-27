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
			"MassEntity",
			"StructUtils",
			"MassCommon",
			"MassSpawner",
			"Json",
			"JsonUtilities",
			"MobiusCore",
		});
	}
}
