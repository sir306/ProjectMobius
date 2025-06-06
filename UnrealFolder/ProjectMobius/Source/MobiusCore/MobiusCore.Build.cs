using System;
using UnrealBuildTool;
using System.IO;

public class MobiusCore : ModuleRules
{
    public MobiusCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "MassEntity",
                "ProceduralMeshComponent",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "HTTP",  
                "Json", 
                "JsonUtilities",
                "MobiusWidgets", // need to break the dependency on the MobiusWidgets module
                "WebSockets",
                "DatasmithRuntime", 
                "DatasmithCore",
                "Visualization",
            }
        );
        
        // TODO: Sort different build versions for different platforms 
        if ((Target.Platform == UnrealTargetPlatform.Win64))
        {
            // get the project dir
            string projectDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../"));
            PublicIncludePaths.Add(Path.Combine(projectDir, "Source\\MobiusCore\\ThirdParty"));
	        
            string platformString = (Target.Platform == UnrealTargetPlatform.Win64) ? "Win64" : "Win32";
            string librariesPath = Path.Combine(projectDir, "Binaries", platformString);

            PublicAdditionalLibraries.Add(Path.Combine(librariesPath, "assimp-vc143-mt.lib"));
	        
            // load the dll
            string dllPath = Path.Combine(librariesPath, "assimp-vc143-mt.dll");
	        
            PublicDelayLoadDLLs.Add(dllPath);
            RuntimeDependencies.Add(dllPath);
	        
        }

        PrivateIncludePaths.AddRange(new string[]
        {
            "MobiusCore/Public",
            "MobiusCore/Private",
            "MobiusCore/ThirdParty",
        });

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true

    }
}