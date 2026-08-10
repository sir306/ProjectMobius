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
#include "Components/ComboBoxString.h"
#include "MobiusThemedComboBox.generated.h"

class UUIThemeSubsystem;

/**
 * Theme-aware UComboBoxString (theme rebuild, migration W1).
 *
 * Two mechanisms, both crash-safe w.r.t. the FMRSWRecursiveAccessDetector ensure that a naive combo
 * restyle used to trip (verified against UE 5.5 Slate source — the SMenuAnchor delegates the ensure
 * guards are read ONLY in the menu open/close paths, never in paint/invalidate):
 *
 *  1. BORN THEMED — RebuildWidget() styles WidgetStyle / ItemStyle / ForegroundColor / Font MEMBERS
 *     from the palette *before* Super builds the SComboBox (via UUIThemeSubsystem::StyleComboBoxForBuild).
 *     The closed-combo surface is a flat RoundedBox (fill=InputBg, 1px InputBorder outline); rows +
 *     dropdown-border also themed; font pinned to Font_Inter (see StyleComboBoxForBuild).
 *
 *  2. LIVE FOLLOW — binds UUIThemeSubsystem::OnThemeChanged (safe now that W2 removed the per-click
 *     reapply firehose: it fires only on a DELIBERATE toggle, dropdown closed). HandleThemeChanged
 *     re-runs StyleComboBoxForBuild (SComboBox holds &WidgetStyle/&ItemStyle by pointer; SetWidgetStyle/
 *     SetItemStyle only Invalidate(Layout), never the SMenuAnchor) and relands the selected-value TEXT
 *     directly on its STextBlock (ForegroundColor is inert live — the inner SButton foreground wins).
 *     Rows + dropdown border update on the next open; surface + selected text update immediately.
 *
 * Residual: the down-arrow glyph tint is baked by-value at construct (like ForegroundColor) and only
 * corrects on the next reconstruct — drive it via ComboButtonStyle.DownArrowImage.TintColor if it matters.
 *
 * Being a UComboBoxString subclass, it satisfies any `meta=(BindWidget)` property typed as UComboBoxString
 * (is-a), so consumer WBPs only need their combo widget's class swapped to this — no C++ binding changes.
 */
UCLASS()
class MOBIUSWIDGETS_API UMobiusThemedComboBox : public UComboBoxString
{
	GENERATED_BODY()

protected:
	//~ Begin UWidget Interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	//~ End UWidget Interface

	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface

	//~ Begin UComboBoxString Interface
	/** Re-apply the explicit selected-value text colour after the base regenerates the display block. */
	virtual void HandleSelectionChanged(TSharedPtr<FString> Item, ESelectInfo::Type SelectionType) override;

	/**
	 * Stamp the CURRENT-theme selected/row text colour onto every generated block. This is the single
	 * regeneration choke point BOTH selection paths funnel through (UComboBoxString::UpdateOrGenerateWidget
	 * -> HandleGenerateWidget). The interactive path also runs HandleSelectionChanged (-> ApplySelectedTextColor),
	 * but the PROGRAMMATIC SetSelectedOption / SetSelectedIndex path takes the base's built-Slate branch which
	 * calls UpdateOrGenerateWidget DIRECTLY and NEVER routes through HandleSelectionChanged (those setters and
	 * UpdateOrGenerateWidget are non-virtual, so this virtual is the only seam). Without this, a combo re-selected
	 * on activation regenerates a block with no ColorAndOpacity that falls back to the core "NormalText" colour
	 * (outside the palette) and never follows a later toggle. Reads the LIVE theme (not cached) so it is correct
	 * even if a toggle happened while the combo was disabled. Const-safe; SetColorAndOpacity Assigns +
	 * Invalidate(Paint) only — no SComboButton / SMenuAnchor touch.
	 */
	virtual TSharedRef<SWidget> HandleGenerateWidget(TSharedPtr<FString> Item) const override;
	//~ End UComboBoxString Interface

private:
	/**
	 * Bound to UUIThemeSubsystem::OnThemeChanged (fires only on a deliberate toggle now that W2 removed the
	 * per-click reapply). Re-themes the style MEMBERS + relands the selected-value colour LIVE, crash-free:
	 * it never touches the SMenuAnchor delegates the FMRSWRecursiveAccessDetector ensure guards (verified
	 * against UE 5.5 Slate source — SMenuAnchor delegates are read only in the open/close paths).
	 */
	UFUNCTION()
	void HandleThemeChanged();

	/**
	 * Recolour the live selected-value STextBlock directly. ForegroundColor is inert live (the inner SButton
	 * foreground wins before paint reaches the block), so SetColorAndOpacity on the generated block is the
	 * only live path; it Assigns + Invalidate(Paint) only — no SMenuAnchor touch.
	 */
	void ApplySelectedTextColor();

	/**
	 * Bind OnThemeChanged if not already bound and the subsystem is now available. Idempotent (AddUnique) and
	 * cheap. Called from RebuildWidget AND from HandleGenerateWidget so a combo first built before the
	 * GameInstance subsystem existed (GetThemeSubsystem() == null at that build) re-establishes live-follow the
	 * first time it is regenerated on (re)activation — closing the "never follows a later toggle" race. const so
	 * it can run from the const HandleGenerateWidget seam; binding a delegate is not observable widget state.
	 */
	void EnsureThemeBound() const;

	/**
	 * Current theme from the subsystem's live CurrentTheme (NOT UserProjectSettings — that is still the OLD
	 * value during OnThemeChanged, which fires mid-ApplyTheme before the persist). Falls back to the
	 * persisted flag when there is no subsystem (design-time preview).
	 */
	bool ResolveIsLight() const;

	UUIThemeSubsystem* GetThemeSubsystem() const;

	/** Cached current-theme selected-value text colour; applied on build / selection / toggle. */
	FSlateColor ThemeTextColor = FSlateColor(FLinearColor::White);

	/** True while bound to OnThemeChanged, so teardown unbinds exactly once. Mutable: the const
	 *  HandleGenerateWidget/EnsureThemeBound seam updates this bookkeeping (a delegate bind is not
	 *  observable widget state). */
	mutable bool bThemeBound = false;

	/**
	 * The subsystem this combo resolved while it still could — i.e. from RebuildWidget / EnsureThemeBound,
	 * where GetWorld() works. Cached because a dropdown ROW is generated into the menu stack, where GetWorld()
	 * returns null: without this, GetThemeSubsystem() had to fall back to sweeping GEngine's world contexts
	 * and would return whichever context matched FIRST, which is not necessarily the PIE one whose theme the
	 * user is actually looking at.
	 *
	 * Caching the SUBSYSTEM is safe where caching a COLOUR was not: this is a live object, so GetTheme() is
	 * read fresh at every use. An earlier attempt cached the resolved colour instead and froze each combo at
	 * its construct-time theme. Weak, so a torn-down PIE subsystem simply falls back to the lookups.
	 */
	mutable TWeakObjectPtr<UUIThemeSubsystem> CachedThemeSubsystem;

	// (A temporary set of reflected diagnostics lived here while the row colour was being chased. They are
	// gone because the row now BINDS its colour rather than baking one, so there is no generation-time value
	// left to record — the failure mode they were added to observe cannot occur.)


};
