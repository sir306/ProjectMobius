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
#include "UIThemeSubsystem.generated.h"

class UWidget;
struct FSlateBrush;

UENUM(BlueprintType)
enum class EMobiusUITheme : uint8
{
	Dark,
	Light
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
	/** Retint the shared SWS tab style + the "Mobius.Button" style-set entry in place. */
	void ApplySharedStyles(bool bLight);

	EMobiusUITheme CurrentTheme = EMobiusUITheme::Dark;
};
