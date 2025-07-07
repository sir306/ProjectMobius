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