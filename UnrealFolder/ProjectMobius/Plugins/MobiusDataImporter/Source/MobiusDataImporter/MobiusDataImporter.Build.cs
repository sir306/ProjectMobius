// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

using UnrealBuildTool;

public class MobiusDataImporter : ModuleRules
{
	public MobiusDataImporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// simdjson (ThirdParty/simdjson, perf task A7): the amalgamated .cpp is compiled inside this
		// module (MobiusSimdJsonAmalgamation.cpp), so keep it out of unity blobs where UE headers and
		// their macros would leak into it.
		bUseUnity = false;
		PrivateIncludePaths.Add(System.IO.Path.Combine(PluginDirectory, "Source", "ThirdParty", "simdjson"));
		// UE builds without C++ exceptions; restrict simdjson to its error-code API.
		PrivateDefinitions.Add("SIMDJSON_EXCEPTIONS=0");
		// UE's default /fp:fast lets MSVC fold simdjson's sign-of-zero handling
		// ("negative ? -0.0 : 0.0" becomes +0.0), silently breaking bit-parity with the CRT-parsed
		// pull-parser path. Parsers must be IEEE-exact; this module does no hot FP math.
		FPSemantics = FPSemanticsMode.Precise;

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
				"Projects",
				"XmlParser"
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
