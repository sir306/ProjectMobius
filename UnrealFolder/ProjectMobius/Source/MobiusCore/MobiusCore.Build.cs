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
			"MobiusLogging",
			"MovieSceneCapture", /* For image writing support -> built-in screenshot api causes issues when scene not fully loaded */
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
			"DatasmithContent", "Hdf5DataPlugin",
		});

		PublicIncludePaths.AddRange(new[]
		{
			"MobiusCore/ThirdParty",
		});
		
		PrivateIncludePaths.AddRange(new[]
		{
			"MobiusCore/Public",
			"MobiusCore/Private",
			"MobiusCore/ThirdParty",
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{

		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			// Add AppKit framework for NSOpenPanel file dialogs
			PublicFrameworks.Add("AppKit");
		}
	}
}
