// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
using UnrealBuildTool;
using System.Collections.Generic;

public class ProjectMobiusTarget : TargetRules
{
	public ProjectMobiusTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V4;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4;
		ExtraModuleNames.AddRange( new string[]
		{
			"HeatmapVisualization",
		} );
		RegisterModulesCreatedByRider();
		
	}

	private void RegisterModulesCreatedByRider()
	{
		ExtraModuleNames.AddRange(new string[] { "MobiusWidgets", "Visualization", "MobiusCore" });
	}
}
