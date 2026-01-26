using UnrealBuildTool;
using System.IO;
 
public class HeatmapVisualization : ModuleRules
{
	public HeatmapVisualization(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine"});
		PrivateDependencyModuleNames.AddRange(new string[] { "MobiusCore" });
 
		PublicIncludePaths.AddRange(new string[] {"HeatmapVisualization/Public"});
		PrivateIncludePaths.AddRange(new string[] { "HeatmapVisualization/Private" });
	}
}
