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

#include "UI/Components/MobiusThemedComboBox.h"

#include "Engine/Engine.h"                    // GEngine->GetGameUserSettings() + GetWorldContexts()
#include "Engine/World.h"                     // FWorldContext::World() in the deep subsystem sweep
#include "Engine/Font.h"                      // UFont (Font_Inter)
#include "Engine/GameInstance.h"              // GetSubsystem<UUIThemeSubsystem>
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateColor.h"
#include "UI/Theme/UIThemeSubsystem.h"        // StyleComboBoxForBuild + OnThemeChanged + MobiusThemePalette
#include "UserConfig/UserProjectSettings.h"   // persisted light/dark flag (fallback / design-time)
#include "Widgets/Input/SComboBox.h"          // MyComboBox
#include "Widgets/Layout/SBox.h"              // ComboBoxContent (selected-value host)
#include "Widgets/Text/STextBlock.h"          // SetColorAndOpacity on the selected-value block

namespace
{
	/**
	 * Stamp Colour on every STextBlock in a generated combo row, however deeply it is wrapped.
	 *
	 * The shallow `GetType() == "STextBlock"` test this replaces only ever matched the ENGINE default row.
	 * Every Mobius combo binds OnGenerateWidgetEvent to a Blueprint generator, so the returned widget is the
	 * BP widget's Slate (a panel, a border, or an SObjectWidget for a UUserWidget) and the text sits one or
	 * more levels down — the stamp was skipped 100% of the time on exactly the combos that need it.
	 *
	 * With nothing stamping it, the text falls back to the SComboBox's ForegroundColor, and that is
	 * CONSTRUCT-ONLY: `ComboBoxString.h:88` says "only set at construction and is not modifiable at
	 * runtime", and RebuildWidget reads it once (`ComboBoxString.cpp:108`). A combo built while the app was
	 * LIGHT therefore keeps InputText-light (near-black 0.016) forever, which is why the dropdown text
	 * rendered black in dark mode while ItemStyle.TextColor / SelectedTextColor / the four button
	 * foregrounds all measured correct at 0.624.
	 *
	 * Deliberately unconditional: a combo ROW is chrome, so uniform themed text is the intent. If a future
	 * row ever needs its own colour (a coloured swatch label, say), give it an explicit opt-out rather than
	 * restoring the type check — the type check is what hid this.
	 */
	void StampRowTextColour(const TSharedRef<SWidget>& Widget, const FSlateColor& Colour, const int32 Depth = 0)
	{
		// Combo rows are shallow; the guard exists so a pathological custom row cannot spin here.
		if (Depth > 8)
		{
			return;
		}
		if (Widget->GetType() == TEXT("STextBlock"))
		{
			StaticCastSharedRef<STextBlock>(Widget)->SetColorAndOpacity(Colour);
			return;
		}
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				StampRowTextColour(Children->GetChildAt(Index), Colour, Depth + 1);
			}
		}
	}
}

UUIThemeSubsystem* UMobiusThemedComboBox::GetThemeSubsystem() const
{
	// ORDER MATTERS, and getting it wrong is a hazard I introduced and then had to undo. The widget's OWN
	// world is the only authoritative answer, so it is tried FIRST and refreshes the cache. Checking the
	// cache first meant that one bad resolve stuck forever — and the sweep at the bottom can absolutely
	// produce a bad resolve, because it returns whichever world context matches first, which may be a stale
	// or non-PIE instance whose theme is not the one on screen.
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UUIThemeSubsystem* Theme = GameInstance->GetSubsystem<UUIThemeSubsystem>())
			{
				CachedThemeSubsystem = Theme;
				return Theme;
			}
		}
	}
	// The instance THIS widget resolved from its own world earlier (RebuildWidget / EnsureThemeBound, where
	// GetWorld() still worked). Correct by construction for a row generated into the menu stack.
	if (UUIThemeSubsystem* Cached = CachedThemeSubsystem.Get())
	{
		return Cached;
	}
	// 2026-08-10: GetWorld() does NOT resolve for a combo whose row is being generated into the menu stack,
	// and the old version returned null there — which sent ResolveIsLight() into its UUserProjectSettings
	// branch. That flag is persisted only AFTER OnThemeChanged broadcasts, so it lags by one toggle, and the
	// rows came out inverted rather than merely unthemed. Sweep GEngine's world contexts as well, the same
	// fallback UFlowCounterListRow::ResolveThemeSubsystem already uses for a widget spawned into a list.
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				if (UGameInstance* GameInstance = World->GetGameInstance())
				{
					if (UUIThemeSubsystem* Theme = GameInstance->GetSubsystem<UUIThemeSubsystem>())
					{
						// Deliberately NOT cached: this is a guess, and caching a guess is what made a wrong
						// resolve permanent last time.
						return Theme;
					}
				}
			}
		}
	}
	return nullptr;
}

