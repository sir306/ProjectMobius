// MobiusEditor module implementation.
// Auto-generates Datasmith override materials on editor startup if missing.

#include "Modules/ModuleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Commandlets/GenerateDatasmithMaterialsCommandlet.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobiusEditor, Log, All);

class FMobiusEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Wait for the asset registry to finish its initial scan before checking
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		if (AssetRegistry.IsLoadingAssets())
		{
			AssetRegistry.OnFilesLoaded().AddRaw(this, &FMobiusEditorModule::OnAssetRegistryReady);
		}
		else
		{
			OnAssetRegistryReady();
		}
	}

	virtual void ShutdownModule() override {}

private:
	/**
	 * True when this process has no Slate application to host a modal dialog.
	 *
	 * The engine only creates one for a regular client or an editor-token run, so a commandlet
	 * (-run=...) has none, and material generation reaches editor asset paths that construct
	 * dialogs — where SWindow's constructor asserts in FSlateInvalidationRoot and takes the
	 * process down. An -unattended editor does get a Slate application, but a dialog there would
	 * block a run with nobody to answer it, so that counts as headless too.
	 */
	static bool IsHeadlessRun()
	{
		return !FSlateApplication::IsInitialized() || IsRunningCommandlet() || FApp::IsUnattended();
	}

	void OnAssetRegistryReady()
	{
		// Existence is answered from disk, not from the asset registry. A commandlet or
		// -unattended run never performs the initial registry scan, so every registry query there
		// misses and a perfectly healthy tree looks like it is missing every override — which is
		// what drove generation (and the crash that followed) on runs where the assets were
		// present all along.
		const TArray<FString> MissingAssets = UGenerateDatasmithMaterialsCommandlet::GetMissingAssetPaths();
		if (MissingAssets.Num() == 0)
		{
			UE_LOG(LogMobiusEditor, Display, TEXT("Datasmith override materials already exist — skipping generation."));
			return;
		}

		const bool bHeadless = IsHeadlessRun();

		// Named individually at Error in a headless run: a cook that goes ahead without these
		// produces a build with unresolved Datasmith overrides, and that must not pass quietly.
		UE_LOG(LogMobiusEditor, Warning, TEXT("%d Datasmith override material(s) missing:"), MissingAssets.Num());
		for (const FString& MissingAsset : MissingAssets)
		{
			if (bHeadless)
			{
				UE_LOG(LogMobiusEditor, Error, TEXT("  MISSING: %s"), *MissingAsset);
			}
			else
			{
				UE_LOG(LogMobiusEditor, Warning, TEXT("  missing: %s"), *MissingAsset);
			}
		}

		if (bHeadless)
		{
			UE_LOG(LogMobiusEditor, Error,
				TEXT("Cannot auto-generate Datasmith override materials in a headless run — skipping. ")
				TEXT("Any cook or package built from this tree will be missing them. Generate them first with:"));
			UE_LOG(LogMobiusEditor, Error, TEXT("  UnrealEditor-Cmd.exe \"%s\" -run=GenerateDatasmithMaterials"),
				*FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
			return;
		}

		UE_LOG(LogMobiusEditor, Warning, TEXT("Auto-generating Datasmith override materials..."));

		UGenerateDatasmithMaterialsCommandlet* Generator = NewObject<UGenerateDatasmithMaterialsCommandlet>();
		const int32 Result = Generator->Main(TEXT(""));

		if (Result == 0)
		{
			UE_LOG(LogMobiusEditor, Display, TEXT("Datasmith override materials generated successfully."));
		}
		else
		{
			UE_LOG(LogMobiusEditor, Error, TEXT("Failed to generate Datasmith override materials. Check log for details."));
		}
	}
};

IMPLEMENT_MODULE(FMobiusEditorModule, MobiusEditor)
