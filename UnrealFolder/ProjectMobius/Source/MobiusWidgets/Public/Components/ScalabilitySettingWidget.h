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
	void UpdateScalabilityLevel();

	UFUNCTION(BlueprintImplementableEvent)
	TArray<FString> GetScalabilityLevelList();

public:
	/** Scalability Level Enum - defaults to Epic */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings")
	TEnumAsByte<EScalabilitySettings> ScalabilityLevel = ESsl_Epic;
	
};
