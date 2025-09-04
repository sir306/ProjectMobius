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
                "RHI",
                "UE_Assimp",
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
                "WebSockets",
                "DatasmithRuntime", 
                "DatasmithCore",
                "Visualization",
                "RenderCore",
                "UE_Assimp",
            }
        );
        
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "UE_Assimp",
            "UE_AssimpLibrary",
        });
        
        // TODO: Sort different build versions for different platforms 
        if ((Target.Platform == UnrealTargetPlatform.Win64))
        {

	        
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