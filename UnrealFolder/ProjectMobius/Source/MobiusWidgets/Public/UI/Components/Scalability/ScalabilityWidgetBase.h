// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Theme/MobiusThemedUserWidget.h"
#include "EnumsAndStructs/ScalabilityEnums.h"
#include "ScalabilityWidgetBase.generated.h"


class UButtonWithText;
/**
 * A6b (2026-07-28): base changed UUserWidget -> UMobiusThemedUserWidget. Pure C++ base insertion — the
 * WBPs still parent to the same classes, so no .uasset changes.
 *
 * 2026-08-05: the family is down to ONE live subclass, UGlobalQualitySegmentWidget
 * (WBP_ScalabilitySettingGlobal). The two others this comment used to name — UScalabilitySettingWidget
 * (WBP_ScalabilitySettingBase) and UResolutionScalabilityWidget (WBP_AdjustResolution) — were deleted
 * along with their now-retired Blueprints, because the panel rebuild replaced the five-button tier row
 * with a segmented control and a checkbox matrix.
 *
 * The A6b-6 note that used to sit here is GONE with them, and deliberately: it warned that the
 * active-tier 2px accent RING had no writer except UScalabilitySettingWidget::ApplyMobiusTheme_
 * Implementation, because UBaseButton::RefreshThemedButtonStyle early-returns on outlines wider than
 * 1.5px (A4 — a thick outline is a meaning-carrying accent, not chrome). That ring no longer exists:
 * the segmented control marks the active tier with an Accent FILL, styled by
 * UGlobalQualitySegmentWidget::RestyleSegments, and its segments clear bFollowThemePalette so
 * RefreshThemedButtonStyle cannot repaint the meaning away. The A4 early-return still stands as written;
 * it simply has nothing to protect here any more.
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
