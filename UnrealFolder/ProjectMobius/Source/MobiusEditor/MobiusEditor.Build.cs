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
			"AssetTools",
			// A6b (2026-07-28): MobiusWidgetEditorTools — scripted widget-class swap for the
			// UMobiusThemedBorder migration. UMG = runtime widget/tree API (UWidgetTree,
			// UPanelWidget::ReplaceChild); UMGEditor = UWidgetBlueprint itself, which is editor-only.
			"UMG",
			"UMGEditor",
			"Slate",
			"SlateCore",
		});

		PublicIncludePaths.AddRange(new[] { "MobiusEditor/Public" });
		PrivateIncludePaths.AddRange(new[] { "MobiusEditor/Private" });
	}
}
