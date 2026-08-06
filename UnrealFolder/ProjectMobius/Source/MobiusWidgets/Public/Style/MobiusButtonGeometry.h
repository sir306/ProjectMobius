// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

/**
 * A10 item 2 (2026-08-05) — named button GEOMETRY, so the shape numbers already living in C++ share one
 * vocabulary instead of being re-typed per file.
 *
 * The Mobius split is: the theme subsystem owns COLOURS, the SWS_* asset owns padding / sound / geometry.
 * That split holds for every button whose asset already carries the right shape. It cannot hold for a
 * button whose shape is a RUNTIME TRANSFORMATION of its asset — `SWS_ScaleabilityButtonCurrentSet` ships
 * as an IMAGE brush with a baked material (`MI_PanelButtonStyle_Pressed`, HALF_HEIGHT rounding, no
 * outline) and is turned into a flat rounded box at load. Half-migrating that into the asset would leave
 * a `.uasset` that looks nothing like what ships, so the owner ruled the geometry stays in C++ — but
 * NAMED and in one place, which is what this header is.
 *
 * Nothing here holds a colour. Colours stay with the palette (`UUIThemeSubsystem`), and every function
 * below leaves `TintColor` and `OutlineSettings.Color` untouched so it can be applied before, after, or
 * independently of a theme pass.
 *
 * ALREADY HOMED ELSEWHERE — do not add duplicates here for these:
 *  - Ribbon tabs (horizontal AND vertical): `UUIThemeSubsystem::GetThemedTabStyle`, which already
 *    overrides the asset's 0,20,0,20 to FMargin(0,4). `bRightEdge` swaps only the material variant, so
 *    the two orientations are ONE geometry.
 *  - Segmented controls (A20 theme toggle): `ThemeToggleWidget.cpp`'s GContainerCornerRadius /
 *    GSegmentCornerRadius / GSegmentPadding / MakeSegmentBrush. That file is the idiom this one follows.
 *  - Playbar play/pause and the VR buttons: pure IMAGE + material brushes with all-zero padding. They
 *    have no geometry to name; converting them to RoundedBox would drop the glyph (Slate routes a
 *    material brush through the material shader and skips the rounded-corner mask).
 */
struct MOBIUSWIDGETS_API FMobiusButtonGeometry
{
	/** Corner radius in px, applied as a FIXED radius on all four corners. */
	float CornerRadius = 0.0f;

	/**
	 * Corner radius for the HOVERED and PRESSED brushes. Negative means "same as CornerRadius", which is
	 * the common case. It exists because SWS_SettingButtonStyle is genuinely square at rest and rounded
	 * on hover (measured: Normal CornerRadii 0, Hovered/Pressed 4) — a single radius cannot express that
	 * family, and flattening it to one value would quietly restyle the material-picker buttons.
	 */
	float HoverCornerRadius = -1.0f;

	/** Outline width in px. 0 = no ring. The outline COLOUR is the palette's, never set here. */
	float OutlineWidth = 0.0f;

	/**
	 * Content padding for the NORMAL state. The pressed state is derived from this rather than stored,
	 * so the two can never drift into unequal totals — a pressed padding that is smaller on any edge
	 * shrinks the hit rect mid-press, Slate fires OnMouseLeave and the click is DISCARDED. That was the
	 * root cause of the "unresponsive buttons" report; see UBaseButton::StabilisePressedPadding.
	 */
	FMargin NormalPadding = FMargin(0.0f);

	/**
	 * Pressed padding with the SAME totals as NormalPadding, nudging the content 1px down so a press
	 * still reads as "pressed in" without moving a single edge of the hit rect. Matches what
	 * StabilisePressedPadding would compute, so that repair becomes a no-op on styles authored here.
	 */
	FMargin GetStablePressedPadding() const;

	/**
	 * Stamp the shape onto one brush: draw type, rounding, radius, outline width, and clearing any
	 * resource object. Clearing the resource IS geometry, not art removal: a brush that still carries a
	 * material renders through the material shader, which ignores the rounded-corner mask and the
	 * outline entirely — so a RoundedBox brush with a material would silently lose its ring.
	 */
	void ApplyToBrush(FSlateBrush& Brush, float Radius) const;

