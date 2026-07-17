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
#include "Subsystems/GameInstanceSubsystem.h"
#include "Styling/SlateTypes.h"     // FButtonStyle / FWindowStyle (GetThemedTabStyle / GetThemedWindowStyle)
#include "UIThemeSubsystem.generated.h"

class UWidget;
class UMaterialInterface;
struct FSlateBrush;

UENUM(BlueprintType)
enum class EMobiusUITheme : uint8
{
	Dark,
	Light
};

/**
 * One entry per mockup CSS var (design-tokens.json v2 "themes" object). This is the authoritative
 * Mobius UI palette — the single source of truth for phases P2-P7, exposed via GetPaletteColor().
 *
 * NOTE ON CONSUMPTION: the theme SWITCH itself is a value-remap walk (MobiusTheme::SurfaceMap /
 * TextMap in the .cpp) that swaps colours dark<->light by matching literal widget values within an
 * epsilon. That walker can only distinguish a *curated subset* of these roles — the dark-grey chrome
 * region is so densely packed that many roles share a value bucket at the walker's epsilon (0.012).
 * Roles the walker cannot separate are instead applied EXPLICITLY per theme via style code
 * (buttons/combos/tabs/scalability chips already do this in ApplySharedStyles) sourcing THIS table.
 * So: read a colour with GetPaletteColor(); when authoring a new control, if its role is not one of
 * the walker's SurfaceMap/TextMap rows, set it explicitly for the current theme (and re-set on
 * toggle) rather than relying on the value-match.
 *
 * Values are design-tokens.json v2 linearRGBA VERBATIM (never hex/255). Order must match GMobiusPalette.
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
	Zebra,               // LoS legend alternating row tint (NEW)
	ChipOutline,         // LoS chip 1px outline, rgba w/ ALPHA (NEW)
	WellBg,              // total-occupants row / move-markers well
	IconTint,            // MID glyph tint per theme (NEW)
	HoverBg,             // spec-4 "hoverbg" — generic row hover, distinct from ButtonHoverBg (Q11)
	HintText,            // spec-4 "hint" (Q12)
	WindowBorder,        // spec-4 "winborder" — SMoveableWindow chrome (Q13)
	Count UMETA(Hidden)
};

/**
 * Runtime light/dark theme switcher for the Mobius UI (design doc: dark = turn 7a "AutoCAD dark",
 * light = turn 4b "Windows white").
 *
 * Widget colours across the app are literal per-widget values (no MPC / style indirection), so the
 * switch works BY VALUE: every live widget is visited and any colour that matches a known
 * dark-palette role is swapped for its light counterpart (and vice versa). Data-driven colours
 * (LOS band chips, heatmap tints) match no palette entry and pass through untouched.
 *
 * On top of the per-widget walk it also:
 *  - swaps chrome material instances between .../Master/Instances/DarkTheme/ and .../LightTheme/,
 *  - retints the bottom-bar icon materials (glyph/background/border) via dynamic instances,
 *  - retints the two shared button styles: SWS_SettingButtonStyle (ribbon tabs) and the
 *    FMobiusStyle "Mobius.Button" fallback (Browse et al) — both mutated in place so live
 *    Slate widgets holding style pointers repaint with the new colours.
 *
 * Widgets spawned AFTER a switch construct with their dark design-time defaults — ReapplyTheme()
 * re-runs the walker (idempotent). UThemeToggleWidget calls it on construct so a saved Light
 * theme is applied at startup.
 */
