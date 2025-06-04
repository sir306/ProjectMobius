// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "MassEntityTypes.h"
#include "TimeDilationTrait.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UTimeDilationTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()
	
protected:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;

	//uint32 TimeStepFragmentHash;
	// Variables related to this trait -- this is a placeholder for now but could be time dilation factor etc.
	
};
