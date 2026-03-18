// Commandlet that generates Datasmith override materials from engine sources.

#include "Commandlets/GenerateDatasmithMaterialsCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenDatasmithMats, Log, All);

// ── Paths ──────────────────────────────────────────────────────────────────────

namespace DatasmithMatPaths
{
	// Engine source path prefix (DatasmithRuntime plugin content)
	static const FString EngineSrc  = TEXT("/DatasmithRuntime/Materials/");

	// Project target path prefix
	static const FString GameDest   = TEXT("/Game/01_Dev/RuntimeMeshGenerator/RuntimeDatasmithOverrides/");

	// Material function paths (project-owned)
	static const FString MF_Opaque       = TEXT("/Game/01_Dev/RuntimeMeshGenerator/MF_ControlDatasmithMaterial");
	static const FString MF_Transparent  = TEXT("/Game/01_Dev/RuntimeMeshGenerator/MF_ControlDatasmithMaterialTransparency");
}

// ── Constructor ────────────────────────────────────────────────────────────────

UGenerateDatasmithMaterialsCommandlet::UGenerateDatasmithMaterialsCommandlet()
{
	IsClient  = false;
	IsEditor  = true;
	IsServer  = false;
	LogToConsole = true;
}

// ── Main ───────────────────────────────────────────────────────────────────────

int32 UGenerateDatasmithMaterialsCommandlet::Main(const FString& Params)
{
	UE_LOG(LogGenDatasmithMats, Display, TEXT("=== GenerateDatasmithMaterials: Starting ==="));

	// Master materials to generate
	const TArray<FMaterialEntry> MasterMaterials = {
		{ TEXT("M_Opaque"),                DatasmithMatPaths::GameDest + TEXT("M_Opaque"),                DatasmithMatPaths::MF_Opaque,      EBlendMode::BLEND_Masked },
		{ TEXT("M_PbrOpaque"),             DatasmithMatPaths::GameDest + TEXT("M_PbrOpaque"),             DatasmithMatPaths::MF_Opaque,      EBlendMode::BLEND_Masked },
		{ TEXT("M_PbrOpaque_2Sided"),      DatasmithMatPaths::GameDest + TEXT("M_PbrOpaque_2Sided"),      DatasmithMatPaths::MF_Opaque,      EBlendMode::BLEND_Masked },
		{ TEXT("M_Cutout"),                DatasmithMatPaths::GameDest + TEXT("M_Cutout"),                DatasmithMatPaths::MF_Opaque,      EBlendMode::BLEND_Masked },
		{ TEXT("M_Transparent"),           DatasmithMatPaths::GameDest + TEXT("M_Transparent"),           DatasmithMatPaths::MF_Transparent, {} },
		{ TEXT("M_PbrTranslucent"),        DatasmithMatPaths::GameDest + TEXT("M_PbrTranslucent"),        DatasmithMatPaths::MF_Transparent, {} },
		{ TEXT("M_PbrTranslucent_2Sided"), DatasmithMatPaths::GameDest + TEXT("M_PbrTranslucent_2Sided"), DatasmithMatPaths::MF_Transparent, {} },
	};

	// Material instances to generate
	const TArray<FMaterialInstanceEntry> MaterialInstances = {
		{ TEXT("MI_Opaque"),      DatasmithMatPaths::GameDest + TEXT("M_Opaque"),      DatasmithMatPaths::GameDest + TEXT("MI_Opaque") },
		{ TEXT("MI_Transparent"), DatasmithMatPaths::GameDest + TEXT("M_Transparent"), DatasmithMatPaths::GameDest + TEXT("MI_Transparent") },
	};

	// ── Generate master materials ──────────────────────────────────────────────

	bool bAllOk = true;

	for (const FMaterialEntry& Entry : MasterMaterials)
	{
		if (!GenerateMasterMaterial(Entry))
		{
			bAllOk = false;
		}
	}

	// ── Generate material instances ────────────────────────────────────────────

	for (const FMaterialInstanceEntry& Entry : MaterialInstances)
	{
		if (!GenerateMaterialInstance(Entry))
		{
			bAllOk = false;
		}
	}

	// ── Validate ───────────────────────────────────────────────────────────────

	if (bAllOk)
	{
		bAllOk = ValidateGeneratedAssets();
	}

	if (bAllOk)
	{
		UE_LOG(LogGenDatasmithMats, Display, TEXT("=== GenerateDatasmithMaterials: SUCCESS — all 9 assets generated ==="));
	}
	else
	{
		UE_LOG(LogGenDatasmithMats, Error, TEXT("=== GenerateDatasmithMaterials: FAILED — see errors above ==="));
	}

	return bAllOk ? 0 : 1;
}

// ── Generate Master Material ───────────────────────────────────────────────────

