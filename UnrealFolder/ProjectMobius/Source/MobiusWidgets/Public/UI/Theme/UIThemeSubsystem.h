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
class UUserWidget;
class UMaterialInterface;
class UComboBoxString;
class UButtonWithText;
// A5: the standard controls the event-driven path themes by type.
class UCheckBox;
class UEditableTextBox;
class UProgressBar;
class UScrollBox;
class USlider;
class UTextBlock;   // A6b-5: the relocated text remap
class UButton;      // A6b-5: the relocated plain-button pass
class UImage;       // A6b-5: the relocated image-brush pass
class UBorder;      // A6b-6b: the relocated border pass
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

	/**
	 * Same event for listeners that are not UObjects. OnThemeChanged is a DYNAMIC delegate, so it can only
	 * be bound by (UObject, UFUNCTION) pairs — the Slate-side chrome (SErrorWindowWidget, SLogWindowWidget,
	 * the title bar) is plain SWidget and cannot subscribe to it at all, which is why those windows used to
	 * poll on a 0-second active timer instead. Bind this with AddSP: an SP binding self-expires when the
	 * widget dies, so a missed Remove cannot dangle.
	 *
	 * Broadcast immediately after OnThemeChanged, from the same place, so the two never disagree.
	 */
	FSimpleMulticastDelegate OnThemeChangedNative;

	/** Re-apply the CURRENT theme (idempotent; late-spawned widgets theme themselves on construct). */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Theme")
	void ReapplyTheme();

	/**
	 * Run the standard-control theme pass over ONE UserWidget's own tree (recursing into its embedded
	 * widgets) for the current theme. Exists for widgets no viewport enumeration reaches: the in-world
	 * flow-counter cards live on a UWidgetComponent, so GetAllWidgetsOfClass never returns them. Called by
	 * ThemeInWorldWidgetComponents and by those cards from their own OnThemeChanged handler.
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
	 * A16 + A17 (2026-07-28): give a themed tab style real HOVER and PRESSED FILLS. GetThemedTabStyle
	 * assigns one MI_Tab* material to all four state brushes, so hovering or holding a tab changed nothing
	 * but the label colour. This repoints Style.Hovered / Style.Pressed at MaterialInstanceDynamics of that
	 * same MI with FillColour overridden to the app-wide ButtonHoverBg / ButtonPressedBg roles — one
	 * interaction language for tabs and buttons alike. Outer owns the MIDs (pass the button). A brighter
	 * Brush.TintColor cannot do this: Slate packs the tint into an FColor vertex colour, so > 1.0 clamps and
	 * dark mode would not lift at all. Padding is untouched: NormalPadding/PressedPadding stay EQUAL so the
	 * hit rect cannot shrink mid-press (the A15 dropped-click trap) — fill is the entire press affordance.
	 * Called by ApplyRibbonTabStyle; separate so a Blueprint feeding SetStyle straight from
	 * GetThemedTabStyle can opt in too.
	 */
	void ApplyTabStateFills(FButtonStyle& Style, UObject* Outer, bool bLight) const;

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

	// ---------------------------------------------------------------------------------------------
	// A5 (2026-07-28) — event-driven theming for the standard engine controls (rebuild Phase 4).
	//
	// These five were the walker's per-control-TYPE branches, lifted out so a widget themes its own controls
	// on construct + OnThemeChanged instead of waiting for a sweep to find it. Type in, palette role out —
	// no colour value-matching, no name table, no polling, which is what let A6b-6 delete the walker
	// outright rather than keep a husk of it. Static: they need the palette and nothing else.
	//
	// TWO widget-NAME special cases are carried inside (they are behaviour, not theming, and each one
	// regresses a specific control if dropped): PlaybackSlider's bar is forced transparent because a
	// UProgressBar draws its track; ScrubFillBar is that progress bar and takes a different look from
	// every other bar. A20 (2026-08-03) removed the third — the `*ThemeToggle*` checkbox exclusion — with
	// the checkbox pill it protected; the UI-theme control is now two UButtonWithText segments.
	// ---------------------------------------------------------------------------------------------

	/**
	 * Theme every standard control in one UserWidget's tree for the CURRENT theme, dispatching on widget
	 * TYPE. Recurses into embedded user widgets so one call from a panel covers its whole subtree. Called by
	 * UMobiusThemedUserWidget on NativeConstruct and on every OnThemeChanged.
	 *
	 * A6b-7 (2026-07-31): the recursion STOPS at a child that is itself a UMobiusThemedUserWidget, because
	 * such a child runs this same pass over its own subtree and then its own ApplyMobiusTheme. Descending
	 * would add no coverage and would land an untargeted write AFTER a declared role write — the parent
	 * recursion had replaced the deleted walk as the clobberer. Coverage is unchanged: every widget is
	 * themed by its nearest themed ancestor's pass, so pruning at themed boundaries leaves the union
	 * identical. Consequence for callers: a new UMobiusThemedUserWidget subclass MUST let the base
	 * NativeConstruct run (every existing override calls Super — verified by sweep at A6b-7), or its
	 * subtree loses its only writer.
	 *
	 * bConstruct additionally applies the theme-INDEPENDENT one-offs that only make sense once per build
	 * (the input-box Mono font) — pass false from a theme-change handler.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Theme")
	void ThemeStandardControlsInTree(UUserWidget* Root, bool bConstruct = false);

	/**
	 * Slider: handle = SliderThumb role (design: thumb is the accent), track = SliderTrack for a plain
	 * greyscale bar and LEFT ALONE for a saturated one, because a coloured bar is data (the material
	 * picker's HSV sliders) and not chrome. Also neutralises the style's own brush tints to white so the
	 * handle/bar colour multipliers land once instead of squared (D171 — an accent baked into both a
	 * brush tint and SetSliderHandleColor rendered a near-black handle).
	 */
	static void StyleSliderForTheme(USlider* Slider, bool bLight);

	/**
	 * CheckBox: checked = CheckboxCheckedBg fill + white 1u outline, unchecked = CheckboxBg fill +
	 * CheckboxBorder 1u outline, both radius 3 (Q24/C4). Mutates the brushes in place so the authored
	 * ImageSize (D43 = 20x20) survives. No name exclusions (A20 removed the last one).
	 */
	static void StyleCheckBoxForTheme(UCheckBox* CheckBox, bool bLight);

	/**
	 * ScrollBox: tint the three scrollbar THUMB states a mid grey that reads on either chrome (there is
	 * no scrollbar palette role); track/background stay asset-owned. Without this an overflowing list
	 * kept a dark thumb on light chrome and vice-versa.
	 */
	static void StyleScrollBoxForTheme(UScrollBox* ScrollBox, bool bLight);

	/**
	 * ProgressBar: §3.8 — Accent fill on a rounded InputBg track with a 1u InputBorder. The scrub bar
	 * behind the playback slider (ScrubFillBar) is the one exception and gets a flat SliderTrack track
	 * with an accent-at-35%-alpha fill instead.
	 */
	static void StyleProgressBarForTheme(UProgressBar* ProgressBar, bool bLight);

	/**
	 * EditableTextBox: the surface lives in FEditableTextBoxStyle, which no Border/Image pass can reach,
	 * so set the input roles EXPLICITLY — InputBg fill, InputBorder outline (only where one is authored),
	 * InputText on all three foregrounds. Never value-remap these: an edit box commonly leaves its
	 * foreground on an inheritance rule, and reading GetSpecifiedColor() off that yields the magenta
	 * sentinel, which a remap-write then bakes in. bApplyFont also lands the Font_Inter Mono 11 input
	 * face (theme-independent, so construct-only).
	 */
	static void StyleEditableTextBoxForTheme(UEditableTextBox* EditBox, bool bLight, bool bApplyFont);

	/**
	 * A6b-5 (2026-07-28): TextBlock — the one relocated helper that is still a VALUE REMAP rather than a
	 * role write, because there is no UMobiusThemedTextBlock and there will not be one: 192 matched design-time
	 * blocks would have to be reparented to buy back three near-identical greys, and that re-fires the
	 * delegate-binding trap. So the remap MOVES here instead of being rebuilt, scoped to one owner's tree and
	 * driven by an event. Evidence in _ClaudeHandoff/A6b5_TEXT_PLAN.md.
	 *
	 * The remap is safe to run repeatedly in one theme: TextMap has no cross-column collision, so a second
	 * pass in the same direction is a no-op (it is only lossy ACROSS a toggle, which the border migration
	 * already removed the teeth from). The neutral-white guard stays OFF, because white text IS a role.
	 *
	 * Reaches only plain UMG text blocks. UVerticalTextBlock, UFieldAndTextWidget and UButtonWithText's label
	 * are all raw STextBlock under a UWidget, not UTextBlock subclasses (there are none in the project), so
	 * this cannot re-acquire the self-theming widgets A6a deliberately dropped from the walk.
	 */
	static void StyleTextBlockForTheme(UTextBlock* TextBlock, bool bLight);

	/**
	 * A6b-5 (2026-07-29): plain UButton — the four state brushes, the ButtonText foreground, and the
	 * BackgroundColor multiplier. Lifted from the walk's UButton branch, which reduces EXACTLY to this once
	 * the ribbon-tab and UBaseButton paths are excluded, so the walk now delegates here rather than keeping
	 * a second copy free to drift.
	 *
	 * NO-OPS ON A UBaseButton, deliberately. Those already self-theme: UBaseButton binds OnThemeChanged in
	 * its own construct and re-runs RefreshThemedButtonStyle from HandleThemeChanged (BaseButton.cpp), and
	 * UButtonWithText extends that with RefreshRibbonAppearance. Styling them here as well would recreate
	 * the two-writers-fighting condition A6a and W2 removed — including the ribbon tab whose Normal brush
	 * carries a BP-managed active-tab material that a re-stamp silently wipes.
	 *
	 * Measured scope: 13 live plain buttons, ALL of them walk-dependent (unlike the borders, none were
	 * incidentally static) and all reachable through this pass, since every one is a design-time child. 8 of
	 * the 13 belong to USimulationPlayBar, which cannot take the themed base (module cycle) and is reached
	 * only by the recursion.
	 *
	 * The foreground write covers Normal/Hovered/Pressed and deliberately NOT Disabled — matching the walk,
	 * and confirmed against a live toggle where DisabledForeground does not move. It looks like an omission
	 * and is not one.
	 */
	static void StyleButtonForTheme(UButton* Button, bool bLight);

	/**
	 * A6b-5 (2026-07-29): UImage — the brush through the same four helpers a button's state brushes get.
	 * Lifted from the walk's UImage branch, which the walk now delegates to. No project class derives from
	 * UImage (grepped), so unlike the button case there is no self-theming family to exclude.
	 *
	 * Touches ONLY the brush. UImage::ColorAndOpacity is a separate multiplier applied on top and the walk
	 * never remapped it — measured across a toggle, it does not move on any live image. Adding it here would
	 * be a behaviour change dressed as a relocation.
	 *
	 * Measured scope: of 6 live images, 4 are walk-dependent (a brush tint + outline on a resource-less
	 * brush) and all 4 are design-time children, so this pass reaches them. The other 2 are material-backed
	 * by instances that none of the three material helpers claim, so a theme flip never touched them and
	 * deleting the walk cannot either.
	 */
	static void StyleImageForTheme(UImage* Image, bool bLight);

	/**
	 * A6b-6b (2026-07-31): plain UBorder — the LAST branch the walk still owned outright, and the reason
	 * A6b-6 could not simply delete it. ThemeStandardControlsInTree dispatched Slider/CheckBox/ScrollBox/
	 * ProgressBar/EditableTextBox/Image/Button/TextBlock and had NO border branch at all, so with the walk
	 * gone a plain UBorder would have had no theme writer of any kind. The walk now delegates here, same as
	 * it does for images and buttons, so there is only ever one copy.
	 *
	 * The load-bearing part is ThemeBackgroundBrush. THREE of the four live /WidgetMaterials/
	 * BackgroundMaterials/ cards sit on a plain UBorder — MI_FlowCounterBackgroundTop
	 * (WBP_FlowCounter.Border_1), MI_FlowCounterBackgroundBottom (WBP_FlowSectionCounter.Border_48) and
	 * MI_EgressBackground (WBP_ASET-RSET_Combined.Border_0) — measured per-widget, not assumed. Without
	 * this they render their baked colours. (The fourth, MI_PlayBarBackground, is on a UImage and was
	 * already covered by StyleImageForTheme.)
	 *
	 * NO-OPS ON A UMobiusThemedBorder, carrying forward the deleted walk's type guard: a themed border owns its fill
	 * and outline from a DECLARED role, and letting the value remap below near-match its light value
	 * against a different SurfaceMap row is the exact epsilon collision the rebuild exists to remove.
	 * Skipping them costs nothing measurable — no themed border carries a BackgroundMaterials instance
	 * (all four cards are plain borders or an image), so the material helper has nothing to do on one.
	 *
	 * The SurfaceMap remap + material folder swap + D169 collapse are carried for BEHAVIOUR PARITY, not
	 * because a casualty was measured for them: they are what a plain border gets today, and relocating a
	 * branch is not the place to change what it does. They remain the lossy value-matching path — every
	 * border reparented to UMobiusThemedBorder subtracts one more widget from it.
	 */
	void StyleBorderForTheme(UBorder* Border, bool bLight);

