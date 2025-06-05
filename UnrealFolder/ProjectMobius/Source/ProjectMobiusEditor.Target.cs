// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.Collections.Generic;

public class ProjectMobiusEditorTarget : TargetRules
{
	public ProjectMobiusEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V4;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4;
		ExtraModuleNames.AddRange( new string[]
		{
			"HeatmapVisualization",
			"ErrorHandling",
		} );
		RegisterModulesCreatedByRider();
	}

	private void RegisterModulesCreatedByRider()
	{
		ExtraModuleNames.AddRange(new string[] { "MobiusWidgets", "Visualization" });
	}
}
