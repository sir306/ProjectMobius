// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ScalabilityWidgetBase.h"
#include "ScalabilitySettingWidget.generated.h"

class UButtonWithText;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UScalabilitySettingWidget : public UScalabilityWidgetBase
{
	GENERATED_BODY()

public:
	virtual void SynchronizeProperties() override;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;

	
	virtual void InitializeScalabilityLevel() override;


	virtual void UpdateScalabilityLevel() override;
	
	TArray<FString> GetScalabilityLevelList();

	UFUNCTION(BlueprintCallable, Category = "Scalability Settings")
	void ApplyButtonStyleForActiveSetting();

	/**
	 * A6b (2026-07-28): re-land the active-tier chip on a light/dark switch.
	 *
	 * A10 (2026-08-04) — the mechanism this comment used to describe is GONE, do not restore it. It read:
	 * "its CheckButtonStyle early-out compares the live Normal brush against the freshly-themed asset, so
	 * a theme flip always fails the check and re-applies". That coupling was load-bearing AND fragile —
	 * correct re-theming depended on a brush comparison happening to mismatch. ApplyButtonStyleForActiveSetting
	 * now swaps the style ASSET and unconditionally re-runs UBaseButton::ApplyMobiusButtonStyle on all five
	 * buttons, which is idempotent, so a theme flip re-lands by construction rather than by a failed compare.
	 *
	 * NOTE for WBP_ScalabilitySettingGlobal: it parents to UScalabilityWidgetBase, NOT to this class, and
	 * carries its OWN ApplyButtonStyleForActiveSetting BP graph (the one that swaps in
	 * SWS_ScaleabilityButtonCurrentSet). It needs its own ApplyMobiusTheme event in the Blueprint —
	 * this override does not reach it. That BP graph is the pattern the C++ path was corrected TO.
	 */
	virtual void ApplyMobiusTheme_Implementation() override;

	UFUNCTION(BlueprintCallable, Category = "Scalability Settings")
	void UpdateScalabilityAndButtonStyle(EScalabilitySettings NewSetting);

public:
	/** Button Style for this widget options — the INACTIVE tier look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings Style")
	TObjectPtr<USlateWidgetStyleAsset> ScalabilityButtonStyle;

	/**
	 * A10 (2026-08-04): the ACTIVE tier's style asset — the "current setting" chip (flat rounded box with a
	 * 2px accent ring). Defaulted to SWS_ScaleabilityButtonCurrentSet by soft path so no .uasset edit is
	 * needed; WBP_ScalabilitySettingGlobal's BP graph already swaps this same asset in, and the C++ path now
	 * matches it. Soft rather than hard so the CDO does not drag the asset into every cook that loads this
	 * class, and a literal path because UIThemeSubsystem::ApplySharedStyles already keys off this asset by
	 * NAME — the path is effectively pinned either way.
	 *
	 * If it fails to resolve, ApplyButtonStyleForActiveSetting falls back to ScalabilityButtonStyle: the
	 * active tier then reads as an ordinary button (no ring) but is still correctly themed and still clicks,
	 * which is the right failure mode for a purely decorative cue.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings Style")
	TSoftObjectPtr<USlateWidgetStyleAsset> ActiveTierButtonStyle = TSoftObjectPtr<USlateWidgetStyleAsset>(
		FSoftObjectPath(TEXT("/Game/01_Dev/Widgets/WidgetMaterials/SlateStyleSheets/UI_Styles/"
			"SWS_ScaleabilityButtonCurrentSet.SWS_ScaleabilityButtonCurrentSet")));

	/** Low setting button */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings", meta = (BindWidget))
	TObjectPtr<UButtonWithText> LowSetting_Button;

	/** Medium setting button */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings", meta = (BindWidget))
	TObjectPtr<UButtonWithText> MedSetting_Button;

	/** High setting button */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings", meta = (BindWidget))
	TObjectPtr<UButtonWithText> HighSetting_Button;

	/** Epic setting button */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings", meta = (BindWidget))
	TObjectPtr<UButtonWithText> EpicSetting_Button;

	/** Cinematic setting button */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings", meta = (BindWidget))
	TObjectPtr<UButtonWithText> CineSetting_Button;

private:
	/** Configures the buttons to switch normal style with hovered and apply's the desired style to all */
	void ConfigureButtonStyles();
};