using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;

public class Hdf5_DataImporter : ModuleRules
{
    public Hdf5_DataImporter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore"
            }
        );
        
        
    }
}