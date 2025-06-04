// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using UnrealBuildTool;
using System.IO;

public class ProjectMobius : ModuleRules
{
	public ProjectMobius(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { 
            "Core", 
            "CoreUObject", 
            "Engine", 
            "InputCore", 
            "EnhancedInput", 
            "UMG", // Required for UMG
            "Slate",
            "SlateCore",
            "ApplicationCore",
            // The following are required for our runtime procedural mesh generation
            "ProceduralMeshComponent",
            // Physics -- following modules are required for PhysicsParallelFor
            "Chaos", "Visualization",
        });

        // The following modules are required for the MASS system to run with a couple of added modules for convenience
        PrivateDependencyModuleNames.AddRange(new string[] {
            "MassEntity",
            "StructUtils",
            "MassCommon",
            "MassMovement",
            "MassActors",
            "MassSpawner",
            "MassGameplayDebug",
            "MassSignals",
            "MassCrowd",
            "MassActors",
            "MassRepresentation",
            "MassReplication",
            "MassNavigation",
            // Needed for creating a custom Niagara Actor
            "Niagara", 
            // External Modules needed for this project that have been made to decouple functionality
            "HeatmapVisualization", "Visualization", "MobiusWidgets", 
            "Slate",
            "SlateCore",
            "DatasmithRuntime", 
            "DatasmithCore",
            "RenderCore",
            // for JSOn handling and our web sockets
            "Json", 
            "JsonUtilities", 
            "WebSockets",
        });
        // TODO: Sort different build versions for different platforms 
        if ((Target.Platform == UnrealTargetPlatform.Win64))
        {
	        // get the project dir
	        string projectDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../"));
	        PublicIncludePaths.Add(Path.Combine(projectDir, "Source\\ProjectMobius\\ThirdParty"));
	        
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
            "ProjectMobius/Public",
            "ProjectMobius/Private",
            "ProjectMobius/ThirdParty",
        });

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
        
   
    }
}
