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
#include "Blueprint/UserWidget.h"
#include "UI/Theme/MobiusThemedUserWidget.h"  // A5: event-driven theming base
#include "ThemeToggleWidget.generated.h"

class UCheckBox;

/**
 * Light/dark theme toggle row for the settings (cog) panel. All logic is native so the widget
 * blueprint only needs a CheckBox named "ThemeToggleCheckBox" — no graph wiring.
 *
 * On construct it also re-applies a saved Light theme (deferred one tick so the full widget tree
 * exists), which is how the persisted theme choice survives restarts.
 */
UCLASS()
class MOBIUSWIDGETS_API UThemeToggleWidget : public UMobiusThemedUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

protected:
	/**
	 * A5 (2026-07-28): the pill self-themes. UUIThemeSubsystem::StyleCheckBoxForTheme deliberately SKIPS
	 * any checkbox named *ThemeToggle* — the standard rounded box would destroy a pill/track control — and
	 * until now the pill's two theme-dependent fills were landed by the value walk (the "field bg" and
	 * "accent" SurfaceMap pairs), which means it would have silently stopped theming when A6 deletes it.
	 * Now: unchecked track = InputBg, checked track = CheckboxCheckedBg, with the checked hover/press
	 * states derived from that accent so all three stay in one family per theme. Geometry (radii 2,
	 * outline width, ImageSize, padding) and the blue-grey track outline are asset-owned and untouched.
	 */
	virtual void ApplyMobiusTheme_Implementation() override;

	UFUNCTION()
	void HandleThemeCheckChanged(bool bIsChecked);

	/** Checked = light theme. */
	UPROPERTY(BlueprintReadOnly, Category = "MobiusWidget|Theme", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> ThemeToggleCheckBox;
};
