// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassAITags.generated.h"

/**
 * Tags for time step
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FMassAITimeStepTag : public FMassTag
{
	GENERATED_BODY()
};

/**
* Tags for entity representation
*/
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FMassEntityRepresentationTag : public FMassTag
{
	GENERATED_BODY()
};

/**
* Tag to mark an entity for deletion
*/
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FMassEntityDeleteTag : public FMassTag
{
	GENERATED_BODY()
};