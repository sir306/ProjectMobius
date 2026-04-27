// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

using UnrealBuildTool;

public class MobiusDataImporter : ModuleRules
{
	public MobiusDataImporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Json",
				"UeHdf5Library",
				"Projects"
				// ... add other public dependencies that you statically link with here ...
			}
			);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("Shlwapi.lib");
		}
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
		
		// TODO: currently statically linking HDF5, may need to change to dynamic linking in future 
	}
}
