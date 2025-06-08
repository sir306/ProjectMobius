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
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore", 
                "ProjectMobius", // the two files that cause the circular dependency are: FloorStatsWidget.cpp and BaseChangePedestrianMaterial.h
                // The problematic file is the floor stats widget, which is a UMG widget that displays the stats of the floor
                /* TODO:
                 * It depends on three MassAI components:
                 * MassEntitySpawnSubsystem
                 * AgentDataSubsystem
                 * This is too many subsystems to not point to, to resolve this we will need to move lots of components around,
                 * the likely solution is to move the mass ai parts from the ProjectMobius module to the MobiusCore module. this is likely going
                 * to mean the ProjectMobius Module will need renaming to something more appropriate after the refactor.
                 * The issue of moving these components is that the state of the current project has lots of blueprints pointing to these components,
                 * and cause reference issues when moving them around.
                 *
                 * Another option is to devise a new subsytem that can be used to call functions on the widgets without relying on delegate bindings
                 * this would mean that the projectmobius module may ultimately be renamed to the MobiusAI module as that is the main functionality
                 * left in the module.
                 */
                "MassRepresentation", 
                "HTTP", 
                "Visualization", 
                "Json", 
                "JsonUtilities",
                "MobiusCore",
            }
        );
    }
}