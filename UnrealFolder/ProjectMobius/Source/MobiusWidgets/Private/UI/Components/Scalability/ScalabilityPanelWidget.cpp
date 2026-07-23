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

#include "UI/Components/Scalability/ScalabilityPanelWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Styling/SlateBrush.h"

void UScalabilityPanelWidget::ApplyMobiusTheme_Implementation()
{
	// Replace the (playbar-material) background with a solid RibbonBg surface the walk couldn't retint.
	if (PanelBackgroundImage)
	{
		FSlateBrush Brush = PanelBackgroundImage->GetBrush();
		// Rounded box with a 1px WindowBorder outline so the panel has a visible themed edge (was a flat
		// Box tinted RibbonBg with no border).
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.SetResourceObject(nullptr);
		Brush.TintColor = FSlateColor(GetThemeColor(EMobiusPaletteRole::RibbonBg));
		Brush.OutlineSettings.Color = FSlateColor(GetThemeColor(EMobiusPaletteRole::WindowBorder));
		Brush.OutlineSettings.Width = 1.0f;
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(6.0, 6.0, 6.0, 6.0);
		PanelBackgroundImage->SetBrush(Brush);
	}

	// Recolour the panel title to the current-theme header text colour.
	if (PanelTitleText)
	{
		PanelTitleText->SetColorAndOpacity(FSlateColor(GetThemeColor(EMobiusPaletteRole::PanelHeaderText)));
	}
}
