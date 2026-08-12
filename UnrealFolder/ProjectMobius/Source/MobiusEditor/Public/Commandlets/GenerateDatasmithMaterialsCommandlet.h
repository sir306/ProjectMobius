// Commandlet that generates Datasmith override materials from engine sources.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "Engine/EngineTypes.h"
#include "GenerateDatasmithMaterialsCommandlet.generated.h"

/**
 * Generates the RuntimeDatasmithOverrides materials by duplicating engine
 * DatasmithRuntime materials and injecting project-owned material functions.
 *
 * Run via:
 *   UnrealEditor-Cmd.exe ProjectMobius.uproject -run=GenerateDatasmithMaterials
 */
UCLASS()
class UGenerateDatasmithMaterialsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGenerateDatasmithMaterialsCommandlet();

	//~ UCommandlet interface
	virtual int32 Main(const FString& Params) override;

	/** True when Content/Twinmotion/ is present, so the Twinmotion-derived overrides are expected too. */
	static bool IsTwinmotionContentPresent();

	/** Every package path this commandlet is expected to produce, for the current Twinmotion state. */
	static TArray<FString> GetExpectedAssetPaths();

	/**
	 * The expected package paths that are absent on disk.
	 *
	 * Answered from the file system, never from the asset registry: a commandlet or -unattended
	 * editor never runs the initial registry scan, so a registry query there reports every asset
	 * in the project as missing.
	 */
	static TArray<FString> GetMissingAssetPaths();

private:
	/** Info about one master material to generate. */
	struct FMaterialEntry
	{
		FString SourcePath;       // Engine plugin content path
		FString DestPath;         // Game content path
		FString MaterialFuncPath; // Material function to inject
		TOptional<EBlendMode> BlendModeOverride; // Override blend mode after duplication
	};

	/** Info about one material instance to generate. */
	struct FMaterialInstanceEntry
	{
		FString Name;
		FString ParentPath; // Game content path of the parent master material
		FString DestPath;   // Game content path for the MI
	};

	/** Info about one Twinmotion-based master material to generate. */
	struct FTwinmotionMaterialEntry
	{
		FString SourcePath;       // Full game content path (e.g. /Game/Twinmotion/...)
		FString DestPath;         // Game content path for output
		FString MaterialFuncPath; // Material function to inject
		TOptional<EBlendMode> BlendModeOverride;
	};

	/** Duplicate engine material to target path and inject a material function. */
	bool GenerateMasterMaterial(const FMaterialEntry& Entry);

	/** Duplicate Twinmotion material to target path and inject a material function. */
	bool GenerateTwinmotionMasterMaterial(const FTwinmotionMaterialEntry& Entry);

	/** Generate all Twinmotion-based DatasmithMasterMaterials (if Twinmotion content is present). */
	bool GenerateTwinmotionMaterials();

	/** Check whether Content/Twinmotion/ directory exists. */
	bool IsTwinmotionContentAvailable() const;

	/** Create a material instance constant with the given parent. */
	bool GenerateMaterialInstance(const FMaterialInstanceEntry& Entry);

	/** Validate that all expected assets exist. */
	bool ValidateGeneratedAssets();
};
