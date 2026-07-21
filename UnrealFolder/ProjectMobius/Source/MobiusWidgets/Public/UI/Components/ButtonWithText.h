// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include "CoreMinimal.h"
#include "BaseButton.h"
#include "ButtonWithText.generated.h"

class UTextBlock;
/**
 *
 */
UCLASS()
class MOBIUSWIDGETS_API UButtonWithText : public UBaseButton
{
	GENERATED_BODY()


public:
	UButtonWithText();

	/**
	 * The SynchronizeProperties function is called when the widget is constructed,
	 * this is where we can apply our custom style to the button.
	 */
	virtual void SynchronizeProperties() override;

	/**
	 * By overriding the ApplyMobiusButtonStyle method, which is called in the synchronised method in the parent,
	 * the properties of the button text can be set or modified.
	 */
	virtual void ApplyMobiusButtonStyle() override;

	virtual TSharedRef<SWidget> RebuildWidget() override;

	UFUNCTION(BlueprintCallable)
	void SetButtonWithNewText(FText NewButtonText);

	/**
	 * Re-push the label's text style to the live Slate widget. STextBlock COPIES its style at
	 * construction, so runtime changes to the shared style set (e.g. a UIThemeSubsystem light/dark
	 * switch) do not reach existing labels without this.
	 */
	void RefreshTextStyle();

	/**
	 * Q49/R4: re-land the label colour DIRECTLY on the live STextBlock. RefreshTextStyle()/SetTextStyle
	 * re-pushes the style struct but does NOT update the STextBlock's resolved ColorAndOpacity, so ribbon
	 * tab + Browse labels keep their light-mode colour after a theme flip. SetColorAndOpacity bypasses the
	 * construction-time style copy. Called from UUIThemeSubsystem::ApplyToWidget for theme-managed labels.
	 */
	void ApplyThemedLabelColor(FLinearColor Color);

	/**
	 * To change the style of the button from default to clicked to give the ribbon appearance on the widget,
	 * we can bind to the on clicked method to flip between the two style sheets */
	UFUNCTION()
	void ButtonClickedUpdateStyle();

	/** Text to be set on the button */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MobiusWidget|ButtonProperties")
	FText ButtonTextValue;

	/** Slate style for the text */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MobiusWidget|ButtonProperties")
	TObjectPtr<USlateWidgetStyleAsset> MobiusButtonTextStyle;

	/** Slate style for the button - the default startup phase */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MobiusWidget|ButtonProperties")
	TObjectPtr<USlateWidgetStyleAsset> ButtonStyleDefault;

	/** Toggle property to say if this button should switch the normal with hovered when clicked */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MobiusWidget|ButtonProperties")
	bool bShouldSwitchNormalWithHovered = true;

	/** The slate text block used inside the button */
	TSharedPtr<STextBlock> MyButtonText;
};