	/**
	 * Stamp the shape onto all four state brushes AND both paddings (equal totals, see above).
	 *
	 * Disabled is deliberately left as ESlateBrushDrawType::NoDrawType rather than being given the shape:
	 * both SWS assets author it that way, so a disabled button paints NOTHING today. Handing it a
	 * RoundedBox here would make every disabled button start drawing a fill — a regression that renders
	 * only in the disabled state and so would not show up in an ordinary look-the-same gate.
	 */
	void ApplyToButtonStyle(FButtonStyle& Style) const;
};

/**
 * Button SOUND. Not geometry — but it has the same "authored in the SWS asset, must survive the asset"
 * problem, so it lives beside the shape rather than being re-derived at each call site.
 *
 * Both SWS_PanelButtonStyle and SWS_SettingButtonStyle author PressedSlateSound = click_Cue and leave
 * HoveredSlateSound EMPTY (measured 2026-08-06). The button styles were the ONLY route from that cue to
 * a button — nothing in C++ referenced it — so dropping the asset snapshot without this would have
 * silently removed the click sound from every Mobius button.
 */
namespace MobiusButtonSound
{
	/**
	 * ⚠️ THIS PATH LITERAL IS THE ONLY REMAINING HARD REFERENCE TO click_Cue from the button path.
	 * A referencer count on that asset will read ZERO once the SWS bindings are unassigned — it is live,
	 * kept alive from here, exactly like the MI_Tab* materials. Do not retire it on a property or
	 * referencer sweep; see the asset-deadness notes for why a name-literal keeper needs a grep, not a count.
	 */
	extern MOBIUSWIDGETS_API const TCHAR* const PressedCuePath;

	/** Put the click cue on Pressed, leaving Hovered silent, matching what both assets author. */
	MOBIUSWIDGETS_API void ApplyPressedCue(FButtonStyle& Style);
}

namespace MobiusButtonGeometry
{
	/**
	 * CHIP — the standard Mobius button: a 4px rounded box with a 1px ring.
	 *
	 * Measured from `SWS_PanelButtonStyle` (CornerRadii 4, OutlineSettings.Width 1, NormalPadding
	 * 8,4,8,4), which is what Browse and the flow-counter buttons already paint. Its authored
	 * PressedPadding is 6,2,6,2 — deliberately NOT copied here; that is the unequal-totals value
	 * StabilisePressedPadding exists to repair, and transcribing it into a named struct would make the
	 * click-eating padding look intentional.
	 *
	 * UPDATE (2026-08-06): it has a caller again. `UBaseButton::ApplyMobiusButtonStyle` now builds the
	 * panel-button family from this instead of snapshotting SWS_PanelButtonStyle, so the numbers below
	 * are no longer a record of the asset — they ARE the shape. Re-measured against the asset before the
	 * swap and identical on every field, which is what made the migration a zero-pixel change.
	 * (Its previous call site was the `SWS_ScaleabilityButtonCurrentSet` branch of `ApplySharedStyles`,
	 * retired 2026-08-05 along with the asset — see `/Game/99_Old/`.)
	 */
	extern MOBIUSWIDGETS_API const FMobiusButtonGeometry Chip;

	/**
	 * TAB — the setting/panel-tab family: square at rest, 4px rounded on hover, NO ring, tall padding.
	 *
	 * Measured from `SWS_SettingButtonStyle` (Normal CornerRadii 0, Hovered/Pressed 4, OutlineSettings
	 * .Width 0 on all four, NormalPadding 0,20,0,20 — already equal to its PressedPadding, so this family
	 * never had the click-eating asymmetry).
	 *
	 * Ribbon tabs bind this asset but do NOT come through here: UButtonWithText::ApplyMobiusButtonStyle
	 * early-returns on bIsRibbonButton into ApplyRibbonTabStyle, and GetThemedTabStyle overrides the
	 * padding to FMargin(0,4). The live consumers of THIS geometry are the material-picker buttons.
	 */
	extern MOBIUSWIDGETS_API const FMobiusButtonGeometry Tab;
}
