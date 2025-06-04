using UnrealBuildTool;
 
public class ErrorHandling : ModuleRules
{
	public ErrorHandling(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", 
			"CoreUObject", 
			"Engine",
			"UMG", // Required for UMG
			"Slate",
			"SlateCore",
		});
 
		PublicIncludePaths.AddRange(new string[] {"ErrorHandling/Public"});
		PrivateIncludePaths.AddRange(new string[] {"ErrorHandling/Private"});
	}
}