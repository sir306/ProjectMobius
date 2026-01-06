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
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicIncludePaths.AddRange(new[]
			{
				"MobiusCore/ThirdParty",
			});

			PrivateIncludePaths.AddRange(new[]
			{
				"MobiusCore/ThirdParty",
			});
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicIncludePaths.AddRange(new[]
			{
				"MobiusCore/ThirdParty/earcut_hpp",
				// This tells the linker to include the AppKit framework (Cocoa) -> Apple's native GUI library
				"AppKit",
			});
			PrivateIncludePaths.AddRange(new[]
			{
				"MobiusCore/ThirdParty/earcut_hpp",
			});
		}
	}
}
