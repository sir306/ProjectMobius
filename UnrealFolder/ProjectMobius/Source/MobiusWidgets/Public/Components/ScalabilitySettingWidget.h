// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EnumsAndStructs/ScalabilityEnums.h"
#include "ScalabilitySettingWidget.generated.h"

/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UScalabilitySettingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void SynchronizeProperties() override;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintCallable, Category = "Scalability Settings")
	virtual void UpdateScalabilityLevel();

	UFUNCTION(BlueprintImplementableEvent)
	TArray<FString> GetScalabilityLevelList();

public:
	/** Scalability Category - defaults to Resolution */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings")
	TEnumAsByte<EScalabilityCategories> ScalabilityCategory = ESc_GlobalIllumination;

protected:
	/** Scalability Level Enum */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings")
	TEnumAsByte<EScalabilitySettings> ScalabilityLevel;
};