UCLASS()
class MOBIUSWIDGETS_API UUIThemeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Switch the whole live UI to the given theme and persist the choice (GameUserSettings). */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Theme")
	void SetTheme(EMobiusUITheme NewTheme);

	UFUNCTION(BlueprintCallable, Category = "Mobius|Theme")
	void ToggleTheme();

	UFUNCTION(BlueprintPure, Category = "Mobius|Theme")
	EMobiusUITheme GetTheme() const { return CurrentTheme; }

	/** Re-run the palette walk for the CURRENT theme (idempotent; picks up late-spawned widgets). */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Theme")
	void ReapplyTheme();

	/**
	 * Look up a mockup-var colour (design-tokens.json v2 verbatim) for the CURRENT theme.
	 * Use this when styling controls whose role the value-remap walker can't distinguish
	 * (see EMobiusPaletteRole docs) — set the returned colour explicitly, and re-set on toggle.
	 */
	UFUNCTION(BlueprintPure, Category = "Mobius|Theme")
	FLinearColor GetPaletteColor(EMobiusPaletteRole Role) const;

	/** As GetPaletteColor but for an explicit theme (e.g. to author both states at once). */
	UFUNCTION(BlueprintPure, Category = "Mobius|Theme")
	FLinearColor GetPaletteColorForTheme(EMobiusPaletteRole Role, EMobiusUITheme Theme) const;

	/**
	 * Theme-aware ribbon-tab material for the CURRENT theme: returns MI_TabSelected / MI_TabDefault
	 * from .../WidgetMaterials/Master/Instances/{Dark,Light}Theme/. ROOT FIX for the tab dark-flash:
	 * the BP tab-swap (WBP_SettingPanel SetActiveRibbonTabMaterial / ResetOldRibbonTabMaterial) should
	 * source its Set-Style brush from THIS instead of hard-referencing the DarkTheme MI — then a tab
	 * click in light mode never shows a dark frame (no deferred ReapplyTheme needed to un-dark it).
	 */
	UFUNCTION(BlueprintPure, Category = "Mobius|Theme")
	UMaterialInterface* GetThemedTabMaterial(bool bSelected) const;

	/**
	 * Q22 ROOT-FIX GETTER for the BP tab swap. Returns a complete FButtonStyle for the ribbon tab in
	 * its selected/inactive state for the CURRENT theme: the shared SWS_SettingButtonStyle as a base,
	 * with Normal/Hovered/Pressed brush materials swapped to GetThemedTabMaterial(bSelected) and the
	 * per-theme fill/foreground applied. WBP_SettingPanel's SetActiveRibbonTabMaterial /
	 * ResetOldRibbonTabMaterial should feed the Set-Style InStyle pin from THIS (one pure node) instead
	 * of the baked FButtonStyle variables that hard-reference the DarkTheme MI — kills the tab dark-flash
	 * in light mode without waiting for the deferred ReapplyTheme.
	 */
	UFUNCTION(BlueprintPure, Category = "Mobius|Theme")
	FButtonStyle GetThemedTabStyle(bool bSelected) const;

	/**
	 * Themed SWindow chrome for the CURRENT theme (D8/Q3): a Core "Window" FWindowStyle with title/
	 * border/background brushes tinted to TitlebarBg / WindowBorder and the title text to TitlebarText.
	 * SMoveableWindow creators (ImPlot overlay, agent-stats window) pass this so the window chrome is
	 * themed at open; SWindowTitleBarWidget additionally polls the palette on paint so the title bar
	 * follows a live theme toggle. Not BlueprintPure (FWindowStyle is C++-consumed only).
	 */
	FWindowStyle GetThemedWindowStyle() const;

	/**
	 * ComboBoxString item/content generator bound by the theme walk: a plain text block in the
	 * current theme's text colour, so dropdown entries follow the theme (the default generator
	 * bakes construction-time colours).
	 */
	UFUNCTION()
	UWidget* HandleGenerateThemedComboEntry(FString Item);

private:
	void ApplyTheme(bool bLight);
	void ApplyToLiveWidgets(bool bLight);
	void ApplyToWidget(UWidget* Widget, bool bLight);
	/**
	 * Explicit per-theme colour for widgets the value-remap walker cannot distinguish (dark-grey
	 * collapse at Epsilon, white-guard, or alpha roles — D25/D26 + P3/P4 EXPLICIT-REAPPLY queues),
	 * keyed by widget-name substring. Table-driven (extend for P5: HoverBg, control states). Returns
	 * true when it fully handled the widget (caller then skips the generic value walk for it).
	 */
	bool ApplyNameRoleOverride(UWidget* Widget, bool bLight);
	/** Retint the shared SWS tab style + the "Mobius.Button" style-set entry in place. */
	void ApplySharedStyles(bool bLight);

	EMobiusUITheme CurrentTheme = EMobiusUITheme::Dark;
};
