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

	/** Duplicate engine material to target path and inject a material function. */
	bool GenerateMasterMaterial(const FMaterialEntry& Entry);

	/** Create a material instance constant with the given parent. */
	bool GenerateMaterialInstance(const FMaterialInstanceEntry& Entry);

	/** Validate that all expected assets exist. */
	bool ValidateGeneratedAssets();
};
