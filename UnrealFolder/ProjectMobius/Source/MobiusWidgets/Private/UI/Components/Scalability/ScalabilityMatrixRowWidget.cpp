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

#include "UI/Components/Scalability/ScalabilityMatrixRowWidget.h"

#include "Components/CheckBox.h"
#include "Components/TextBlock.h"

void UScalabilityMatrixRowWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	// Designer-visible copy comes from the instance property, so a row's label and its category are set in
	// one place instead of the label living in the .uasset and the category in the details panel.
	if (RowLabelText && !RowLabel.IsEmpty())
	{
		RowLabelText->SetText(RowLabel);
	}
}

void UScalabilityMatrixRowWidget::NativeConstruct()
{
	// Bind before Super: Super themes the tree and calls ApplyMobiusTheme, and RefreshCellStates below
	// writes check states that the theme pass then styles.
	if (LowCheckBox)
	{
		LowCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UScalabilityMatrixRowWidget::HandleLowToggled);
	}
	if (MediumCheckBox)
	{
		MediumCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UScalabilityMatrixRowWidget::HandleMediumToggled);
	}
	if (HighCheckBox)
	{
		HighCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UScalabilityMatrixRowWidget::HandleHighToggled);
	}
	if (UltraCheckBox)
	{
		UltraCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UScalabilityMatrixRowWidget::HandleUltraToggled);
	}
	if (CinematicCheckBox)
	{
		CinematicCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UScalabilityMatrixRowWidget::HandleCinematicToggled);
	}

	RefreshCellStates();

	Super::NativeConstruct();
}

void UScalabilityMatrixRowWidget::ApplyMobiusTheme_Implementation()
{
	Super::ApplyMobiusTheme_Implementation();

	// Explicit role rather than leaving it to StyleTextBlockForTheme's authored-colour remap: a matrix row
	// label is body copy on the panel surface, and the remap would key off whatever grey the .uasset shipped.
	// Colour only. The font is authored into WBP_ScalabilityMatrixRow (owner decision, 2026-08-04) — the
	// theme walk re-tints text but never sets FSlateFontInfo, so an unauthored label would render at UMG's
	// Roboto 24 default and blow out the 30px row.
	if (RowLabelText)
	{
		RowLabelText->SetColorAndOpacity(FSlateColor(GetThemeColor(EMobiusPaletteRole::LabelText)));
	}
}

void UScalabilityMatrixRowWidget::SetStagedLevel(const TEnumAsByte<EScalabilitySettings> NewLevel)
{
	StagedLevel = NewLevel;
	RefreshCellStates();
}

void UScalabilityMatrixRowWidget::HandleLowToggled(const bool bIsChecked)
{
	HandleCellToggled(EScalabilitySettings::ESsl_Low, bIsChecked);
}

void UScalabilityMatrixRowWidget::HandleMediumToggled(const bool bIsChecked)
{
	HandleCellToggled(EScalabilitySettings::ESsl_Medium, bIsChecked);
}

void UScalabilityMatrixRowWidget::HandleHighToggled(const bool bIsChecked)
{
	HandleCellToggled(EScalabilitySettings::ESsl_High, bIsChecked);
}

void UScalabilityMatrixRowWidget::HandleUltraToggled(const bool bIsChecked)
{
	HandleCellToggled(EScalabilitySettings::ESsl_Epic, bIsChecked);
}

void UScalabilityMatrixRowWidget::HandleCinematicToggled(const bool bIsChecked)
{
	HandleCellToggled(EScalabilitySettings::ESsl_Cinematic, bIsChecked);
}

void UScalabilityMatrixRowWidget::HandleCellToggled(const TEnumAsByte<EScalabilitySettings> CellLevel, const bool bIsChecked)
{
	if (bSuppressCellCallbacks)
	{
		return;
	}

	if (!bIsChecked)
	{
		// Radio semantics: a row always has a level. Unticking the active cell would leave the row
		// meaningless, so put the tick back and change nothing.
		if (CellLevel == StagedLevel)
		{
			RefreshCellStates();
		}
		return;
	}

	if (CellLevel == StagedLevel)
	{
		// Already the staged level (e.g. the user re-clicked it). Still normalise the other four.
		RefreshCellStates();
		return;
	}

	StagedLevel = CellLevel;
	RefreshCellStates();
	OnLevelStaged.Broadcast(ScalabilityCategory, StagedLevel);
}

void UScalabilityMatrixRowWidget::RefreshCellStates()
{
	// SetIsChecked fires OnCheckStateChanged, which would re-enter HandleCellToggled and fight this write.
	bSuppressCellCallbacks = true;
	ForEachCell([this](UCheckBox* Cell, const TEnumAsByte<EScalabilitySettings> CellLevel)
	{
		Cell->SetIsChecked(CellLevel == StagedLevel);
	});
	bSuppressCellCallbacks = false;
}

void UScalabilityMatrixRowWidget::ForEachCell(
	TFunctionRef<void(UCheckBox*, TEnumAsByte<EScalabilitySettings>)> Visitor) const
{
	// ESsl_Epic is the engine's name for the tier the UI labels "Ultra"; ESsl_Default is deliberately absent
	// (it is Hidden in the enum and is not a pickable cell).
	const TPair<UCheckBox*, TEnumAsByte<EScalabilitySettings>> Cells[] = {
		{LowCheckBox, EScalabilitySettings::ESsl_Low},
		{MediumCheckBox, EScalabilitySettings::ESsl_Medium},
		{HighCheckBox, EScalabilitySettings::ESsl_High},
		{UltraCheckBox, EScalabilitySettings::ESsl_Epic},
		{CinematicCheckBox, EScalabilitySettings::ESsl_Cinematic}
	};

	for (const TPair<UCheckBox*, TEnumAsByte<EScalabilitySettings>>& Cell : Cells)
	{
		if (Cell.Key)
		{
			Visitor(Cell.Key, Cell.Value);
		}
	}
}
