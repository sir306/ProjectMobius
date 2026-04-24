// Commandlet that generates Datasmith override materials from engine sources.

#include "Commandlets/GenerateDatasmithMaterialsCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "IAssetTools.h"
#include "ObjectTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenDatasmithMats, Log, All);

// ── Asset helpers (replaces UEditorAssetLibrary without the EditorScriptingUtilities plugin dep) ──

static bool AssetExists(const FString& PackagePath)
{
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> Found;
	AR.GetAssetsByPackageName(FName(*PackagePath), Found);
	return Found.Num() > 0;
}

static void DeleteAssetAtPath(const FString& PackagePath)
{
	const FString ObjectPath = PackagePath + TEXT(".") + FPackageName::GetShortName(PackagePath);
	UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPath);
	if (Asset)
	{
		TArray<UObject*> ToDelete = { Asset };
		ObjectTools::ForceDeleteObjects(ToDelete, /*bShowConfirmation=*/false);
	}
}

static UObject* DuplicateAssetToPath(const FString& SourcePackagePath, const FString& DestPackagePath)
{
	const FString SourceObjectPath = SourcePackagePath + TEXT(".") + FPackageName::GetShortName(SourcePackagePath);
	UObject* Source = LoadObject<UObject>(nullptr, *SourceObjectPath);
	if (!Source)
	{
		return nullptr;
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	return AssetTools.DuplicateAsset(FPackageName::GetShortName(DestPackagePath), FPackageName::GetLongPackagePath(DestPackagePath), Source);
}

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

	// Twinmotion source paths (full game content paths)
	static const FString TwinmotionSrc_Opaque      = TEXT("/Game/Twinmotion/Materials/StdOpaque/M_TMStdOpaque");
	static const FString TwinmotionSrc_Translucent  = TEXT("/Game/Twinmotion/Materials/StdTranslucent/M_StdTranslucentNEW");

	// Twinmotion target path prefix
	static const FString TwinmotionDest = TEXT("/Game/01_Dev/RuntimeMeshGenerator/DatasmithMasterMaterials/");
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

	// ── Generate Twinmotion materials (if available) ─────────────────────────

	int32 TwinmotionAssetCount = 0;
	if (IsTwinmotionContentAvailable())
	{
		if (!GenerateTwinmotionMaterials())
		{
			bAllOk = false;
		}
		else
		{
			TwinmotionAssetCount = 5;
		}
	}

	// ── Validate ───────────────────────────────────────────────────────────────

	if (bAllOk)
	{
		bAllOk = ValidateGeneratedAssets();
	}

	const int32 TotalAssets = 9 + TwinmotionAssetCount;
	if (bAllOk)
	{
		UE_LOG(LogGenDatasmithMats, Display, TEXT("=== GenerateDatasmithMaterials: SUCCESS — all %d assets generated ==="), TotalAssets);
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
	if (AssetExists(Entry.DestPath))
	{
		UE_LOG(LogGenDatasmithMats, Display, TEXT("  Deleting existing asset at %s"), *Entry.DestPath);
		DeleteAssetAtPath(Entry.DestPath);
	}

	// Step 1: Duplicate engine material to target path
	UObject* Duplicated = DuplicateAssetToPath(SourcePath, Entry.DestPath);
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
	if (AssetExists(Entry.DestPath))
	{
		UE_LOG(LogGenDatasmithMats, Display, TEXT("  Deleting existing MI at %s"), *Entry.DestPath);
		DeleteAssetAtPath(Entry.DestPath);
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

// ── Twinmotion Content Check ───────────────────────────────────────────────

bool UGenerateDatasmithMaterialsCommandlet::IsTwinmotionContentAvailable() const
{
	const FString TwinmotionDir = FPaths::ProjectContentDir() / TEXT("Twinmotion");
	const bool bExists = FPaths::DirectoryExists(TwinmotionDir);
	if (!bExists)
	{
		UE_LOG(LogGenDatasmithMats, Display, TEXT("Twinmotion content not found at %s — skipping Twinmotion material generation."), *TwinmotionDir);
	}
	return bExists;
}

// ── Generate Twinmotion Master Material ────────────────────────────────────

bool UGenerateDatasmithMaterialsCommandlet::GenerateTwinmotionMasterMaterial(const FTwinmotionMaterialEntry& Entry)
{
	UE_LOG(LogGenDatasmithMats, Display, TEXT("Generating Twinmotion: %s -> %s"), *Entry.SourcePath, *Entry.DestPath);

	// Delete any existing asset at the destination so we start clean
	if (AssetExists(Entry.DestPath))
	{
		UE_LOG(LogGenDatasmithMats, Display, TEXT("  Deleting existing asset at %s"), *Entry.DestPath);
		DeleteAssetAtPath(Entry.DestPath);
	}

	// Step 1: Duplicate Twinmotion material to target path
	UObject* Duplicated = DuplicateAssetToPath(Entry.SourcePath, Entry.DestPath);
	if (!Duplicated)
	{
		UE_LOG(LogGenDatasmithMats, Error, TEXT("  FAILED to duplicate %s to %s"), *Entry.SourcePath, *Entry.DestPath);
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

	// Step 2b: Override blend mode if specified
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

	// Step 4: Find what's currently connected to root MaterialAttributes
	// Twinmotion materials chain MaterialFunctionCall nodes directly to root,
	// unlike DatasmithRuntime materials which use MakeMaterialAttributes nodes.
	UMaterialExpression* ExistingExpression = Material->GetEditorOnlyData()->MaterialAttributes.Expression;
	if (!ExistingExpression)
	{
		UE_LOG(LogGenDatasmithMats, Error, TEXT("  No expression connected to MaterialAttributes in %s"), *Entry.SourcePath);
		return false;
	}

	const int32 ExistingOutputIndex = Material->GetEditorOnlyData()->MaterialAttributes.OutputIndex;

	UE_LOG(LogGenDatasmithMats, Display, TEXT("  Found existing root connection: %s (output %d)"), *ExistingExpression->GetName(), ExistingOutputIndex);

	// Step 5: Create a MaterialFunctionCall expression
	UMaterialExpressionMaterialFunctionCall* FuncCallNode = NewObject<UMaterialExpressionMaterialFunctionCall>(Material);
	FuncCallNode->MaterialFunction = MatFunc;
	FuncCallNode->UpdateFromFunctionResource();
	Material->GetExpressionCollection().AddExpression(FuncCallNode);

	// Position the function call node to the right of the existing expression
	FuncCallNode->MaterialExpressionEditorX = ExistingExpression->MaterialExpressionEditorX + 400;
	FuncCallNode->MaterialExpressionEditorY = ExistingExpression->MaterialExpressionEditorY;

	// Step 6: Rewire the graph
	// Wire: [ExistingExpression] -> FuncCall input -> FuncCall output -> Root.MaterialAttributes

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
		UE_LOG(LogGenDatasmithMats, Warning, TEXT("  Could not find 'Non Modified Material Input' pin — using first input (index 0)"));
		InputIdx = 0;
	}

	if (InputIdx == INDEX_NONE)
	{
		UE_LOG(LogGenDatasmithMats, Error, TEXT("  Material function %s has no inputs"), *Entry.MaterialFuncPath);
		return false;
	}

	// Connect ExistingExpression -> FuncCall input
	FuncCallNode->FunctionInputs[InputIdx].Input.Connect(ExistingOutputIndex, ExistingExpression);

	// Connect FuncCall output -> root node's MaterialAttributes
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

// ── Generate Twinmotion Materials ──────────────────────────────────────────

bool UGenerateDatasmithMaterialsCommandlet::GenerateTwinmotionMaterials()
{
	UE_LOG(LogGenDatasmithMats, Display, TEXT("--- Generating Twinmotion DatasmithMasterMaterials ---"));

	// Master materials
	const TArray<FTwinmotionMaterialEntry> TwinmotionMasters = {
		{ DatasmithMatPaths::TwinmotionSrc_Opaque,      DatasmithMatPaths::TwinmotionDest + TEXT("M_DatasmithOpaqueMasked"),  DatasmithMatPaths::MF_Opaque,      EBlendMode::BLEND_Masked },
		{ DatasmithMatPaths::TwinmotionSrc_Translucent,  DatasmithMatPaths::TwinmotionDest + TEXT("M_DatasmithTranslucent"),   DatasmithMatPaths::MF_Transparent, {} },
	};

	// Material instances
	const TArray<FMaterialInstanceEntry> TwinmotionInstances = {
		{ TEXT("MI_DatasmithOpaqueMasked"),  DatasmithMatPaths::TwinmotionDest + TEXT("M_DatasmithOpaqueMasked"),  DatasmithMatPaths::TwinmotionDest + TEXT("MI_DatasmithOpaqueMasked") },
		{ TEXT("MI_DatasmithTranslucent"),   DatasmithMatPaths::TwinmotionDest + TEXT("M_DatasmithTranslucent"),   DatasmithMatPaths::TwinmotionDest + TEXT("MI_DatasmithTranslucent") },
		{ TEXT("MI_DatasmithTranslucent"),   DatasmithMatPaths::TwinmotionDest + TEXT("M_DatasmithTranslucent"),   DatasmithMatPaths::TwinmotionDest + TEXT("WindowsGlass/MI_DatasmithTranslucent") },
	};

	bool bAllOk = true;

	for (const FTwinmotionMaterialEntry& Entry : TwinmotionMasters)
	{
		if (!GenerateTwinmotionMasterMaterial(Entry))
		{
			bAllOk = false;
		}
	}

	for (const FMaterialInstanceEntry& Entry : TwinmotionInstances)
	{
		if (!GenerateMaterialInstance(Entry))
		{
			bAllOk = false;
		}
	}

	return bAllOk;
}

// ── Validation ─────────────────────────────────────────────────────────────────

bool UGenerateDatasmithMaterialsCommandlet::ValidateGeneratedAssets()
{
	TArray<FString> ExpectedAssets = {
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

	// Conditionally add Twinmotion assets if that content is present
	if (IsTwinmotionContentAvailable())
	{
		ExpectedAssets.Append({
			DatasmithMatPaths::TwinmotionDest + TEXT("M_DatasmithOpaqueMasked"),
			DatasmithMatPaths::TwinmotionDest + TEXT("M_DatasmithTranslucent"),
			DatasmithMatPaths::TwinmotionDest + TEXT("MI_DatasmithOpaqueMasked"),
			DatasmithMatPaths::TwinmotionDest + TEXT("MI_DatasmithTranslucent"),
			DatasmithMatPaths::TwinmotionDest + TEXT("WindowsGlass/MI_DatasmithTranslucent"),
		});
	}

	bool bAllExist = true;
	for (const FString& Path : ExpectedAssets)
	{
		if (!AssetExists(Path))
		{
			UE_LOG(LogGenDatasmithMats, Error, TEXT("  Validation FAILED — missing: %s"), *Path);
			bAllExist = false;
		}
	}

	if (bAllExist)
	{
		UE_LOG(LogGenDatasmithMats, Display, TEXT("  Validation passed: all %d assets exist"), ExpectedAssets.Num());
	}

	return bAllExist;
}