bool UMobiusThemedComboBox::ResolveIsLight() const
{
	// The subsystem's CurrentTheme is set BEFORE OnThemeChanged broadcasts, so it is the correct live value
	// during a toggle; UserProjectSettings is only persisted AFTER the broadcast (it lags by one toggle).
	if (const UUIThemeSubsystem* Theme = GetThemeSubsystem())
	{
		return Theme->GetTheme() == EMobiusUITheme::Light;
	}
	const UUserProjectSettings* Settings =
		Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr);
	return (!Settings || Settings->GetUseLightUITheme());
}

TSharedRef<SWidget> UMobiusThemedComboBox::RebuildWidget()
{
	const bool bLight = ResolveIsLight();

	// Surface brush (flat InputBg RoundedBox, no material), dropdown row colours + the menu-border
	// outline → the style MEMBERS.
	// Members only; the (not-yet-built) live widget is untouched.
	UUIThemeSubsystem::StyleComboBoxForBuild(this, bLight);

	ThemeTextColor = FSlateColor(MobiusThemePalette::Color(EMobiusPaletteRole::InputText, bLight));

	// Build-time foreground (by value): the base uses it for the down-arrow tint and as the selected-text
	// fallback. The selected TEXT is also set explicitly after Super (foreground can't be updated live), but
	// this still colours the arrow at construct. InitForegroundColor is the pre-build-only setter.
	InitForegroundColor(ThemeTextColor);

	// FONT — pin to the app field font (Font_Inter Regular 10, == "Mobius.Text.Field") so a recreated combo
	// never falls back to the Roboto Bold 16 default. InitFont is the pre-build-only setter; both the closed
	// selected text and the generated dropdown rows read this Font.
	if (UFont* Inter = LoadObject<UFont>(nullptr, TEXT("/Game/01_Dev/Widgets/Fonts/Font_Inter.Font_Inter")))
	{
		InitFont(FSlateFontInfo(Inter, 10, FName(TEXT("Regular"))));
	}

	TSharedRef<SWidget> Built = Super::RebuildWidget();

	// The selected-value block exists only after Super. Colour it explicitly: UseForeground would resolve to
	// the inner SButton foreground, which can't be updated on a live toggle.
	ApplySelectedTextColor();

	// LIVE theme-follow. Safe to bind now that W2 removed the per-click reapply firehose: OnThemeChanged
	// fires only on a deliberate toggle, and HandleThemeChanged never touches the SMenuAnchor delegates the
	// FMRSWRecursiveAccessDetector ensure guards (verified against UE 5.5 Slate source). Idempotent; also
	// retried from HandleGenerateWidget so a combo built before the subsystem existed still binds later.
	EnsureThemeBound();

	return Built;
}

void UMobiusThemedComboBox::HandleThemeChanged()
{
	// The delegate can fire before RebuildWidget or after ReleaseSlateResources — bail if there is no live
	// widget (also matches the base's own MyComboBox.IsValid() guard pattern).
	if (!MyComboBox.IsValid())
	{
		return;
	}
	const bool bLight = ResolveIsLight();

	// Re-theme the style MEMBERS. StyleComboBoxForBuild's SetWidgetStyle/SetItemStyle only Invalidate(Layout)
	// on the SComboBox (never the SMenuAnchor); the surface material is idempotent, and the row colours +
	// menu-border outline are read live via the style pointers on the next paint/open.
	UUIThemeSubsystem::StyleComboBoxForBuild(this, bLight);

	ThemeTextColor = FSlateColor(MobiusThemePalette::Color(EMobiusPaletteRole::InputText, bLight));

	// Selected-value text: the one piece that needs an explicit live set.
	ApplySelectedTextColor();

	// DROPDOWN ROWS — force regeneration, or the toggle never reaches them.
	//
	// This is the mechanism three earlier attempts missed. SComboBox generates each row ONCE via
	// HandleGenerateWidget and the SListView REUSES that widget on every subsequent open, so the row's text
	// colour is fixed at whatever theme was live the first time that combo's list was built and no later
	// toggle can move it. Nothing about the stamp was wrong; it simply never ran again.
	//
	// That is also why different combos disagreed with each other rather than all being wrong the same way:
	// each froze independently, at whatever theme happened to be current when IT was first opened. The
	// render-mode combo froze light while the colour-mode and flow-counter-type combos froze dark, which
	// reads like conflicting theming and is really one cache with three different birthdays.
	//
	// RefreshOptions() forwards to SComboBox::RefreshOptions -> RequestListRefresh, which discards the
	// generated rows so the next open re-enters HandleGenerateWidget and re-stamps against the CURRENT
	// theme. It refreshes the option widgets only; SelectedItem is untouched, so the closed value does not
	// change and no OnSelectionChanged fires.
	RefreshOptions();
}