bool UGenerateDatasmithMaterialsCommandlet::GenerateMasterMaterial(const FMaterialEntry& Entry)
{
	const FString SourcePath = DatasmithMatPaths::EngineSrc + Entry.SourcePath;

	UE_LOG(LogGenDatasmithMats, Display, TEXT("Generating: %s -> %s"), *SourcePath, *Entry.DestPath);

	// Delete any existing asset at the destination so we start clean
	if (UEditorAssetLibrary::DoesAssetExist(Entry.DestPath))
	{
		UE_LOG(LogGenDatasmithMats, Display, TEXT("  Deleting existing asset at %s"), *Entry.DestPath);
		UEditorAssetLibrary::DeleteAsset(Entry.DestPath);
	}

	// Step 1: Duplicate engine material to target path
	UObject* Duplicated = UEditorAssetLibrary::DuplicateAsset(SourcePath, Entry.DestPath);
	if (!Duplicated)
	{
		UE_LOG(LogGenDatasmithMats, Error, TEXT("  FAILED to duplicate %s to %s"), *SourcePath, *Entry.DestPath);
		return false;
	}

	UMaterial* Material = Cast<UMaterial>(Duplicated);
	if (!Material)
	{
		UE_LOG(LogGenDatasmithMats, Error, TEXT("  Duplicated asset is not a UMaterial: %s"), *Entry.DestPath);
		return false;
	}

	// Step 2: Ensure bUseMaterialAttributes is true
	Material->bUseMaterialAttributes = true;

	// Step 2b: Override blend mode if specified (e.g. Masked for box cutout support)
	if (Entry.BlendModeOverride.IsSet())
	{
		Material->BlendMode = Entry.BlendModeOverride.GetValue();
		UE_LOG(LogGenDatasmithMats, Display, TEXT("  Set BlendMode to %d"), (int32)Material->BlendMode);
	}

	// Step 3: Load the material function
	UMaterialFunction* MatFunc = LoadObject<UMaterialFunction>(nullptr, *Entry.MaterialFuncPath);
	if (!MatFunc)
	{
		UE_LOG(LogGenDatasmithMats, Error, TEXT("  FAILED to load material function: %s"), *Entry.MaterialFuncPath);
		return false;
	}

	// Step 4: Find the existing MakeMaterialAttributes node
	UMaterialExpressionMakeMaterialAttributes* MakeAttribNode = nullptr;
	for (UMaterialExpression* Expr : Material->GetExpressions())
	{
		if (UMaterialExpressionMakeMaterialAttributes* MakeAttrib = Cast<UMaterialExpressionMakeMaterialAttributes>(Expr))
		{
			MakeAttribNode = MakeAttrib;
			break;
		}
	}

	if (!MakeAttribNode)
	{
		UE_LOG(LogGenDatasmithMats, Error, TEXT("  Could not find MakeMaterialAttributes node in %s"), *Entry.SourcePath);
		return false;
	}

	// Step 5: Create a MaterialFunctionCall expression
	UMaterialExpressionMaterialFunctionCall* FuncCallNode = NewObject<UMaterialExpressionMaterialFunctionCall>(Material);
	FuncCallNode->MaterialFunction = MatFunc;
	FuncCallNode->UpdateFromFunctionResource();
	Material->GetExpressionCollection().AddExpression(FuncCallNode);

	// Position the function call node to the right of MakeMaterialAttributes
	FuncCallNode->MaterialExpressionEditorX = MakeAttribNode->MaterialExpressionEditorX + 400;
	FuncCallNode->MaterialExpressionEditorY = MakeAttribNode->MaterialExpressionEditorY;

	// Step 6: Rewire the graph
	// The MakeMaterialAttributes output was connected to the root node's MaterialAttributes input.
	// We need to:
	//   a) Connect MakeMaterialAttributes output -> FuncCall "Non Modified Material Input" input
	//   b) Connect FuncCall "Result" output -> root node MaterialAttributes input

	// Find the function call's input pin named "Non Modified Material Input"
	int32 InputIdx = INDEX_NONE;
	for (int32 i = 0; i < FuncCallNode->FunctionInputs.Num(); ++i)
	{
		if (FuncCallNode->FunctionInputs[i].ExpressionInput.Get()->InputName == TEXT("Non Modified Material Input") ||
			FuncCallNode->FunctionInputs[i].Input.InputName == TEXT("Non Modified Material Input"))
		{
			InputIdx = i;
			break;
		}
	}

	if (InputIdx == INDEX_NONE)
	{
		// Try matching by partial name as a fallback
		for (int32 i = 0; i < FuncCallNode->FunctionInputs.Num(); ++i)
		{
			const FString& Name = FuncCallNode->FunctionInputs[i].ExpressionInput.Get()->InputName.ToString();
			if (Name.Contains(TEXT("Non Modified")) || Name.Contains(TEXT("Material Input")))
			{
				InputIdx = i;
				break;
			}
		}
	}

	if (InputIdx == INDEX_NONE && FuncCallNode->FunctionInputs.Num() > 0)
	{
		// Last resort: use the first input
		UE_LOG(LogGenDatasmithMats, Warning, TEXT("  Could not find 'Non Modified Material Input' pin — using first input (index 0)"));
		InputIdx = 0;
	}

	if (InputIdx == INDEX_NONE)
	{
		UE_LOG(LogGenDatasmithMats, Error, TEXT("  Material function %s has no inputs"), *Entry.MaterialFuncPath);
		return false;
	}

	// Connect MakeMaterialAttributes -> FuncCall input
	FuncCallNode->FunctionInputs[InputIdx].Input.Connect(0, MakeAttribNode);

	// Connect FuncCall output -> root node's MaterialAttributes
	// The MaterialAttributes property uses FExpressionInput
	Material->GetEditorOnlyData()->MaterialAttributes.Connect(0, FuncCallNode);

	// Step 7: Recompile and save
	UMaterialEditingLibrary::RecompileMaterial(Material);

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();

	UPackage* Package = Material->GetOutermost();
	const FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
	const FSavePackageResultStruct Result = UPackage::Save(Package, Material, *PackageFilename, SaveArgs);

	if (!Result.IsSuccessful())
	{
		UE_LOG(LogGenDatasmithMats, Error, TEXT("  FAILED to save package: %s"), *PackageFilename);
		return false;
	}

	UE_LOG(LogGenDatasmithMats, Display, TEXT("  OK: %s"), *Entry.DestPath);
	return true;
}

