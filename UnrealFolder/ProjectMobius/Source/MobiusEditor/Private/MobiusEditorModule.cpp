// MobiusEditor module implementation.
// Auto-generates Datasmith override materials on editor startup if missing.

#include "Modules/ModuleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Commandlets/GenerateDatasmithMaterialsCommandlet.h"

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
	void OnAssetRegistryReady()
	{
		static const FString TestAsset = TEXT("/Game/01_Dev/RuntimeMeshGenerator/RuntimeDatasmithOverrides/MI_Opaque");

		if (UEditorAssetLibrary::DoesAssetExist(TestAsset))
		{
			UE_LOG(LogMobiusEditor, Display, TEXT("Datasmith override materials already exist — skipping generation."));
			return;
		}

		UE_LOG(LogMobiusEditor, Warning, TEXT("Datasmith override materials not found — auto-generating from engine sources..."));

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