void UMobiusThemedComboBox::ApplySelectedTextColor()
{
	// ComboBoxContent (protected SBox from the base) hosts the generated selected-value widget.
	// SetColorAndOpacity Assigns + Invalidate(Paint) only — no SComboButton / SMenuAnchor involvement.
	//
	// 2026-08-10: this used to require the child to BE an STextBlock, which is only true on the engine
	// default path. With OnGenerateWidgetEvent bound — as it is on every Mobius combo — the child is the
	// Blueprint generator's widget and the text is nested inside it, so the recolour silently did nothing
	// and the CLOSED value kept the construct-only ForegroundColor. Recurse instead, same as the row path.
	if (!ComboBoxContent.IsValid())
	{
		return;
	}
	if (FChildren* Children = ComboBoxContent->GetChildren())
	{
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			StampRowTextColour(Children->GetChildAt(Index), ThemeTextColor);
		}
	}
}

void UMobiusThemedComboBox::HandleSelectionChanged(TSharedPtr<FString> Item, ESelectInfo::Type SelectionType)
{
	// The base regenerates the selected-value STextBlock (with no ColorAndOpacity), dropping our explicit
	// colour — re-apply it afterwards. (HandleGenerateWidget below also stamps it, so this is belt-and-braces
	// for the interactive path; the programmatic SetSelectedOption path bypasses this method entirely.)
	Super::HandleSelectionChanged(Item, SelectionType);
	ApplySelectedTextColor();
}

TSharedRef<SWidget> UMobiusThemedComboBox::HandleGenerateWidget(TSharedPtr<FString> Item) const
{
	// A combo first built before the GameInstance subsystem existed re-establishes live-follow HERE, the
	// first time it is regenerated on (re)activation — the programmatic "built" path funnels through
	// UpdateOrGenerateWidget -> HandleGenerateWidget and never runs RebuildWidget again.
	EnsureThemeBound();

	// BUILD THE ROW OURSELVES when no Blueprint generator is bound.
	//
	// This is the fix after four failed attempts, all of which tried to RECOLOUR the widget the base handed
	// back. The base's default row is SNew(STextBlock).Text(...).Font(GetFont()) with **no ColorAndOpacity**,
	// so it resolves its colour by FOREGROUND INHERITANCE from the SComboBox — whose ForegroundColor the
	// engine fixes at construction and never updates (ComboBoxString.h:88). Recolouring after the fact fought
	// that inheritance instead of replacing it. Constructing the row with an explicit colour removes the
	// inheritance from the picture entirely, which is the only version of this that cannot regress.
	//
	// IsBound() is the authoritative test for a Blueprint generator. Do NOT infer it from Python: the repr of
	// OnGenerateWidgetEvent prints the delegate SIGNATURE type (FGenerateWidgetForString) and looks identical
	// whether or not anything is bound — reading it as "bound" is what sent the earlier diagnosis wrong.
	// A BP-supplied row is still respected, and still gets the recursive stamp below as a best effort.
	if (!OnGenerateWidgetEvent.IsBound())
	{
		// BIND the colour, do not bake it.
		//
		// Owner, 2026-08-10: "it renders correctly for the start theme and then incorrectly when i switch."
		// That is the whole bug in one sentence, and it invalidates every previous attempt at once — each of
		// them computed a colour AT GENERATION TIME, and the row widget is generated once and then reused.
		// SListView caches its generated row widgets, so RefreshOptions()/RequestListRefresh does not
		// re-enter this function for an already-built row; whatever value was correct on the first open stays
		// there through every later toggle. The failures were never about WHICH colour was computed.
		//
		// A TAttribute lambda is re-evaluated on every paint, so the row reads the CURRENT theme each frame
		// and there is nothing left to go stale — no cache, no regeneration dependency, no construction-time
		// snapshot. Cost is one palette lookup per visible row per paint, and only while the menu is open.
		//
		// Weak pointer: the menu can outlive the combo during teardown, and the lambda must not resurrect it.
		TWeakObjectPtr<const UMobiusThemedComboBox> WeakSelf(this);
		return SNew(STextBlock)
			.Text(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty())
			.Font(GetFont())
			.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([WeakSelf]() -> FSlateColor
			{
				const UMobiusThemedComboBox* Self = WeakSelf.Get();
				if (!Self)
				{
					return FSlateColor(FLinearColor::White);
				}
				if (const UUIThemeSubsystem* Theme = Self->GetThemeSubsystem())
				{
					return FSlateColor(MobiusThemePalette::Color(
						EMobiusPaletteRole::InputText, Theme->GetTheme() == EMobiusUITheme::Light));
				}
				// ThemeTextColor is re-stated on every toggle, so it still tracks the theme.
				return Self->ThemeTextColor;
			}));
	}

	TSharedRef<SWidget> Row = Super::HandleGenerateWidget(Item);

	// The base default row is SNew(STextBlock).Text(...).Font(Font) with NO ColorAndOpacity, so it falls back
	// to the core "NormalText" style colour — outside the Mobius palette and untouched by the live style path.
	// Stamp the CURRENT-theme InputText colour so the programmatic SetSelectedOption "built" branch (which
	// regenerates the selected-value block and bypasses the HandleSelectionChanged override where
	// ApplySelectedTextColor would have run) is born correctly themed and follows every later toggle. Read the
	// live theme directly so it is right even if a toggle happened while this combo was disabled.
	// SetColorAndOpacity Assigns + Invalidate(Paint) only.
	//
	// 2026-08-10: this used to test `Row->GetType() == "STextBlock"` and skip anything else as "a WBP-supplied
	// custom row". Every Mobius combo binds a Blueprint generator, so that test failed on all four of them and
	// the stamp never ran — see StampRowTextColour above for why that surfaced as black text in dark mode.
	//
	// Stamp ONLY from the LIVE subsystem, and only if it actually resolves.
	//
	// Two earlier attempts got this wrong in instructive ways. Using ResolveIsLight() inverted the colours,
	// because in the menu stack it fell through to the UUserProjectSettings flag that lags one toggle. Using
	// the cached ThemeTextColor moved the problem rather than fixing it: that member is only refreshed when
	// the widget itself rebuilds or handles a toggle, so combos with different build/toggle histories froze
	// at different values — which is why the render-mode combo was correct in light while the colour-mode and
	// flow-counter-type combos were correct in dark. Same defect, opposite sign, one cache.
	//
	// Reading the subsystem here cannot go stale, now that GetThemeSubsystem sweeps GEngine's world contexts.
	// And if it still cannot be resolved, DO NOTHING: ItemStyle.TextColor / SelectedTextColor already carry
	// the correct per-theme value (measured 0.016 light / 0.624 dark), so leaving the row alone degrades to
	// "correct" whereas guessing a theme degrades to "inverted".
	if (const UUIThemeSubsystem* Theme = GetThemeSubsystem())
	{
		const bool bLight = Theme->GetTheme() == EMobiusUITheme::Light;
		StampRowTextColour(Row, FSlateColor(MobiusThemePalette::Color(EMobiusPaletteRole::InputText, bLight)));
	}
	else
	{
		// Last resort, and it must still be a THEMED value. Skipping the stamp entirely leaves the row on the
		// SComboBox foreground, which RebuildWidget fixed at construct time and the engine never updates
		// (ComboBoxString.h:88) — that is light InputText, i.e. black text on a dark dropdown. ThemeTextColor
		// is at least re-stated on every toggle, so it is wrong only if this combo has never handled one.
		StampRowTextColour(Row, ThemeTextColor);
	}
	return Row;
}

