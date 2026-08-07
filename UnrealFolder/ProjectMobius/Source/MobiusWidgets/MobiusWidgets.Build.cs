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
                "Synthesis", // Required for the synth2d component
                "MobiusLogging",
            }
        );
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "InputCore",
                "ApplicationCore", // FPlatformApplicationMisc::ClipboardCopy — the chart TSV export (S6)
                "AssetRegistry", // UIThemeSubsystem sweeps shared Slate style assets on theme switch

                "ProjectMobius", // FloorStatsWidget and BaseChangePedestrianMaterial use MassAI subsystems from ProjectMobius
                /* TODO: Move MassEntitySpawnSubsystem and AgentDataSubsystem to MobiusCore
                 * to eliminate this cross-module reach. Blocked by Blueprint references to
                 * current component paths — needs CoreRedirects when moved.
                 */
                "MassRepresentation",
                "HTTP",
                "RHI",
                "RenderCore",
                "Json",
                "JsonUtilities",
                "MobiusCore", "Niagara",
            }
        );

        PrivateIncludePaths.AddRange(
            new string[]
            {
                "MobiusWidgets/ThirdParty/ImGui",
                "MobiusWidgets/ThirdParty/ImPlot",
            }
        );
    }
}
