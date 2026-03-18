using UnrealBuildTool;

public class MobiusEditor : ModuleRules
{
	public MobiusEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"MaterialEditor",
			"MobiusCore",
			"DatasmithRuntime",
			"DatasmithContent",
			"EditorScriptingUtilities",
			"AssetTools",
		});

		PublicIncludePaths.AddRange(new[] { "MobiusEditor/Public" });
		PrivateIncludePaths.AddRange(new[] { "MobiusEditor/Private" });
	}
}
