// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TopMainUiWrapper.generated.h"

class UImprovedLoadingNotifyWidget;
class UMobiusSettingPanel;
class ULoadingNotifyWidget;
class UErrorWindowWidget;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UTopMainUiWrapper : public UUserWidget
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