void UMobiusThemedComboBox::EnsureThemeBound() const
{
	if (bThemeBound)
	{
		return;
	}
	if (UUIThemeSubsystem* Theme = GetThemeSubsystem())
	{
		// const_cast: AddUniqueDynamic needs a non-const receiver; the widget's observable state is unchanged
		// (bThemeBound is mutable bookkeeping). AddUnique => bind at most once; ReleaseSlateResources/
		// BeginDestroy unbind exactly once (guarded on bThemeBound).
		Theme->OnThemeChanged.AddUniqueDynamic(
			const_cast<UMobiusThemedComboBox*>(this), &UMobiusThemedComboBox::HandleThemeChanged);
		bThemeBound = true;
	}
}

void UMobiusThemedComboBox::ReleaseSlateResources(bool bReleaseChildren)
{
	if (bThemeBound)
	{
		if (UUIThemeSubsystem* Theme = GetThemeSubsystem())
		{
			Theme->OnThemeChanged.RemoveDynamic(this, &UMobiusThemedComboBox::HandleThemeChanged);
		}
		bThemeBound = false;
	}
	Super::ReleaseSlateResources(bReleaseChildren);
}

void UMobiusThemedComboBox::BeginDestroy()
{
	// Safety net: unbind before destruction so a later broadcast never fires into a torn-down widget
	// (the subsystem outlives this widget).
	if (bThemeBound)
	{
		if (UUIThemeSubsystem* Theme = GetThemeSubsystem())
		{
			Theme->OnThemeChanged.RemoveDynamic(this, &UMobiusThemedComboBox::HandleThemeChanged);
		}
		bThemeBound = false;
	}
	Super::BeginDestroy();
}
