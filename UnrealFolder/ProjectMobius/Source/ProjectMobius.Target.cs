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

		// check()/ensure() and friends bake __FILE__ into the binary, so a packaged build carries the
		// build machine's absolute source paths (home folder included). Rewrite the project directory
		// to "." for the packaged Mac game; the editor keeps real paths so local debugging finds sources.
		// The installed engine shares UnrealGame's build environment, hence bOverrideBuildEnvironment.
		if (Target.Platform == UnrealTargetPlatform.Mac && ProjectFile != null && !ProjectFile.Directory.FullName.Contains(' '))
		{
			bOverrideBuildEnvironment = true;
			AdditionalCompilerArguments = $"-ffile-prefix-map={ProjectFile.Directory.FullName}=.";
		}
	}

	private void RegisterModulesCreatedByRider()
	{
		ExtraModuleNames.AddRange(new string[] { "MobiusWidgets", "Visualization", "MobiusCore" });
	}
}
