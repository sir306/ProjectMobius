// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

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
            "Chaos", 
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
            "HeatmapVisualization", 
            "MobiusWidgets", 
            "Slate",
            "SlateCore",
            "RenderCore",
            // for JSOn handling and our web sockets
            "Json", 
            "JsonUtilities", 
            "MobiusCore",
        });

    }
}
