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
#include "UI/Theme/MobiusThemePalette.h"  // EMobiusPaletteRole (by-value UFUNCTION param on GetThemeColor)
#include "MobiusThemedUserWidget.generated.h"

class UUIThemeSubsystem;

/**
 * Event-driven theming base for Mobius UMG widgets (new architecture — see
 * _ClaudeHandoff/PRD_ThemeSystemRework.md §4, §8). Widgets theme THEMSELVES instead of being
 * value-walked by the subsystem:
 *  - on NativeConstruct: cache the UUIThemeSubsystem, bind its OnThemeChanged, pull the palette once,
 *  - on OnThemeChanged: re-pull the palette (event, NOT poll, NOT colour value-match),
 *  - on NativeDestruct: unbind.
 *
 * Subclasses (C++ or Blueprint) override ApplyMobiusTheme() and set their role colours from
 * GetThemeColor(). The base ApplyMobiusTheme_Implementation() is intentionally empty.
 */
UCLASS(Abstract)
class MOBIUSWIDGETS_API UMobiusThemedUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/**
	 * Pull this widget's role colours for the CURRENT theme and apply them. Called once on construct
	 * and again on every OnThemeChanged. Override in C++ (_Implementation) or Blueprint. Base is empty.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Mobius|Theme")
	void ApplyMobiusTheme();

protected:
	/** Bound to the subsystem's OnThemeChanged; just re-runs ApplyMobiusTheme(). */
	UFUNCTION()
	void HandleThemeChanged();

	/** The theme subsystem this widget cached on construct, or a live fetch fallback. May be null. */
	UUIThemeSubsystem* GetThemeSubsystem() const;

	/** Current-theme colour for Role via the subsystem; FLinearColor::Black if no subsystem. */
	UFUNCTION(BlueprintPure, Category = "Mobius|Theme")
	FLinearColor GetThemeColor(EMobiusPaletteRole Role) const;

private:
	/** Cached on NativeConstruct so NativeDestruct can unbind even mid-teardown. Weak: never keeps it alive. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UUIThemeSubsystem> CachedThemeSubsystem;
};
