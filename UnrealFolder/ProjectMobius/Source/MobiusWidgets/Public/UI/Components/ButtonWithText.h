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
class UUIThemeSubsystem;
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
	 * construction-time style copy. Its former caller (the theme walk's per-label handling) was disabled by
	 * the W2 label experiment and deleted with the walk in A6b-6 — labels follow the button STYLE foreground
	 * now. Kept because the ribbon tab path still needs a direct writer (UIThemeSubsystem's tab styling).
	 */
	void ApplyThemedLabelColor(FLinearColor Color);

	/**
	 * A5 (2026-07-28): the LABEL half of the SWS retirement — A4 moved the button's brushes and foregrounds
	 * onto the palette and explicitly left MobiusButtonTextStyle behind. Re-pushes the label's text style
	 * (font face/size stay asset-owned, per the owner's geometry-vs-colour split) and then sets the label
	 * COLOUR explicitly, so the label no longer needs UUIThemeSubsystem::ApplySharedStyles to mutate the
	 * shared SWS_*TextStyle asset in place on every theme apply — the thing A6 deletes.
	 *
	 * Colour rule: the ButtonText role, UNLESS the assigned text style carries a SATURATED authored colour,
	 * which means the label is a deliberate SIGNAL rather than chrome — then it takes the DangerText role.
	 * The asset is only the detector; the value is the palette's, so the destructive red is theme-correct in
	 * both directions instead of the single authored red that only has contrast on a dark button. Tested by
	 * saturation, not by asset name, because §6c warns that the name-matched branches in ApplySharedStyles
	 * die silently on a rename. Destructive = red TEXT on the normal surface, not a red fill (owner, 07-28).
	 *
	 * Ribbon tabs are excluded — ApplyRibbonTabStyle owns their label colour (active vs inactive accent).
	 * So are buttons with bFollowThemePalette cleared (A20): that flag means the OWNER drives this button's
	 * colours, and it has to cover the label as well as the brushes or the owner's write is overwritten on
	 * every theme change. Note this also skips RefreshTextStyle(), so such a button keeps an owner-set FONT.
	 */
	void RefreshThemedLabelStyle();

	// -------- Ribbon-tab self-theming (W2, 2026-07-21) --------
	// A ribbon tab drives its OWN look from the UIThemeSubsystem instead of the old split (SWS snapshot in
	// construct + BP SetStyle on activation + walk re-landing the label) that fought itself and left the
	// tab text invisible. Set bIsRibbonButton in the designer; drive bIsActiveTab from the ribbon BP (its
	// BlueprintSetter re-applies the themed style + label colour). No manual SetStyle in the BP needed.

	/** Re-pull + apply this ribbon button's themed tab style + label colour from the UIThemeSubsystem for
	 *  the current bIsActiveTab. No-op unless bIsRibbonButton and the subsystem is available. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Ribbon")
	void RefreshRibbonAppearance();

	UFUNCTION(BlueprintSetter)
	void SetIsActiveTab(bool bNewActive);

	UFUNCTION(BlueprintGetter)
	bool GetIsActiveTab() const { return bIsActiveTab; }

	/**
	 * To change the style of the button from default to clicked to give the ribbon appearance on the widget,
	 * we can bind to the on clicked method to flip between the two style sheets */
	UFUNCTION()
	void ButtonClickedUpdateStyle();

	/** Marks this button as a ribbon tab: self-themes from the UIThemeSubsystem (GetThemedTabStyle + tab
	 *  text palette) instead of the SWS snapshot, and re-themes on OnThemeChanged. Set in the designer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Ribbon")
	bool bIsRibbonButton = false;

	/** Active/selected state of a ribbon tab. Setting it re-applies the themed tab style + label colour
	 *  (BlueprintSetter -> SetIsActiveTab), so the ribbon BP just sets this instead of calling SetStyle. */
	UPROPERTY(EditAnywhere, BlueprintGetter = GetIsActiveTab, BlueprintSetter = SetIsActiveTab, Category = "Mobius|Ribbon")
	bool bIsActiveTab = false;

	/** Ribbon tabs only: put the active-accent on the RIGHT edge (vertical side tool-rail, WBP_ToolPanel)
	 *  instead of the bottom underline (top ribbon). Selects the MI_Tab*Right material variants via
	 *  UIThemeSubsystem::GetThemedTabStyle. Set in the designer alongside bIsRibbonButton. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Ribbon")
	bool bRightEdgeAccent = false;

	/** Text to be set on the button */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Content")
	FText ButtonTextValue;

	/** Slate style for the LABEL. Falls back to the shared "Mobius.Text.Label" when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Style")
	TObjectPtr<USlateWidgetStyleAsset> MobiusButtonTextStyle;

	/** Slate style used as the SButton's construction-time style (RebuildWidget), distinct from
	 *  UBaseButton::SlateButtonStyle which is re-applied on every SynchronizeProperties.
	 *  NOTE (audited 2026-07-27): not set in ANY WidgetBlueprint - the only writer is
	 *  UScalabilitySettingWidget in C++. Prefer SlateButtonStyle in the designer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Mobius|Style")
	TObjectPtr<USlateWidgetStyleAsset> ButtonStyleDefault;

	/** Toggle property to say if this button should switch the normal with hovered when clicked.
	 *  NOTE: only acts through ButtonClickedUpdateStyle, whose OnClicked binding is currently commented
	 *  out (ButtonWithText.cpp), so this is inert unless a Blueprint calls that function directly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Mobius|Behaviour")
	bool bShouldSwitchNormalWithHovered = true;

	/** The slate text block used inside the button */
	TSharedPtr<STextBlock> MyButtonText;

protected:
	/** Ribbon tabs get their whole look (tab material + per-state foreground + label colour) from
	 *  ApplyRibbonTabStyle, so the base class's flat palette re-stamp must not also run on them. */
	virtual bool ShouldFollowThemePalette() const override;

	/** Extends the base handler: re-themes the ribbon tab on a light/dark switch. The subsystem bind and
	 *  the unbind live in UBaseButton (OnWidgetRebuilt / BeginDestroy) - one bind for every Mobius button. */
	virtual void HandleThemeChanged() override;
};
