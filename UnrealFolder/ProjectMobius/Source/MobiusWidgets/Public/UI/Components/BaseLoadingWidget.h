// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Theme/MobiusThemedUserWidget.h"
#include "BaseLoadingWidget.generated.h"

class UImage;
class UProgressBar;
class UTextBlock;

// DELEGATES
/** Delegate to notify listeners if something is loading or finished loading */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoadingStateChanged, bool, bLoadingStateChanged);

/**
 *
 */
UCLASS()
class MOBIUSWIDGETS_API UBaseLoadingWidget : public UMobiusThemedUserWidget
{
	GENERATED_BODY()

public:
#pragma region MEHTODS


	void UpdateLoading(float NewLoadPercent);
	void UpdateLoading(bool bNewLoading);

	void UpdateLoadingText(FText& NewLoadingText);

	/**
	 * Repaint a Border whose brush is an M_WidgetBackground instance (MI_LoadingInnerBackground /
	 * MI_LoadingOuterBackground) to Fill + Outline. Those MIs bake dark colours into their "Background
	 * Color Tint" / "Border Color Tint" params, so the theme colour has to be written through a MID —
	 * SetBrushColor on its own only multiplies the baked value and can never reach a light surface.
	 * Static so the notify popup can theme its outer frame with the same rule.
	 */
	static void ThemeMaterialCard(class UBorder* Border, const FLinearColor& Fill, const FLinearColor& Outline);

#pragma endregion METHODS

protected:
	/** Born-theme: sub-text -> SublabelText, percent -> LabelText, bar -> Accent/SliderTrack, card frame MID. */
	virtual void ApplyMobiusTheme_Implementation() override;

public:
#pragma region PUBLIC_PROPERTIES
	/** Delegate to notify listeners if something is loading or finished loading */
	UPROPERTY(BlueprintAssignable, Category = "LoadingWidget|Delegates")
	FOnLoadingStateChanged OnLoadingStateChanged;

	/** Text block to show current Load Text - this will inform the user what loading action is being done */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<UTextBlock> LoadingText;

	/** Text block to show how much is loaded as a percent - this will inform the user what loading action is being done */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	TObjectPtr<UTextBlock> LoadedAmount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	TObjectPtr<UProgressBar> LoadingBar;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	TObjectPtr<UImage> LoadingInfiniteImage;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	float LoadPercent = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	bool bIsInfiniteLoadingWidget = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	bool bIsLoading = false;
#pragma endregion PUBLIC_PROPERTIES
};
