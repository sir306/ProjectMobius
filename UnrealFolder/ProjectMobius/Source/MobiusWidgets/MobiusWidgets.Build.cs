using UnrealBuildTool;

public class MobiusWidgets : ModuleRules
{
    public MobiusWidgets(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "UMG", 
                "Synthesis",// Required for the synth2d component
                "ProjectMobius", // Required for UMG
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore", "ProjectMobius", "MassRepresentation", "HTTP", "Visualization", "Json", 
                "JsonUtilities", 
            }
        );
    }
}