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

#include "UI/Components/ThemeToggleWidget.h"

#include "Components/CheckBox.h"
#include "TimerManager.h"
#include "UI/Theme/UIThemeSubsystem.h"

void UThemeToggleWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UUIThemeSubsystem* ThemeSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UUIThemeSubsystem>() : nullptr;
	if (!ThemeSubsystem)
	{
		return;
	}

	if (ThemeToggleCheckBox)
	{
		// SetIsChecked does not broadcast OnCheckStateChanged, so this cannot re-trigger a theme apply.
		ThemeToggleCheckBox->SetIsChecked(ThemeSubsystem->GetTheme() == EMobiusUITheme::Light);
		ThemeToggleCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UThemeToggleWidget::HandleThemeCheckChanged);
	}

	if (ThemeSubsystem->GetTheme() == EMobiusUITheme::Light)
	{
		// Widgets construct with dark design-time defaults; repaint the saved theme once the
		// full tree has finished constructing.
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(ThemeSubsystem, &UUIThemeSubsystem::ReapplyTheme));
		}
	}
}

void UThemeToggleWidget::ApplyMobiusTheme_Implementation()
{
	Super::ApplyMobiusTheme_Implementation();

	if (!ThemeToggleCheckBox)
	{
		return;
	}

	// Only the two FILLS are theme-dependent; everything else about the pill (radii 2, the 1u blue-grey
	// track outline, ImageSize, padding) is authored geometry the walk never matched, so it stays put.
	const FLinearColor TrackOff = GetThemeColor(EMobiusPaletteRole::InputBg);
	const FLinearColor TrackOn = GetThemeColor(EMobiusPaletteRole::CheckboxCheckedBg);
	// Checked hover / press derived from the accent rather than authored per state: x1.22 / x0.74
	// reproduces the asset's dark-theme values (0.135,0.405,0.750 and 0.070,0.240,0.500) to within ~0.02,
	// and gives light mode a matching pair instead of the dark accent's bright-blue hover leaking into it.
	auto ScaleRGB = [](const FLinearColor& In, const float Scale)
	{
		return FLinearColor(FMath::Min(In.R * Scale, 1.0f), FMath::Min(In.G * Scale, 1.0f),
			FMath::Min(In.B * Scale, 1.0f), In.A);
	};

	FCheckBoxStyle Style = ThemeToggleCheckBox->GetWidgetStyle();
	Style.UncheckedImage.TintColor = FSlateColor(TrackOff);
	Style.UncheckedHoveredImage.TintColor = FSlateColor(TrackOff);
	Style.UncheckedPressedImage.TintColor = FSlateColor(TrackOff);
	Style.CheckedImage.TintColor = FSlateColor(TrackOn);
	Style.CheckedHoveredImage.TintColor = FSlateColor(ScaleRGB(TrackOn, 1.22f));
	Style.CheckedPressedImage.TintColor = FSlateColor(ScaleRGB(TrackOn, 0.74f));
	ThemeToggleCheckBox->SetWidgetStyle(Style);
}

void UThemeToggleWidget::HandleThemeCheckChanged(const bool bIsChecked)
{
	if (UUIThemeSubsystem* ThemeSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UUIThemeSubsystem>() : nullptr)
	{
		ThemeSubsystem->SetTheme(bIsChecked ? EMobiusUITheme::Light : EMobiusUITheme::Dark);
	}
}
