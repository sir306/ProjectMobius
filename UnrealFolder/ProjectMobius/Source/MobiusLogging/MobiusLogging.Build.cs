using UnrealBuildTool;

public class MobiusLogging : ModuleRules
{
	public MobiusLogging(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PublicIncludePaths.AddRange(new[] { "MobiusLogging/Public" });
		PrivateIncludePaths.AddRange(new[] { "MobiusLogging/Private" });
	}
}
