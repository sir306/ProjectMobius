// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Theme/MobiusThemedUserWidget.h"
#include "EnumsAndStructs/ScalabilityEnums.h"
#include "ScalabilityWidgetBase.generated.h"


class UButtonWithText;
/**
 * A6b (2026-07-28): base changed UUserWidget -> UMobiusThemedUserWidget. Pure C++ base insertion — the
 * WBPs still parent to the same classes, so no .uasset changes. One reparent here covers all three
 * scalability widget families at once: UScalabilitySettingWidget (WBP_ScalabilitySettingBase),
 * UResolutionScalabilityWidget (WBP_AdjustResolution) and WBP_ScalabilitySettingGlobal, which parents
 * straight to this class.
 *
 * WHY it is needed before the walker dies: the active-tier "current setting" chip is signalled by a 2px
 * accent RING, and UBaseButton::RefreshThemedButtonStyle deliberately early-returns on any outline wider
 * than 1.5px (A4 — a thick outline is a meaning-carrying accent, not chrome). So nothing in the A4 event
 * path re-lands that chip, and ApplyToLiveWidgets is the only thing doing it today. Deleting the walker
 * without this leaves the active tier wearing the PREVIOUS theme's fill and ring after every toggle.
 */
UCLASS()
class MOBIUSWIDGETS_API UScalabilityWidgetBase : public UMobiusThemedUserWidget
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
