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

#include "UI/Components/HeatmapToggleRow.h"

#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "UI/Theme/UIThemeSubsystem.h"

void UHeatmapToggleRow::ApplyMobiusTheme_Implementation()
{
	// Walk this row's own tree so the checkbox picks up the current theme (rows are spawned late and
	// are skipped by the startup live-widget walk).
	if (UUIThemeSubsystem* ThemeSubsystem = GetThemeSubsystem())
	{
		ThemeSubsystem->ReapplyToUserWidget(this);
	}

	// Correct the off-palette (white) label AFTER the walk so it matches its sibling labels.
	if (HeatmapName)
	{
		HeatmapName->SetColorAndOpacity(FSlateColor(GetThemeColor(EMobiusPaletteRole::LabelText)));
	}
}
