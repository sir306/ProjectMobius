using System;
using System.IO;
using UnrealBuildTool;

public class MobiusCore : ModuleRules
{
	public MobiusCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Public deps: only what your *public headers* require
		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"MassEntity",
			"ProceduralMeshComponent",
			"RHI",
			"UE_Assimp",
			"UE_AssimpLibrary",
		});

		// Private deps: used in your .cpp files
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"HTTP",
			"Json",
			"JsonUtilities",
			"WebSockets",
			"DatasmithRuntime",
			"DatasmithCore",
			"Visualization",
			"RenderCore",
			"DatasmithContent",
		});


		PrivateIncludePaths.AddRange(new[]
		{
			"MobiusCore/Public",
			"MobiusCore/Private",
			"MobiusCore/ThirdParty",
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Platform switches if you need them later
		}
	}
}