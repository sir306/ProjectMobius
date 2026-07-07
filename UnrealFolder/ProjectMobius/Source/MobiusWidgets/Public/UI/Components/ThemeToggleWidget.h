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
class MOBIUSWIDGETS_API UThemeToggleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

protected:
	UFUNCTION()
	void HandleThemeCheckChanged(bool bIsChecked);

	/** Checked = light theme. */
	UPROPERTY(BlueprintReadOnly, Category = "MobiusWidget|Theme", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> ThemeToggleCheckBox;
};
