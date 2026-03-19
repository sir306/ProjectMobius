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
		static const FString RuntimeTestAsset = TEXT("/Game/01_Dev/RuntimeMeshGenerator/RuntimeDatasmithOverrides/MI_Opaque");
		static const FString TwinmotionTestAsset = TEXT("/Game/01_Dev/RuntimeMeshGenerator/DatasmithMasterMaterials/MI_DatasmithOpaqueMasked");
		const FString TwinmotionContentDir = FPaths::ProjectContentDir() / TEXT("Twinmotion");

		const bool bRuntimeExists = UEditorAssetLibrary::DoesAssetExist(RuntimeTestAsset);
		const bool bTwinmotionNeeded = FPaths::DirectoryExists(TwinmotionContentDir) && !UEditorAssetLibrary::DoesAssetExist(TwinmotionTestAsset);

		if (bRuntimeExists && !bTwinmotionNeeded)
		{
			UE_LOG(LogMobiusEditor, Display, TEXT("Datasmith override materials already exist — skipping generation."));
			return;
		}

		if (!bRuntimeExists)
		{
			UE_LOG(LogMobiusEditor, Warning, TEXT("Datasmith override materials not found — auto-generating..."));
		}
		else if (bTwinmotionNeeded)
		{
			UE_LOG(LogMobiusEditor, Warning, TEXT("Twinmotion DatasmithMasterMaterials not found — auto-generating..."));
		}

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
