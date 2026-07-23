// Shared themed button style for standalone Slate windows (Log window, Error/Perf popup).
//
// These windows are raw SMoveableWindow chrome added via FSlateApplication::AddWindow, so the UMG
// theme walker never reaches them. Their bodies already poll the palette per-paint; their Close
// buttons previously used the engine-default FCoreStyle "Button" (off-palette). This header builds a
// themed FButtonStyle from the Mobius palette so both windows share one definition.
//
// Header-only (inline) on purpose: adding a .h needs no project regeneration, and the function has no
// state. Callers MUST store the returned style in a member (SButton caches raw pointers into the
// FButtonStyle brushes, so a stack-local would dangle) and apply it via .ButtonStyle(&Member). The
// background brush colours are stamped for the theme current at construct; for live label follow,
// give the button an explicit STextBlock child with a ColorAndOpacity lambda polling ButtonText.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "UI/Theme/MobiusThemePalette.h"
#include "UI/Theme/UIThemeSubsystem.h"

namespace MobiusWindowButtonStyle
{
	/**
	 * Resolve a palette role for the current theme. Falls back to the LIGHT palette value (product
	 * default) when the theme subsystem is unavailable, so an early/unresolved window button is not
	 * stuck on off-palette engine defaults.
	 */
	inline FLinearColor Resolve(const UUIThemeSubsystem* Theme, const EMobiusPaletteRole Role)
	{
		return Theme ? Theme->GetPaletteColor(Role) : MobiusThemePalette::Color(Role, /*bLight=*/true);
	}

	/**
	 * Build a themed FButtonStyle for a standalone Slate window's Close button from poll-based palette
	 * roles: Normal = ButtonBg, Hovered = ButtonHoverBg, Pressed = ButtonPressedBg, 1px outline =
	 * ButtonBorder, per-state foreground = ButtonText. Rounded-box brushes match the shared
	 * "Mobius.Button" look built in FMobiusStyle.
	 */
	inline FButtonStyle MakeWindowButtonStyle(const UUIThemeSubsystem* Theme)
	{
		const FLinearColor NormalBg  = Resolve(Theme, EMobiusPaletteRole::ButtonBg);
		const FLinearColor HoverBg   = Resolve(Theme, EMobiusPaletteRole::ButtonHoverBg);
		const FLinearColor PressedBg = Resolve(Theme, EMobiusPaletteRole::ButtonPressedBg);
		const FLinearColor Border    = Resolve(Theme, EMobiusPaletteRole::ButtonBorder);
		const FSlateColor   Text      = FSlateColor(Resolve(Theme, EMobiusPaletteRole::ButtonText));

		return FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(NormalBg,  2.0f, Border, 1.0f))
			.SetHovered(FSlateRoundedBoxBrush(HoverBg,  2.0f, Border, 1.0f))
			.SetPressed(FSlateRoundedBoxBrush(PressedBg, 2.0f, Border, 1.0f))
			.SetDisabled(FSlateRoundedBoxBrush(NormalBg, 2.0f, Border, 1.0f))
			.SetNormalForeground(Text)
			.SetHoveredForeground(Text)
			.SetPressedForeground(Text)
			.SetDisabledForeground(Text)
			.SetNormalPadding(FMargin(8.0f, 3.0f))
			.SetPressedPadding(FMargin(8.0f, 3.0f));
	}
}
