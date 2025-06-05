using UnrealBuildTool;

public class MobiusCore : ModuleRules
{
    public MobiusCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "CoreUObject",
        });
    }
}