// ── Generate Material Instance ─────────────────────────────────────────────────

bool UGenerateDatasmithMaterialsCommandlet::GenerateMaterialInstance(const FMaterialInstanceEntry& Entry)
{
	UE_LOG(LogGenDatasmithMats, Display, TEXT("Generating MI: %s (parent: %s)"), *Entry.DestPath, *Entry.ParentPath);

	// Delete any existing asset at the destination
	if (UEditorAssetLibrary::DoesAssetExist(Entry.DestPath))
	{
		UE_LOG(LogGenDatasmithMats, Display, TEXT("  Deleting existing MI at %s"), *Entry.DestPath);
		UEditorAssetLibrary::DeleteAsset(Entry.DestPath);
	}

	// Load the parent material
	UMaterial* ParentMaterial = LoadObject<UMaterial>(nullptr, *Entry.ParentPath);
	if (!ParentMaterial)
	{
		UE_LOG(LogGenDatasmithMats, Error, TEXT("  FAILED to load parent material: %s"), *Entry.ParentPath);
		return false;
	}

	// Create the material instance constant
	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = ParentMaterial;

	const FString PackagePath = FPackageName::GetLongPackagePath(Entry.DestPath);
	const FString AssetName = FPackageName::GetShortName(Entry.DestPath);

	UPackage* Package = CreatePackage(*Entry.DestPath);
	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(
		Factory->FactoryCreateNew(UMaterialInstanceConstant::StaticClass(), Package, *AssetName, RF_Standalone | RF_Public, nullptr, GWarn));

	if (!MIC)
	{
		UE_LOG(LogGenDatasmithMats, Error, TEXT("  FAILED to create MaterialInstanceConstant: %s"), *Entry.DestPath);
		return false;
	}

	MIC->SetParentEditorOnly(ParentMaterial);

	// Save
	MIC->PreEditChange(nullptr);
	MIC->PostEditChange();
	MIC->MarkPackageDirty();

	const FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
	const FSavePackageResultStruct Result = UPackage::Save(Package, MIC, *PackageFilename, SaveArgs);

	if (!Result.IsSuccessful())
	{
		UE_LOG(LogGenDatasmithMats, Error, TEXT("  FAILED to save MI package: %s"), *PackageFilename);
		return false;
	}

	UE_LOG(LogGenDatasmithMats, Display, TEXT("  OK: %s"), *Entry.DestPath);
	return true;
}

// ── Validation ─────────────────────────────────────────────────────────────────

bool UGenerateDatasmithMaterialsCommandlet::ValidateGeneratedAssets()
{
	const TArray<FString> ExpectedAssets = {
		DatasmithMatPaths::GameDest + TEXT("M_Opaque"),
		DatasmithMatPaths::GameDest + TEXT("M_PbrOpaque"),
		DatasmithMatPaths::GameDest + TEXT("M_PbrOpaque_2Sided"),
		DatasmithMatPaths::GameDest + TEXT("M_Cutout"),
		DatasmithMatPaths::GameDest + TEXT("M_Transparent"),
		DatasmithMatPaths::GameDest + TEXT("M_PbrTranslucent"),
		DatasmithMatPaths::GameDest + TEXT("M_PbrTranslucent_2Sided"),
		DatasmithMatPaths::GameDest + TEXT("MI_Opaque"),
		DatasmithMatPaths::GameDest + TEXT("MI_Transparent"),
	};

	bool bAllExist = true;
	for (const FString& Path : ExpectedAssets)
	{
		if (!UEditorAssetLibrary::DoesAssetExist(Path))
		{
			UE_LOG(LogGenDatasmithMats, Error, TEXT("  Validation FAILED — missing: %s"), *Path);
			bAllExist = false;
		}
	}

	if (bAllExist)
	{
		UE_LOG(LogGenDatasmithMats, Display, TEXT("  Validation passed: all 9 assets exist"));
	}

	return bAllExist;
}
