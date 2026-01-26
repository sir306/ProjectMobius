// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EnumsAndStructs/ScalabilityEnums.h"
#include "ScalabilityWidgetBase.generated.h"


class UButtonWithText;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UScalabilityWidgetBase : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual void SynchronizeProperties() override;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintCallable, Category = "Scalability Settings")
	virtual void InitializeScalabilityLevel();

	UFUNCTION(BlueprintCallable, Category = "Scalability Settings")
	virtual void UpdateScalabilityLevel();

	UFUNCTION(BlueprintImplementableEvent)
	TArray<FString> GetScalabilityLevelList();

public:
	/** Scalability Category - defaults to Resolution */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings")
	TEnumAsByte<EScalabilityCategories> ScalabilityCategory = ESc_GlobalIllumination;

	
private:
	/** Scalability Level Enum */
	UPROPERTY(EditAnywhere, Category = "Scalability Settings")
	TEnumAsByte<EScalabilitySettings> ScalabilityLevel = EScalabilitySettings::ESsl_Default;


public:
	/**
	 * Setter for Scalability Level
	 */
	UFUNCTION(BlueprintCallable, Category = "Scalability Settings")
	FORCEINLINE void SetScalabilityLevel(const TEnumAsByte<EScalabilitySettings> NewScalabilityLevel) { ScalabilityLevel = NewScalabilityLevel; }

	/**
	 * Getter for Scalability Level
	 * 
	 * @return The current scalability level as an EScalabilitySettings enum value.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scalability Settings")
	FORCEINLINE TEnumAsByte<EScalabilitySettings> GetScalabilityLevel() const { return ScalabilityLevel; }
};
