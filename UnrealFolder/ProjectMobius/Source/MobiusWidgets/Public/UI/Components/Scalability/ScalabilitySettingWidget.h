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
	 * A6b (2026-07-28): re-land the active-tier chip on a light/dark switch. The five tier buttons take
	 * their fill from the SWS asset (ScalabilityButtonStyle = SWS_PanelButtonStyle), which
	 * ApplySharedStyles retints per theme — but the LIVE button styles were snapshotted at the previous
	 * theme, and A4's palette re-stamp skips the active one because of its 2px accent ring. Re-running the
	 * existing apply is enough: its CheckButtonStyle early-out compares the live Normal brush against the
	 * freshly-themed asset, so a theme flip always fails the check and re-applies, while a redundant call
	 * in the same theme is a no-op. Replaces what ApplyToLiveWidgets was doing for this widget.
	 *
	 * NOTE for WBP_ScalabilitySettingGlobal: it parents to UScalabilityWidgetBase, NOT to this class, and
	 * carries its OWN ApplyButtonStyleForActiveSetting BP graph (the one that swaps in
	 * SWS_ScaleabilityButtonCurrentSet). It needs its own ApplyMobiusTheme event in the Blueprint —
	 * this override does not reach it.
	 */
	virtual void ApplyMobiusTheme_Implementation() override;

	UFUNCTION(BlueprintCallable, Category = "Scalability Settings")
	void UpdateScalabilityAndButtonStyle(EScalabilitySettings NewSetting);

public:
	/** Button Style for this widget options */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings Style")
	TObjectPtr<USlateWidgetStyleAsset> ScalabilityButtonStyle;

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