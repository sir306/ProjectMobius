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
#include "HeatmapToggleRow.generated.h"

class UCheckBox;
class UTextBlock;

/**
 * Born-themed parent for WBP_ToggleHeatmapOnOff — one "Active heatmaps" row (a checkbox + a label).
 *
 * These rows are spawned dynamically at runtime, so the startup value-remap walk misses them and the
 * label renders off-palette (white). As a UMobiusThemedUserWidget it self-applies on NativeConstruct
 * and every OnThemeChanged: it runs the per-widget walk over its own tree (themes the checkbox) then
 * corrects the label colour to LabelText so it matches its sibling labels.
 *
 * Member names MUST match the existing widget names (TurnHeatmapOnOff / HeatmapName) so
 * BindWidgetOptional auto-binds without any WBP rename.
 */
UCLASS()
class MOBIUSWIDGETS_API UHeatmapToggleRow : public UMobiusThemedUserWidget
{
	GENERATED_BODY()

public:
	/** Row label (the heatmap's name); recoloured to LabelText on theme apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HeatmapName;

	/** On/off toggle for this heatmap; themed via the per-widget walk on theme apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> TurnHeatmapOnOff;

protected:
	//~ Begin UMobiusThemedUserWidget Interface
	virtual void ApplyMobiusTheme_Implementation() override;
	//~ End UMobiusThemedUserWidget Interface
};
