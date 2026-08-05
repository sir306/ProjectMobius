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
	void ApplyToBrush(FSlateBrush& Brush) const;

	/** Stamp the shape onto all four state brushes AND both paddings (equal totals, see above). */
	void ApplyToButtonStyle(FButtonStyle& Style) const;
};

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
	 * NOTE (2026-08-05): this currently has NO caller, and that is expected, not an oversight. Its one
	 * call site was the `SWS_ScaleabilityButtonCurrentSet` branch of `ApplySharedStyles`, retired the
	 * same day along with the asset (see `/Game/99_Old/`). Kept because it is the named home the three
	 * families in the header note above would otherwise each re-derive — reach for it the next time a
	 * chip's shape has to be set from C++, rather than typing radius 4 / outline 1 into a fourth file.
	 */
	extern MOBIUSWIDGETS_API const FMobiusButtonGeometry Chip;
}
