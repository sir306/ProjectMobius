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
#include "MobiusThemePalette.generated.h"

/**
 * DEDICATED THEME-DATA HEADER (owner directive 2026-07-21): the authoritative Mobius UI palette lives
 * HERE, not buried in UIThemeSubsystem.cpp, so it is easy to see and edit. It is the single source of
 * truth for both the (legacy, being retired) value-remap walker AND the new MPC_UITheme writer — one
 * table, two consumers. Values are design-tokens.json v2 linearRGBA VERBATIM (never hex/255).
 *
 * One entry per mockup CSS var. Order MUST match GMobiusPalette (static_assert enforces the count).
 */
UENUM(BlueprintType)
enum class EMobiusPaletteRole : uint8
{
	TitlebarBg,
	TitlebarBorder,
	TitlebarText,
	TabstripBg,
	TabstripBorder,      // spec-4 "line" (#d0d0d0 / #242424)
	TabActiveBg,         // spec-4 "tabactive"
	TabActiveText,
	TabInactiveText,
	TabActiveOutline,    // light-only outline; dark value = dark line for completeness
	Accent,
	RibbonBg,            // spec-4 "surface"
	PanelHeaderBg,       // spec-4 "header"
	PanelHeaderText,     // spec-4 "text2"
	PanelHeaderBorder,   // spec-4 "hairline"
	PanelDivider,        // spec-4 "divider"
	LabelText,           // spec-4 "text"
	SublabelText,        // spec-4 "muted"
	MicroText,           // spec-4 "faint"
	InputBg,             // spec-4 "input"
	InputBorder,         // spec-4 "inputborder"
	InputText,
	InputPlaceholder,
	InputMonoText,       // spec-4 "icon" (glyph/mono text #555555 / #9a9a9a)
	ButtonBg,            // spec-4 "btn"
	ButtonBorder,        // spec-4 "btnborder"
	ButtonText,
	ButtonHoverBg,       // spec-4 "btnhover"
	ButtonHoverBorder,   // spec-4 "btnhoverborder"
	ButtonPressedBg,
	CheckboxBg,
	CheckboxBorder,
	CheckboxCheckedBg,
	CheckboxCheckmark,
	SliderTrack,         // spec-4 "track"
	SliderThumb,
	KbdBg,
	KbdBorder,
	KbdText,
	HelpRowDivider,      // spec-4 "rowline"
	HelpRowText,
	Zebra,               // LoS legend alternating row tint
	ChipOutline,         // LoS chip 1px outline, rgba w/ ALPHA
	WellBg,              // total-occupants row / move-markers well
	IconTint,            // MID glyph tint per theme
	HoverBg,             // spec-4 "hoverbg" — generic row hover, distinct from ButtonHoverBg (Q11)
	HintText,            // spec-4 "hint" (Q12)
	WindowBorder,        // spec-4 "winborder" — SMoveableWindow chrome (Q13)
	/**
	 * DESTRUCTIVE / ERROR text + glyph (owner directive 2026-07-28: "we want danger/error role in the
	 * palette as that is better UX design making it more accessible to what people expect"). Consumers:
	 * the Remove flow-counter label today; close (×) icon buttons and the error-window styling next.
	 * Deliberately ONE role for now — a full danger ramp (fill / border / hover / pressed) waits on the
	 * error-UI task rather than being invented here.
	 */
	DangerText,
	Count UMETA(Hidden)
};

namespace MobiusThemePalette
{
	/** Light/dark pair for one role. Plain (non-reflected) POD — colour data only. */
	struct FThemeColor
	{
		FLinearColor Light;
		FLinearColor Dark;
	};

