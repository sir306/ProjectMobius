using UnrealBuildTool;

public class MobiusEditor : ModuleRules
{
	public MobiusEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"EditorScriptingUtilities",
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
			"AssetTools",
		});

		PublicIncludePaths.AddRange(new[] { "MobiusEditor/Public" });
		PrivateIncludePaths.AddRange(new[] { "MobiusEditor/Private" });
	}
}
