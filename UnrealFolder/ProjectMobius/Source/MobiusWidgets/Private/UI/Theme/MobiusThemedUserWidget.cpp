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

#include "UI/Theme/MobiusThemedUserWidget.h"

#include "UI/Theme/UIThemeSubsystem.h"

void UMobiusThemedUserWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UUIThemeSubsystem* ThemeSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UUIThemeSubsystem>() : nullptr;
	CachedThemeSubsystem = ThemeSubsystem;

	if (ThemeSubsystem)
	{
		// Event-driven: re-pull on every theme apply instead of being value-walked by the subsystem.
		ThemeSubsystem->OnThemeChanged.AddUniqueDynamic(this, &UMobiusThemedUserWidget::HandleThemeChanged);

		// Pull the current palette once so the widget matches the active theme at construct.
		ApplyMobiusTheme();
	}
}

void UMobiusThemedUserWidget::NativeDestruct()
{
	if (UUIThemeSubsystem* ThemeSubsystem = CachedThemeSubsystem.Get())
	{
		ThemeSubsystem->OnThemeChanged.RemoveDynamic(this, &UMobiusThemedUserWidget::HandleThemeChanged);
	}

	Super::NativeDestruct();
}

void UMobiusThemedUserWidget::HandleThemeChanged()
{
	ApplyMobiusTheme();
}

void UMobiusThemedUserWidget::ApplyMobiusTheme_Implementation()
{
	// Base is intentionally empty. Subclasses (C++ override or Blueprint) pull their role colours
	// via GetThemeColor() and apply them here.
}

UUIThemeSubsystem* UMobiusThemedUserWidget::GetThemeSubsystem() const
{
	if (UUIThemeSubsystem* Cached = CachedThemeSubsystem.Get())
	{
		return Cached;
	}

	// Fallback for callers before NativeConstruct has cached it (const method — cannot backfill cache).
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UUIThemeSubsystem>() : nullptr;
}

FLinearColor UMobiusThemedUserWidget::GetThemeColor(const EMobiusPaletteRole Role) const
{
	if (const UUIThemeSubsystem* ThemeSubsystem = GetThemeSubsystem())
	{
		return ThemeSubsystem->GetPaletteColor(Role);
	}

	return FLinearColor::Black;
}
