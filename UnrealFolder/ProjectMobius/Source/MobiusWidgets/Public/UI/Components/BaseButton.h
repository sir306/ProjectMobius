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
 * Which named C++ shape a Mobius button takes, replacing "whatever FButtonStyle the bound SWS asset
 * happened to carry". Declared here rather than beside the geometry structs because MobiusButtonGeometry.h
 * is a plain header with no UHT reflection, and this has to be a UPROPERTY the designer can set.
 */
UENUM(BlueprintType)
enum class EMobiusButtonGeometryFamily : uint8
{
	/**
	 * Transitional default: infer the family from the bound SlateButtonStyle asset's NAME, and leave the
	 * geometry alone entirely when nothing is bound. Exists so the C++ migration could land without
	 * editing every consuming widget in the same commit. Retire it once every button sets a family.
	 */
	FromAsset	UMETA(DisplayName = "From Asset (legacy)"),

	/** The standard Mobius button: 4px rounded box, 1px ring, 8/4 padding (MobiusButtonGeometry::Chip). */
	Panel		UMETA(DisplayName = "Panel Button"),

	/** Setting/panel tab: square at rest, 4px on hover, no ring, 0/20 padding (MobiusButtonGeometry::Tab). */
	Tab			UMETA(DisplayName = "Setting Tab"),

	/** Take no shape from C++ — keep the shared "Mobius.Button" style. What an unbound button does today. */
	Shared		UMETA(DisplayName = "Shared Default")
};

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
	 * ALSO neutralises UButton::BackgroundColor's RGB to white (alpha untouched) before any of the above.
	 * That property reaches Slate as SButton's BorderBackgroundColor and MULTIPLIES every brush tint set
	 * here, so a non-neutral authored value silently re-tints the palette colour this function just
	 * resolved. A6b-6a moved that write here from the legacy value-walk, which was its only other owner;
	 * see the implementation comment for the project-wide measurement that made the move safe.
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

	/**
	 * LEGACY, being retired. The Style asset the designer bound in the widget. It used to supply the
	 * button's GEOMETRY (draw type, corner radii, outline width, padding, sound) by being snapshotted
	 * wholesale into the live style.
	 *
	 * As of 2026-08-06 it no longer is: geometry and sound come from FMobiusButtonGeometry, and colour
	 * has come from the palette since A6b. This pointer is now read for ONE thing — to infer which
	 * geometry family a button belongs to when GeometryFamily is FromAsset — and that inference exists
	 * only so the migration could land without editing 40 widgets in the same commit. Once every
	 * consumer carries an explicit GeometryFamily, both this property and the inference go.
	 *
	 * Do not bind it on a NEW widget. Set GeometryFamily instead: the owner's 2026-08-06 ruling is that
	 * widgets are themed by the palette subsystem, not by a style asset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Style")
	TObjectPtr<USlateWidgetStyleAsset> SlateButtonStyle;

	/**
	 * Which named C++ geometry this button is shaped by. Defaults to FromAsset so that every widget on
	 * disk at migration time keeps exactly the shape it had — the 12 UBaseButtons that bind NO style
	 * asset (the tool-panel rows, the Custom Display link, the Reset/Confirm bar) must keep falling
	 * through to the shared "Mobius.Button" style, and defaulting this to Panel would have silently
	 * re-laid-out all ten live ones.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Style")
	EMobiusButtonGeometryFamily GeometryFamily = EMobiusButtonGeometryFamily::FromAsset;

	/**
	 * Resolve GeometryFamily to a shape, or null for "leave this button's geometry alone".
	 * FromAsset maps the bound asset by name; an unbound button resolves to null and keeps the shared
	 * style, which is the pre-migration behaviour for those 12.
	 */
	const struct FMobiusButtonGeometry* ResolveButtonGeometry() const;

	/** Set: the button's fill/hover/pressed/outline/foreground colours are re-stamped from the theme
	 *  palette on construct and on every theme change (the SWS asset still supplies geometry).
	 *  Clear: colours are left exactly as the style asset authored them — use this only for a button
	 *  whose owner drives its state colours itself, so the re-stamp cannot repaint that meaning away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Theme")
	bool bFollowThemePalette = true;

	/**
	 * Set: this button is a TOOL PANEL ROW, not a standard button — the third styling variant alongside
	 * the default fill and UButtonWithText's ribbon tabs.
	 *
	 * Per UE_IMPLEMENTATION_SPEC_v2 §3.2, a tool-panel row is a flat click target on the pane surface:
	 * the pane's own background shows through at rest and only HOVER carries a fill ("hover = btnhover
	 * tint"). Emphasis on a row is the job of a sibling border with a declared role — e.g. the floor-stats
	 * Total row's wellbg + hairline well — not of the button's own fill.
	 *
	 * So Normal/Disabled become fully transparent and the label takes LabelText rather than ButtonText.
	 * That also removes a real defect: these rows author Normal as DrawAs=Box with NO ResourceObject
	 * (Hovered/Pressed are RoundedBox), and an unresourced Box brush paints a hard dark slab, which is
	 * what made the floor-stats header and rows read as black in dark theme.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Theme")
	bool bIsToolPanelRow = false;

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
