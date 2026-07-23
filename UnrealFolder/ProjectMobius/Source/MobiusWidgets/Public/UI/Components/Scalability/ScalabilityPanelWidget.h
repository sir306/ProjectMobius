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
#include "UI/Theme/MobiusThemedUserWidget.h"
#include "ScalabilityPanelWidget.generated.h"

class UImage;
class UTextBlock;

/**
 * Born-themed parent for WBP_CustomScalabilitySettings (the "Custom Display Settings" panel).
 *
 * The old runtime value-remap walk misses this panel (shown before/after a startup theme ticker,
 * and its background used the playbar material MI_PlayBarBackground which the walk can't retint).
 * As a UMobiusThemedUserWidget it self-applies on NativeConstruct and every OnThemeChanged:
 *  - the panel background image is replaced with a solid RibbonBg surface,
 *  - the panel title text is recoloured to PanelHeaderText.
 */
UCLASS()
class MOBIUSWIDGETS_API UScalabilityPanelWidget : public UMobiusThemedUserWidget
{
	GENERATED_BODY()

public:
	/** Panel surface. Its brush is replaced with a solid RibbonBg box (no material) on theme apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UImage> PanelBackgroundImage;

	/** Panel title ("Custom Display Settings"); recoloured to PanelHeaderText on theme apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PanelTitleText;

protected:
	//~ Begin UMobiusThemedUserWidget Interface
	virtual void ApplyMobiusTheme_Implementation() override;
	//~ End UMobiusThemedUserWidget Interface
};
