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
#include "Components/Border.h"
#include "UI/Theme/MobiusThemePalette.h"
#include "MobiusThemedBorder.generated.h"

class UUIThemeSubsystem;

/**
 * A6b (2026-07-28): a UBorder that carries its palette ROLE as authored data and repaints itself on
 * OnThemeChanged. This is the replacement for the two mechanisms the walker used to colour Borders, and
 * it is what lets both of them be deleted outright:
 *
 *  - `SurfaceMap` VALUE-MATCHING (46 borders) — the walker compared a Border's current colour against a
 *    table of light/dark pairs and swapped it. That is the direct cause of the "fighting loop": any two
 *    roles whose values collide at Epsilon share a bucket, so a widget could walk 0.0243 -> 0.791 -> 0.913
 *    across successive toggles and never come back. A declared role cannot drift, and is idempotent.
 *  - `ApplyNameRoleOverride`'s substring table (36 borders) — deterministic, but keyed on widget NAME, so
 *    a rename in the designer silently un-themes the widget with nothing to catch it (§6c's warning).
 *
 * Owner ruling 2026-07-28: these Borders CANNOT take A3's material route. Material-backing kills the
 * corner mask and the 1px outline, and 114 of the 265 authored Borders are ROUNDED_BOX with radii, 92
 * carrying an outline. So the role lives on the widget instead, and the brush stays flat.
 *
 * USAGE: reparent the Border in the designer (right-click -> Replace With), then set FillRole and, if the
 * Border draws an outline, tick bThemeOutline and set OutlineRole. Geometry — radii, outline WIDTH,
 * padding, DrawAs, ImageSize — stays asset-owned and is never touched here, matching the owner's
 * geometry-from-the-asset / colour-from-C++ split.
 */
UCLASS(meta = (DisplayName = "Mobius Themed Border"))
class MOBIUSWIDGETS_API UMobiusThemedBorder : public UBorder
{
	GENERATED_BODY()

public:
	/** Untick for a Border whose fill is data-driven (e.g. the LoS colour chips) and only wants an outline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Theme")
	bool bThemeFill = true;

	/** Palette role for the fill. Applied to BrushColor; the brush tint is forced white (see .cpp, D169). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Theme", meta = (EditCondition = "bThemeFill"))
	EMobiusPaletteRole FillRole = EMobiusPaletteRole::RibbonBg;

	/** Tick for Borders that draw a 1px hairline/outline; leave off and the authored outline is untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Theme")
	bool bThemeOutline = false;

	/** Palette role for OutlineSettings.Color. The outline WIDTH stays asset-owned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Theme", meta = (EditCondition = "bThemeOutline"))
	EMobiusPaletteRole OutlineRole = EMobiusPaletteRole::ChipOutline;

	/** Re-pull this Border's roles for the current theme and apply them. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Theme")
	void RefreshThemedBorder();

protected:
	virtual void OnWidgetRebuilt() override;
	virtual void BeginDestroy() override;
	virtual void SynchronizeProperties() override;

	/** Bound to the subsystem's OnThemeChanged. */
	UFUNCTION()
	void HandleThemeChanged();

private:
	/** Resolve + cache the subsystem. Null at design time (no game instance), which is a no-op, not an error. */
	UUIThemeSubsystem* ResolveThemeSubsystem();

	/** Weak so a cached subsystem never keeps anything alive; lets BeginDestroy unbind mid-teardown. */
	TWeakObjectPtr<UUIThemeSubsystem> CachedThemeSubsystem;
};
