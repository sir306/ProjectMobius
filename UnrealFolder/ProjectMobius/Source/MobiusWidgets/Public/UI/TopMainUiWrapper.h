// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Theme/MobiusThemedUserWidget.h"  // A5: event-driven control theming base
#include "TopMainUiWrapper.generated.h"

class UImprovedLoadingNotifyWidget;
class UMobiusSettingPanel;
class ULoadingNotifyWidget;
class UErrorWindowWidget;
/**
 * 
 */
UCLASS()
// A5 (2026-07-28): base changed UUserWidget -> UMobiusThemedUserWidget so this widget themes its own
// standard controls on construct + every OnThemeChanged (see UUIThemeSubsystem::ThemeStandardControlsInTree)
// instead of waiting for the value walk to find them. Pure C++ base insertion - the WBP still parents to
// this class, so no .uasset changes.
class MOBIUSWIDGETS_API UTopMainUiWrapper : public UMobiusThemedUserWidget
{
	GENERATED_BODY()
#pragma region INHERITED_METHODS
public:
	virtual void SynchronizeProperties() override;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

#pragma endregion INHERITED_METHODS

public:
#pragma region PUBLIC_METHODS
#pragma endregion PUBLIC_METHODS

private:
#pragma region PRIVATE_METHODS
#pragma endregion PRIVATE_METHODS

public:
#pragma region PUBLIC_COMPONENTS
	/** */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	//TObjectPtr<UMobiusSettingPanel> MobiusSettingPanel;
	
	/** */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<UImprovedLoadingNotifyWidget> LoadingNotifyWidget;
	
#pragma endregion PUBLIC_COMPONENTS
	
private:
#pragma region PRIVATE_COMPONENTS
	UPROPERTY(Transient)
	TObjectPtr<UErrorWindowWidget> ErrorWindowWidget;
#pragma endregion PRIVATE_COMPONENTS
	
};