	// AUTHORITATIVE PALETTE — design-tokens.json v2 linearRGBA VERBATIM. Indexed by EMobiusPaletteRole.
	// `inline` (C++17+) gives one definition across TUs. Moved verbatim from UIThemeSubsystem.cpp 2026-07-21.
	inline const FThemeColor GMobiusPalette[] =
	{
		/* TitlebarBg        */ { FLinearColor(1.0f, 1.0f, 1.0f),                 FLinearColor(0.03955f, 0.03955f, 0.03955f) },
		/* TitlebarBorder    */ { FLinearColor(0.76052f, 0.76052f, 0.76052f),     FLinearColor(0.01938f, 0.01938f, 0.01938f) },
		/* TitlebarText      */ { FLinearColor(0.0331f, 0.0331f, 0.0331f),        FLinearColor(0.55201f, 0.55201f, 0.55201f) },
		/* TabstripBg        */ { FLinearColor(0.7913f, 0.7913f, 0.7913f),        FLinearColor(0.02843f, 0.02843f, 0.02843f) },
		/* TabstripBorder    */ { FLinearColor(0.63076f, 0.63076f, 0.63076f),     FLinearColor(0.01764f, 0.01764f, 0.01764f) },
		/* TabActiveBg       */ { FLinearColor(0.9131f, 0.9131f, 0.9131f),        FLinearColor(0.04519f, 0.04519f, 0.04519f) },
		/* TabActiveText     */ { FLinearColor(0.0f, 0.13563f, 0.52712f),         FLinearColor(0.10224f, 0.32778f, 0.66539f) },
		/* TabInactiveText   */ { FLinearColor(0.05781f, 0.05781f, 0.05781f),     FLinearColor(0.32314f, 0.32314f, 0.32314f) },
		/* TabActiveOutline  */ { FLinearColor(0.63076f, 0.63076f, 0.63076f),     FLinearColor(0.01764f, 0.01764f, 0.01764f) },
		/* Accent            */ { FLinearColor(0.0f, 0.13563f, 0.52712f),         FLinearColor(0.10224f, 0.32778f, 0.66539f) },
		/* RibbonBg          */ { FLinearColor(0.9131f, 0.9131f, 0.9131f),        FLinearColor(0.03955f, 0.03955f, 0.03955f) },
		/* PanelHeaderBg     */ { FLinearColor(0.82279f, 0.82279f, 0.82279f),     FLinearColor(0.05286f, 0.05286f, 0.05286f) },
		/* PanelHeaderText   */ { FLinearColor(0.05781f, 0.05781f, 0.05781f),     FLinearColor(0.46208f, 0.46208f, 0.46208f) },
		/* PanelHeaderBorder */ { FLinearColor(0.7454f, 0.7454f, 0.7454f),        FLinearColor(0.06848f, 0.06848f, 0.06848f) },
		/* PanelDivider      */ { FLinearColor(0.76815f, 0.76815f, 0.76815f),     FLinearColor(0.05951f, 0.05951f, 0.05951f) },
		/* LabelText         */ { FLinearColor(0.016f, 0.016f, 0.016f),           FLinearColor(0.62396f, 0.62396f, 0.62396f) },
		/* SublabelText      */ { FLinearColor(0.13287f, 0.13287f, 0.13287f),     FLinearColor(0.32314f, 0.32314f, 0.32314f) },
		/* MicroText         */ { FLinearColor(0.2462f, 0.2462f, 0.2462f),        FLinearColor(0.25818f, 0.25818f, 0.25818f) },
		/* InputBg           */ { FLinearColor(1.0f, 1.0f, 1.0f),                 FLinearColor(0.02416f, 0.02416f, 0.02416f) },
		/* InputBorder       */ { FLinearColor(0.19462f, 0.19462f, 0.19462f),     FLinearColor(0.09084f, 0.09084f, 0.09084f) },
		/* InputText         */ { FLinearColor(0.016f, 0.016f, 0.016f),           FLinearColor(0.62396f, 0.62396f, 0.62396f) },
		/* InputPlaceholder  */ { FLinearColor(0.40198f, 0.40198f, 0.40198f),     FLinearColor(0.14413f, 0.14413f, 0.14413f) },
		/* InputMonoText     */ { FLinearColor(0.09084f, 0.09084f, 0.09084f),     FLinearColor(0.32314f, 0.32314f, 0.32314f) },
		/* ButtonBg          */ { FLinearColor(0.93869f, 0.93869f, 0.93869f),     FLinearColor(0.06848f, 0.06848f, 0.06848f) },
		/* ButtonBorder      */ { FLinearColor(0.41789f, 0.41789f, 0.41789f),     FLinearColor(0.10224f, 0.10224f, 0.10224f) },
		/* ButtonText        */ { FLinearColor(0.016f, 0.016f, 0.016f),           FLinearColor(0.7454f, 0.7454f, 0.7454f) },
		/* ButtonHoverBg     */ { FLinearColor(0.81485f, 0.87962f, 0.95597f),     FLinearColor(0.09306f, 0.09306f, 0.09306f) },
		/* ButtonHoverBorder */ { FLinearColor(0.25415f, 0.47932f, 0.72306f),     FLinearColor(0.14413f, 0.14413f, 0.14413f) },
		/* ButtonPressedBg   */ { FLinearColor(0.69387f, 0.7991f, 0.9131f),       FLinearColor(0.04971f, 0.04971f, 0.04971f) },
		/* CheckboxBg        */ { FLinearColor(1.0f, 1.0f, 1.0f),                 FLinearColor(0.02416f, 0.02416f, 0.02416f) },
		/* CheckboxBorder    */ { FLinearColor(0.19462f, 0.19462f, 0.19462f),     FLinearColor(0.13287f, 0.13287f, 0.13287f) },
		/* CheckboxCheckedBg */ { FLinearColor(0.0f, 0.13563f, 0.52712f),         FLinearColor(0.10224f, 0.32778f, 0.66539f) },
		/* CheckboxCheckmark */ { FLinearColor(1.0f, 1.0f, 1.0f),                 FLinearColor(1.0f, 1.0f, 1.0f) },
		/* SliderTrack       */ { FLinearColor(0.58408f, 0.58408f, 0.58408f),     FLinearColor(0.06848f, 0.06848f, 0.06848f) },
		/* SliderThumb       */ { FLinearColor(0.0f, 0.13563f, 0.52712f),         FLinearColor(0.10224f, 0.32778f, 0.66539f) },
		/* KbdBg             */ { FLinearColor(1.0f, 1.0f, 1.0f),                 FLinearColor(0.05951f, 0.05951f, 0.05951f) },
		/* KbdBorder         */ { FLinearColor(0.41789f, 0.41789f, 0.41789f),     FLinearColor(0.10224f, 0.10224f, 0.10224f) },
		/* KbdText           */ { FLinearColor(0.016f, 0.016f, 0.016f),           FLinearColor(0.7454f, 0.7454f, 0.7454f) },
		/* HelpRowDivider    */ { FLinearColor(0.7913f, 0.7913f, 0.7913f),        FLinearColor(0.05951f, 0.05951f, 0.05951f) },
		/* HelpRowText       */ { FLinearColor(0.0331f, 0.0331f, 0.0331f),        FLinearColor(0.62396f, 0.62396f, 0.62396f) },
		/* Zebra             */ { FLinearColor(0.85499f, 0.85499f, 0.85499f),     FLinearColor(0.04971f, 0.04971f, 0.04971f) },
		/* ChipOutline       */ { FLinearColor(0.0f, 0.0f, 0.0f, 0.25f),          FLinearColor(1.0f, 1.0f, 1.0f, 0.30f) },
		/* WellBg            */ { FLinearColor(0.85499f, 0.87137f, 0.88792f),     FLinearColor(0.05286f, 0.05286f, 0.05286f) },
		/* IconTint          */ { FLinearColor(0.04374f, 0.04374f, 0.04374f),     FLinearColor(0.69387f, 0.69387f, 0.69387f) },
		/* HoverBg           */ { FLinearColor(0.69387f, 0.69387f, 0.69387f),     FLinearColor(0.06848f, 0.06848f, 0.06848f) },
		/* HintText          */ { FLinearColor(0.31855f, 0.31855f, 0.31855f),     FLinearColor(0.14413f, 0.14413f, 0.14413f) },
		/* WindowBorder      */ { FLinearColor(0.43415f, 0.43415f, 0.43415f),     FLinearColor(0.01033f, 0.01033f, 0.01033f) },
		// DangerText — DARK REVISED 2026-07-28 to #FF6B5E after the owner saw #FF8A80 in PIE: "the removal
		// text color in dark mode looks washed out not the vibrant red it was before, light mode looks
		// correct". Light is unchanged (confirmed correct); only the dark pair moved.
		//
		// There is a HARD CEILING here, so this is a deliberate trade, not a tuning miss. Contrast is driven
		// by relative luminance, and a saturated red has almost none: pure #FF0000 tops out at **2.22:1** on
		// the dark ButtonBg (0.06848). Every point of contrast above that has to come from adding green and
		// blue — i.e. from desaturating toward salmon, which is exactly the "washed out" the owner rejected.
		// So "vibrant AND 4.5:1" does not exist on this surface. #FF6B5E is about the most saturated red that
		// still clears **3:1** (measured 3.17:1) — the WCAG bar for large text and UI components, and it
		// still reads red rather than the orange you get by pushing further (#FF7043 buys only 3.23:1 and
		// turns orange). For reference on the same background: #FF8A80 3.9:1 (rejected, washed out),
		// #E7392D 2.1:1 (the original authored red — the accessibility bug this role exists to fix).
		// If the 11px Remove label must hit strict AA, the fix is a lighter BUTTON, not a lighter red.
		//
		// Light #C42B1C — the Fluent/Win11 error red, which is the family the rest
		// of the light theme already comes from; measured 5.3:1 on ButtonBg (0.93869), so AA for normal text.
		// Dark #FF8A80 rather than the red that was authored into SWS_FlowRemoveButtonTextStyle
		// (0.7836, 0.0409, 0.0252 = #E7392D): that value measures only **2.1:1** on the dark ButtonBg
		// (0.06848) and is exactly the accessibility problem this role exists to fix. #FF8A80 measures
		// 3.9:1 — a clear pass for large text, a hair under the 4.5:1 normal-text bar. If strict AA is
		// wanted for the 11px labels, #FF9E9E is 4.5:1; it reads more salmon than red, so it is the
		// owner's call, not a silent substitution.
		/* DangerText        */ { FLinearColor(0.55199f, 0.02415f, 0.01162f),     FLinearColor(1.0f, 0.14701f, 0.11192f) },
	};

	static_assert(UE_ARRAY_COUNT(GMobiusPalette) == static_cast<int32>(EMobiusPaletteRole::Count),
		"GMobiusPalette must have exactly one entry per EMobiusPaletteRole (order must match).");

	/** Look up a role's colour for the given theme. Returns black for an out-of-range role. */
	inline FLinearColor Color(const EMobiusPaletteRole Role, const bool bLight)
	{
		const int32 Index = static_cast<int32>(Role);
		if (Index < 0 || Index >= UE_ARRAY_COUNT(GMobiusPalette))
		{
			return FLinearColor::Black;
		}
		return bLight ? GMobiusPalette[Index].Light : GMobiusPalette[Index].Dark;
	}
}
