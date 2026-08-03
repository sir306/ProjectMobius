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
#include "Blueprint/UserWidget.h"
#include "UI/Theme/MobiusThemedUserWidget.h"  // A5: event-driven theming base
#include "ThemeToggleWidget.generated.h"

class UButtonWithText;
class UCheckBox;
class UMobiusThemedBorder;
class UPanelWidget;

/**
 * Light/dark theme control for the settings (cog) panel. All logic is native so the widget blueprint
 * only needs the label row — no graph wiring.
 *
 * A20 (2026-08-03): this is a two-segment `Light | Dark` control, not a checkbox pill. The mockup
 * (Full UI Mockup 1b, the gear popup) is a hairline-bordered container split into two equal segments,
 * the ACTIVE one filled with the theme accent under a white SemiBold label and the inactive one
 * transparent over the container surface under a normal-weight muted label — the same active/inactive
 * language as the ribbon tabs. Every colour was derived from the mockup's own hex values and matches a
 * declared palette role EXACTLY, so no token was invented (EMobiusPaletteRole is 1:1 with the mockup
 * CSS vars and static_asserts on its count):
 *
 *   container fill    InputBg           #2b2b2b dark   (the mockup's inactive-segment background)
 *   container hairline ButtonBorder     #5a5a5a dark
 *   segment seam      PanelDivider      #454545 dark
 *   ACTIVE fill       Accent            #5a9bd5 dark / the deeper blue in light — the per-theme accent
 *                                       difference the design plan shows IS this role in both directions
 *   ACTIVE label      white, SemiBold
 *   inactive label    TabInactiveText   #9a9a9a dark
 *
 * NOTE on the two roles the task brief named: TabActiveBg is a SURFACE grey (0.9131 light / 0.04519
 * dark), not the accent, so it cannot produce the mockup's filled segment; TabActiveText is
 * byte-identical to Accent in both themes. Accent is therefore the fill role and only the *fill*
 * diverges from the brief's parenthetical.
 *
 * WHY THE SEGMENTS ARE BUILT IN C++ RATHER THAN AUTHORED IN THE ASSET: there is no automation route to
 * add a custom-class widget to a WidgetBlueprint's tree in this project (the bridge's UMG verbs cover
 * only stock panels/text/checkbox and have no add-button verb; editor python deadlocks on load_asset —
 * memory reference_unreal_mcp_http_port). The A20 directive's actual goal was deleting two widget-NAME
 * special cases, and that lands either way: UUIThemeSubsystem::StyleCheckBoxForTheme's `*ThemeToggle*`
 * early-return is GONE, and so is the pill self-theming that used to live in ApplyMobiusTheme here.
 *
 * On construct it also re-applies a saved Light theme (deferred one tick so the full widget tree
 * exists), which is how the persisted theme choice survives restarts. That is theme PERSISTENCE, not
 * pill styling — it stays (UIThemeSubsystem.h documents this widget as its trigger).
 */
UCLASS()
class MOBIUSWIDGETS_API UThemeToggleWidget : public UMobiusThemedUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Builds the segmented control into the asset's label row BEFORE Super runs. Order is load-bearing:
	 * UMobiusThemedUserWidget::NativeConstruct runs ThemeStandardControlsInTree and then
	 * ApplyMobiusTheme, so building first means the segments exist for this widget's own theme pull, and
	 * the vestigial checkbox is already out of the tree before the standard checkbox styler could reach
	 * it. Deliberately NOT NativeOnInitialized: UUserWidget::Initialize gates that call on
	 * PlayerContext.IsValid() || bCanCallInitializedWithoutPlayerContext, neither of which is guaranteed
	 * for a widget embedded in another WBP (UserWidget.cpp), and NativeConstruct always runs.
	 */
	virtual void NativeConstruct() override;

protected:
	/** Re-pulls both segments' colours + label weights for the current theme. Sole writer for them. */
	virtual void ApplyMobiusTheme_Implementation() override;

	UFUNCTION()
	void HandleLightSegmentClicked();

	UFUNCTION()
	void HandleDarkSegmentClicked();

	/** The asset's row (label + control). Root widget of WBP_ThemeToggle; UPanelWidget so any box works. */
	UPROPERTY(BlueprintReadOnly, Category = "MobiusWidget|Theme", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ThemeToggleRow;

	/**
	 * VESTIGIAL (A20). The old checked-is-light pill. Removed from the tree on construct and never bound
	 * to anything. It is still present in WBP_ThemeToggle because no available tool can delete a widget
	 * from a WidgetBlueprint outside the designer; optional so the class compiles once it is deleted by
	 * hand. Nothing here styles it any more — that was the second of the two special cases A20 removed.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> ThemeToggleCheckBox;

private:
	/** Idempotent: constructs container + two segments + seam and parents them to ThemeToggleRow. */
	void BuildSegmentedControl();

	/**
	 * Absolute per-state write for one segment. bActive picks Accent fill + white SemiBold label;
	 * inactive is transparent at rest with ButtonHoverBg / ButtonPressedBg affordance and a
	 * TabInactiveText normal-weight label. bLeftEdge rounds the outer two corners only, so the pair
	 * reads as one pill inside the container's radius.
	 */
	void StyleSegment(UButtonWithText* Segment, bool bActive, bool bLeftEdge) const;

	/** Construct one segment and parent it to Box with an even Fill slot. Caller binds OnClicked. */
	UButtonWithText* MakeSegment(UPanelWidget* Box, FName WidgetName, const FText& Label);

	UPROPERTY(Transient)
	TObjectPtr<UMobiusThemedBorder> SegmentContainer;

	UPROPERTY(Transient)
	TObjectPtr<UButtonWithText> LightSegment;

	UPROPERTY(Transient)
	TObjectPtr<UButtonWithText> DarkSegment;
};
