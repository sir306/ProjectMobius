// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "PedestrianTrait.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UPedestrianTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()
protected:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;

	// Variables related to this trait
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrian")
	class UDataTable* PedestrianData;
};