private:
	void ApplyTheme(bool bLight);
	/**
	 * Push the current palette into the MPC_UITheme Material Parameter Collection (new architecture:
	 * chrome/button/input materials sample it, so a theme switch updates them GPU-side with no walk).
	 * No-op until the MPC asset exists. See _ClaudeHandoff/PRD_ThemeSystemRework.md.
	 */
	void WriteThemeToMPC(bool bLight);
	/**
	 * A6b-6: count the live on-screen leaf widgets. NOT a theming pass — this is the startup ticker's only
	 * signal that the HUD has finished constructing, and it is the deleted walk's traversal with the styling
	 * call removed so the ticker's calibrated ">200" threshold still means what it meant. See the .cpp.
	 */
	int32 CountLiveLeafWidgets();
	/** Re-theme every in-world UWidgetComponent-hosted widget (flow-counter cards) — GetAllWidgetsOfClass
	 *  does not return world-space component widgets, so no viewport enumeration reaches them. */
	void ThemeInWorldWidgetComponents();
	/**
	 * Explicit per-theme colour for widgets the value remap cannot distinguish (dark-grey collapse at
	 * Epsilon, white-guard, or alpha roles — D25/D26 + P3/P4 EXPLICIT-REAPPLY queues), keyed by widget-name
	 * substring. Returns true when it fully handled the widget (caller then skips the generic remap for it).
	 *
	 * A6b-6: after the walk's deletion this has exactly ONE caller, StyleBorderForTheme, and is only ever
	 * passed a UBorder — which was always the only type it handled (it casts and early-outs). Of its 7 name
	 * rules only HeatmapColourBand is still live; every other name-matched widget has been reparented to
	 * UMobiusThemedBorder and is excluded by that caller's type guard before reaching here. Signature kept
	 * as UWidget* so a second caller does not have to pre-cast.
	 */
	bool ApplyNameRoleOverride(UWidget* Widget, bool bLight);
	/** Retint the shared SWS tab style + the "Mobius.Button" style-set entry in place. */
	void ApplySharedStyles(bool bLight);

	/**
	 * A6b-2 (2026-07-29): run ApplySharedStyles now, and again the moment the asset registry finishes
	 * scanning if it has not yet.
	 *
	 * This exists because ApplySharedStyles has ONE real dependency — a scanned asset registry, since it
	 * sweeps /Game/01_Dev/Widgets for SWS assets — and at subsystem Initialize that scan is usually still
	 * running, so the startup call themes ZERO assets. The old fix was to re-run it on every startup ticker
	 * pass and hope one of them landed after the scan. This waits for the actual event instead.
	 *
	 * A6b-6: the ticker still calls this, but now only as a BACKSTOP for the one-shot-broadcast race
	 * (OnFilesLoaded can complete between the IsLoadingAssets test and the bind below, and is then never
	 * delivered). The former reason — an ordering guard so the walk could not stamp disk-authored styles onto
	 * snapshot-at-construct buttons — died with the walk.
	 */
	void ApplySharedStylesWhenRegistryReady();

	EMobiusUITheme CurrentTheme = EMobiusUITheme::Dark;

	/**
	 * Startup one-shot re-theme ticker (FTSTicker core ticker, ~30 s window). Stored so Deinitialize
	 * can remove it: the core ticker is GLOBAL and outlives the PIE world, so if left registered it
	 * fires during world teardown and walks half-destroyed widgets → the combo SMenuAnchor delegate
	 * -access ensure (the "crash on PIE close"). Also reset when the ticker self-unregisters.
	 *
	 * A6b-2 expected this whole ticker to die with the walk in A6b-6. It did NOT, and the reason is the one
	 * thing the walk was doing that owner-pull cannot do for itself: the theme must be re-applied AFTER the
	 * HUD constructs, because the shared SWS assets are retinted asynchronously (asset-registry scan) and
	 * because widgets construct over several seconds behind shader compilation. What changed in A6b-6 is the
	 * MECHANISM — each pass now BROADCASTS OnThemeChanged instead of walking, which is what removes the
	 * launch-vs-toggle ordering asymmetry that let a startup walk overwrite an owner-pull write.
	 */
	FTSTicker::FDelegateHandle StartupThemeTickerHandle;

	/**
	 * Registry-ready hook for ApplySharedStyles. Held so Deinitialize can unbind: IAssetRegistry is
	 * MODULE-level and outlives the GameInstance exactly the way the core ticker does, and PIE builds a
	 * fresh subsystem per session — so leaving this bound would both stack a binding per run and fire into
	 * a dead subsystem, which is the same shape as the documented PIE-close crash above.
	 */
	FDelegateHandle AssetRegistryFilesLoadedHandle;

	/**
	 * True once ApplySharedStyles has run at a moment when the registry was NOT still scanning — i.e. once
	 * it has actually been able to see the SWS assets.
	 *
	 * Deliberately not "has ApplySharedStyles ever run": it is also called from ApplyTheme, so a theme
	 * toggle during load would otherwise set this while the scan was still in flight and suppress the
	 * fix-up. The ticker checks this flag unconditionally, which is what closes the race where the scan
	 * completes between the IsLoadingAssets() test and the OnFilesLoaded bind — that bind is a one-shot
	 * broadcast, so a missed one never fires at all.
	 */
	bool bSharedStylesAppliedAfterRegistryScan = false;
};
