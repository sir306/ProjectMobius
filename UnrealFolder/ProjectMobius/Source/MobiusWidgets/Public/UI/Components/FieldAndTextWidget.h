// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "FieldAndTextWidget.generated.h"

class SFieldAndTitleText;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UFieldAndTextWidget : public UWidget
{
	GENERATED_BODY()

public:
	/** Title text - defaults to Title Value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldAndTextWidget")
	FText TitleText = FText::FromString("Title Value");
	
	/** Text field - defaults to Field Value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldAndTextWidget")
	FText FieldText = FText::FromString("Field Value");

	/** Flag to determine the layout of this component ie side by side text or above below */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldAndTextWidget")
	bool bIsTitleAboveField = true;

	/** Flag to determine if it should Auto Center the text to the widget*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldAndTextWidget")
	bool bAutoCenter = false;

	/** Slate Title Text Style sheet */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldAndTextWidget")
	TObjectPtr<USlateWidgetStyleAsset> TitleTextStyle;

	/** Slate Field Text Style sheet */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldAndTextWidget")
	TObjectPtr<USlateWidgetStyleAsset> FieldTextStyle;

protected:
	TSharedPtr<SFieldAndTitleText> FieldAndTextWidget;
	
	virtual TSharedRef<SWidget> RebuildWidget() override;

public:
	virtual void SynchronizeProperties() override;
	
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	UFUNCTION(BlueprintCallable, Category = "FieldAndTextWidget")
	void SetUpdateTitleText(FText InTitleText);
	
	UFUNCTION(BlueprintCallable, Category = "FieldAndTextWidget")
	void SetUpdateFieldText(FText InFieldText);

	/**
	 * Re-land themed text colours (both = LabelText, high contrast) on the inner raw-Slate title/field
	 * blocks. The theme walk calls the (bool) overload with the theme it is applying — the no-arg version
	 * (build/sync cold-start) resolves the current theme itself. Taking the walk's theme is essential for
	 * the in-world flow-counter card: its widget-component GetWorld()/GetGameInstance() resolves an
	 * unreliable theme, so self-resolving there gave grey (dark-value) text on a light card.
	 */
	void RefreshThemedStyle();
	void RefreshThemedStyle(bool bLight);
	/**
	 * Gets the size of the text in this widget, used for layout calculations
	 * 
	 * @return The size of the field and title text combined
	 */
	FVector2D GetTextSize() const;

	/**
	 * Set the font size for both the title and field text -> Potentially we can use a float for more flexibility but int32 is more consistent with Unreal's text rendering
	 * @param[int32] InFontSize The font size to set for both texts
	 */
	void SetFontSize(float InFontSize) const;

	/**
	 * Set the typeface face of the FIELD (value) text only, on the composite Font_Inter (e.g. "Mono" for
	 * numeric/path/timecode readouts per spec §3.4). Title face is left alone. No-op until Slate is built.
	 */
	void SetFieldFontFace(FName InTypeface) const;

	// Optional getters if you want to bind attributes rather than call setters
	FText GetTitleText() const   { return TitleText; }
	FText GetFieldText() const   { return FieldText; }
	bool  GetIsTitleAboveField() const { return bIsTitleAboveField; }
	float GetFontSize();
};
