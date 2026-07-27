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
#include "Components/Button.h"
#include "BaseButton.generated.h"

class UUIThemeSubsystem;

/**
 * To apply our custom style to the button, we need to create a new class that inherits from UButton,
 * this is to reduce some boilerplate code that we would have to write if we were to create a new button for each widget.
 */
UCLASS()
class MOBIUSWIDGETS_API UBaseButton : public UButton
{
	GENERATED_BODY()

public:
	/**
	 * The SynchronizeProperties function is called when the widget is constructed,
	 * this is where we can apply our custom style to the button.
	 */
	virtual void SynchronizeProperties() override;

	/**
	 * Method to apply the custom style to the button.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Style")
	virtual void ApplyMobiusButtonStyle();

	/**
	 * Injects the click-path diagnostics (see Diagnostics/MobiusClickLog.h) into EVERY Mobius button.
	 * Bound here rather than in SynchronizeProperties because this fires once per Slate rebuild, and a
	 * rebuild is itself evidence in the unresponsive-button investigation: an SButton rebuilt between
	 * press and release drops mouse capture, which reads to the user as "the click did nothing".
	 * All logging is gated on `Mobius.LogClicks` and skipped at design time.
	 */
	virtual void OnWidgetRebuilt() override;

	/**
	 * Re-stamp the LIVE button style's state colours from the theme palette: Normal = ButtonBg,
	 * Hovered = ButtonHoverBg, Pressed = ButtonPressedBg, Disabled = ButtonBg, 1px outline =
	 * ButtonBorder, all four foregrounds = ButtonText. Geometry (draw type, corner radii, outline
	 * WIDTH, padding, sound) is left exactly as the SWS asset / shared style authored it — a material
	 * on a RoundedBox brush loses the corner mask and the outline, so this recolours flat brushes
	 * rather than swapping in materials (owner ruling).
	 *
	 * Called from construct (via ApplyMobiusButtonStyle) and from OnThemeChanged — never from
	 * hover/press. Re-styling mid-press makes SButton drop mouse capture, which reads to the user as
	 * "the click did nothing" (see A15 in the pending-task list).
	 *
	 * Skips, by design:
	 *  - brushes carrying a ResourceObject (image/material art: the playbar play/pause MIDs, the VR
	 *    button MIs) — the asset owns those pixels,
	 *  - styles whose outline is WIDER than the shared 1px chrome line: a thick outline is a MEANINGFUL
	 *    accent ring (the scalability "current tier" chip is 2px accent blue), not themeable chrome,
	 *  - ribbon tabs (UButtonWithText::bIsRibbonButton), which self-theme via ApplyRibbonTabStyle.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Theme")
	void RefreshThemedButtonStyle();

	/**
	 * CLICK-RELIABILITY FIX (2026-07-27, owner-diagnosed). Slate treats a button's pressed padding as
	 * LAYOUT, not decoration: `SButton::GetCombinedPadding` (SButton.cpp:159-164) returns
	 * ContentPadding + (IsPressed ? Style->PressedPadding : Style->NormalPadding), so a smaller pressed
	 * padding SHRINKS the widget the instant you press it. If the cursor sat within that delta of the
	 * edge, the retreating edge fires OnMouseLeave (which calls Release(), SButton.cpp:449) and the
	 * following OnMouseButtonUp sees `bEventOverButton = IsHovered()` == false (SButton.cpp:374) — so the
	 * click is silently dropped even though the button visibly hovered and visibly depressed.
	 *
	 * `SWS_PanelButtonStyle` authored Normal 8,4,8,4 → Pressed 6,2,6,2 = 2px in on every side, which is
	 * exactly the dead band the owner measured across the flow-counter / load-data / scalability buttons.
	 *
	 * This keeps the press FEEL but removes the size change: the pressed margin is redistributed to the
	 * same totals (content nudges 1px down) instead of shrinking, so the hit rect never moves. Runs for
	 * EVERY Mobius button — click reliability is not a theming concern, so it is deliberately outside
	 * ShouldFollowThemePalette() and outside the accent-ring early-out.
	 */
	void StabilisePressedPadding();

	/** The Style asset for the button — supplies GEOMETRY (draw type, corner radii, outline width,
	 *  padding, sound). Colours come from the theme palette when bFollowThemePalette is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Style")
	TObjectPtr<USlateWidgetStyleAsset> SlateButtonStyle;

	/** Set: the button's fill/hover/pressed/outline/foreground colours are re-stamped from the theme
	 *  palette on construct and on every theme change (the SWS asset still supplies geometry).
	 *  Clear: colours are left exactly as the style asset authored them — use this only for a button
	 *  whose owner drives its state colours itself, so the re-stamp cannot repaint that meaning away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Theme")
	bool bFollowThemePalette = true;

protected:
	/** Gate for RefreshThemedButtonStyle. Overridden by UButtonWithText to exclude ribbon tabs. */
	virtual bool ShouldFollowThemePalette() const;

	/** Cache + return the theme subsystem (runtime only — none at design time). */
	UUIThemeSubsystem* ResolveThemeSubsystem();

	/** Bound to UUIThemeSubsystem::OnThemeChanged — event-driven replacement for the value-matching walk.
	 *  Virtual so a subclass with its own themed look (ribbon tabs) extends rather than re-binds. */
	UFUNCTION()
	virtual void HandleThemeChanged();

	virtual void BeginDestroy() override;

	/** Weak so a torn-down game instance cannot keep the subsystem alive through this button. */
	TWeakObjectPtr<UUIThemeSubsystem> CachedThemeSubsystem;

	/** Click-log handlers bound to UButton's OnPressed / OnReleased / OnClicked. */
	UFUNCTION()
	void HandleClickLogPressed();

	UFUNCTION()
	void HandleClickLogReleased();

	UFUNCTION()
	void HandleClickLogClicked();

	/** "<ButtonName> in <OwningWidgetClass>" — identifies the button in a log line. */
	FString GetClickLogLabel() const;
};
