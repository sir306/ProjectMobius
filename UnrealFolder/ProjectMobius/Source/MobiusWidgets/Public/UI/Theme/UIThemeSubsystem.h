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
#include "Containers/Ticker.h"       // FTSTicker::FDelegateHandle (startup re-theme ticker, removed in Deinitialize)
#include "Styling/SlateTypes.h"     // FButtonStyle / FWindowStyle (GetThemedTabStyle / GetThemedWindowStyle)
#include "UI/Theme/MobiusThemePalette.h"  // EMobiusPaletteRole + authoritative palette (moved out of the .cpp, 2026-07-21)
#include "UIThemeSubsystem.generated.h"

class UWidget;
class UMaterialInterface;
class UComboBoxString;
class UButtonWithText;
struct FSlateBrush;

UENUM(BlueprintType)
enum class EMobiusUITheme : uint8
{
	Dark,
	Light
};

// EMobiusPaletteRole + the authoritative palette table moved to UI/Theme/MobiusThemePalette.h
// (owner directive 2026-07-21 — theme data lives in a dedicated header, not the subsystem).

/** Broadcast at the END of a theme apply so widgets can re-pull their role colours (new architecture). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMobiusThemeChanged);

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
	virtual void Deinitialize() override;

	/** Switch the whole live UI to the given theme and persist the choice (GameUserSettings). */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Theme")
	void SetTheme(EMobiusUITheme NewTheme);

	UFUNCTION(BlueprintCallable, Category = "Mobius|Theme")
	void ToggleTheme();

	UFUNCTION(BlueprintPure, Category = "Mobius|Theme")
	EMobiusUITheme GetTheme() const { return CurrentTheme; }

	/**
	 * Fires at the END of every theme apply (SetTheme / ReapplyTheme). New-architecture widgets bind
	 * this and re-pull their role colours on fire — the event replacement for the value-remap walk.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Mobius|Theme")
	FOnMobiusThemeChanged OnThemeChanged;

	/** Re-run the palette walk for the CURRENT theme (idempotent; picks up late-spawned widgets). */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Theme")
	void ReapplyTheme();

	/**
	 * Run the per-widget theme pass over ONE UserWidget's own tree for the current theme. For widgets the
	 * live-widget walk deliberately SKIPS — the in-world flow-counter cards live on a UWidgetComponent, not in
	 * the viewport, so ApplyToLiveWidgets excludes them. They call this from their own OnThemeChanged handler
	 * to self-theme (card material via ThemeBackgroundBrush, FieldAndText rows via the walk branch, etc.).
	 */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Theme")
	void ReapplyToUserWidget(UUserWidget* UserWidget);

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
	// bRightEdge=false → bottom-underline tab MI (top ribbon, unchanged). bRightEdge=true → the *Right
	// variants (MI_TabSelectedRight / MI_TabDefaultRight) whose AccentEdge=1 puts the accent on the RIGHT
	// edge — for the vertical side tool-rail (WBP_ToolPanel), which reads better than a bottom underline.
	UFUNCTION(BlueprintPure, Category = "Mobius|Theme")
	UMaterialInterface* GetThemedTabMaterial(bool bSelected, bool bRightEdge = false) const;

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
	FButtonStyle GetThemedTabStyle(bool bSelected, bool bRightEdge = false) const;

	/**
	 * Apply the themed ribbon-tab look to a UButtonWithText in ONE authoritative place (new architecture,
	 * W2): sets the button's FButtonStyle from GetThemedTabStyle(bActive) AND sets the label colour
	 * EXPLICITLY (TabActiveText / TabInactiveText) via ApplyThemedLabelColor. The explicit label set is
	 * the fix for the invisible ribbon-tab text: UseForeground did not resolve to the button foreground
	 * for these buttons, and the old split (BP SetStyle in construct + on activation, walk re-landing the
	 * label) fought itself. Called by the button on bIsActiveTab change / construct / OnThemeChanged — the
	 * ribbon BP no longer needs to SetStyle at all, just set bIsActiveTab.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Theme")
	void ApplyRibbonTabStyle(UButtonWithText* Button, bool bActive);

	/**
	 * A16 (2026-07-28): give a themed tab style a real HOVER FILL. GetThemedTabStyle assigns one MI_Tab*
	 * material to all four state brushes, so hovering a tab changed nothing but the label colour. This
	 * repoints Style.Hovered at a MaterialInstanceDynamic of that same MI with FillColour overridden to
	 * the app-wide ButtonHoverBg role — one hover language for tabs and buttons alike. Outer owns the MID
	 * (pass the button). A brighter Brush.TintColor cannot do this: Slate packs the tint into an FColor
	 * vertex colour, so > 1.0 clamps and dark mode would not lift at all. Called by ApplyRibbonTabStyle;
	 * separate so a Blueprint feeding SetStyle straight from GetThemedTabStyle can opt in too.
	 */
	void ApplyTabHoverFill(FButtonStyle& Style, UObject* Outer, bool bLight) const;

	/**
	 * Themed SWindow chrome for the CURRENT theme (D8/Q3): a Core "Window" FWindowStyle with title/
	 * border/background brushes tinted to TitlebarBg / WindowBorder and the title text to TitlebarText.
	 * SMoveableWindow creators (ImPlot overlay, agent-stats window) pass this so the window chrome is
	 * themed at open; SWindowTitleBarWidget additionally polls the palette on paint so the title bar
	 * follows a live theme toggle. Not BlueprintPure (FWindowStyle is C++-consumed only).
	 */
	FWindowStyle GetThemedWindowStyle() const;

	/**
	 * Style a UComboBoxString's WidgetStyle / ItemStyle MEMBERS for the given theme (new architecture,
	 * migration W1). The closed-combo button SURFACE becomes an M_MobiusInput brush (samples
	 * MPC_UITheme.Field) so it follows a runtime toggle GPU-side; the dropdown row colours are baked flat.
	 * It writes ONLY UPROPERTY members and NEVER touches the live SComboBox / SMenuAnchor, so it is valid
	 * ONLY before the Slate is built — call it from a UComboBoxString subclass's RebuildWidget() before
	 * Super (see UMobiusThemedComboBox). "Born themed, never restyled live" is the fix for the
	 * FMRSWRecursiveAccessDetector ensure that a post-build combo restyle tripped on menu-open / PIE-close.
	 * The selected-item TEXT foreground (InputText role) is set by the subclass via the engine's protected
	 * InitForegroundColor() — the only pre-build foreground setter — so it is not handled here.
	 */
	static void StyleComboBoxForBuild(UComboBoxString* Combo, bool bLight);

private:
	void ApplyTheme(bool bLight);
	/**
	 * Push the current palette into the MPC_UITheme Material Parameter Collection (new architecture:
	 * chrome/button/input materials sample it, so a theme switch updates them GPU-side with no walk).
	 * No-op until the MPC asset exists. See _ClaudeHandoff/PRD_ThemeSystemRework.md.
	 */
	void WriteThemeToMPC(bool bLight);
	int32 ApplyToLiveWidgets(bool bLight);
	/** Re-theme every in-world UWidgetComponent-hosted widget (flow-counter cards) — GetAllWidgetsOfClass
	 *  does not return world-space component widgets, so the walk cannot reach them. */
	void ThemeInWorldWidgetComponents();
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

	/**
	 * Startup one-shot re-theme ticker (FTSTicker core ticker, ~30 s window). Stored so Deinitialize
	 * can remove it: the core ticker is GLOBAL and outlives the PIE world, so if left registered it
	 * fires during world teardown and walks half-destroyed widgets → the combo SMenuAnchor delegate
	 * -access ensure (the "crash on PIE close"). Also reset when the ticker self-unregisters.
	 */
	FTSTicker::FDelegateHandle StartupThemeTickerHandle;
};
