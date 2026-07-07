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

void UThemeToggleWidget::HandleThemeCheckChanged(const bool bIsChecked)
{
	if (UUIThemeSubsystem* ThemeSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UUIThemeSubsystem>() : nullptr)
	{
		ThemeSubsystem->SetTheme(bIsChecked ? EMobiusUITheme::Light : EMobiusUITheme::Dark);
	}
}
