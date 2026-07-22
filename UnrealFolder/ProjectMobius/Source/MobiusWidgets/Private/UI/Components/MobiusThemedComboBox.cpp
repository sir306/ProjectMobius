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

#include "UI/Components/MobiusThemedComboBox.h"

#include "Engine/Engine.h"                    // GEngine->GetGameUserSettings()
#include "Engine/Font.h"                      // UFont (Font_Inter)
#include "Engine/GameInstance.h"              // GetSubsystem<UUIThemeSubsystem>
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateColor.h"
#include "UI/Theme/UIThemeSubsystem.h"        // StyleComboBoxForBuild + OnThemeChanged + MobiusThemePalette
#include "UserConfig/UserProjectSettings.h"   // persisted light/dark flag (fallback / design-time)
#include "Widgets/Input/SComboBox.h"          // MyComboBox
#include "Widgets/Layout/SBox.h"              // ComboBoxContent (selected-value host)
#include "Widgets/Text/STextBlock.h"          // SetColorAndOpacity on the selected-value block

UUIThemeSubsystem* UMobiusThemedComboBox::GetThemeSubsystem() const
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<UUIThemeSubsystem>();
		}
	}
	return nullptr;
}

bool UMobiusThemedComboBox::ResolveIsLight() const
{
	// The subsystem's CurrentTheme is set BEFORE OnThemeChanged broadcasts, so it is the correct live value
	// during a toggle; UserProjectSettings is only persisted AFTER the broadcast (it lags by one toggle).
	if (const UUIThemeSubsystem* Theme = GetThemeSubsystem())
	{
		return Theme->GetTheme() == EMobiusUITheme::Light;
	}
	const UUserProjectSettings* Settings =
		Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr);
	return (!Settings || Settings->GetUseLightUITheme());
}

TSharedRef<SWidget> UMobiusThemedComboBox::RebuildWidget()
{
	const bool bLight = ResolveIsLight();

	// Surface brush (M_MobiusInput), dropdown row colours + the menu-border outline → the style MEMBERS.
	// Members only; the (not-yet-built) live widget is untouched.
	UUIThemeSubsystem::StyleComboBoxForBuild(this, bLight);

	ThemeTextColor = FSlateColor(MobiusThemePalette::Color(EMobiusPaletteRole::InputText, bLight));

	// Build-time foreground (by value): the base uses it for the down-arrow tint and as the selected-text
	// fallback. The selected TEXT is also set explicitly after Super (foreground can't be updated live), but
	// this still colours the arrow at construct. InitForegroundColor is the pre-build-only setter.
	InitForegroundColor(ThemeTextColor);

	// FONT — pin to the app field font (Font_Inter Regular 10, == "Mobius.Text.Field") so a recreated combo
	// never falls back to the Roboto Bold 16 default. InitFont is the pre-build-only setter; both the closed
	// selected text and the generated dropdown rows read this Font.
	if (UFont* Inter = LoadObject<UFont>(nullptr, TEXT("/Game/01_Dev/Widgets/Fonts/Font_Inter.Font_Inter")))
	{
		InitFont(FSlateFontInfo(Inter, 10, FName(TEXT("Regular"))));
	}

	TSharedRef<SWidget> Built = Super::RebuildWidget();

	// The selected-value block exists only after Super. Colour it explicitly: UseForeground would resolve to
	// the inner SButton foreground, which can't be updated on a live toggle.
	ApplySelectedTextColor();

	// LIVE theme-follow. Safe to bind now that W2 removed the per-click reapply firehose: OnThemeChanged
	// fires only on a deliberate toggle, and HandleThemeChanged never touches the SMenuAnchor delegates the
	// FMRSWRecursiveAccessDetector ensure guards (verified against UE 5.5 Slate source). AddUnique → bind once.
	if (UUIThemeSubsystem* Theme = GetThemeSubsystem())
	{
		Theme->OnThemeChanged.AddUniqueDynamic(this, &UMobiusThemedComboBox::HandleThemeChanged);
		bThemeBound = true;
	}

	return Built;
}

void UMobiusThemedComboBox::HandleThemeChanged()
{
	// The delegate can fire before RebuildWidget or after ReleaseSlateResources — bail if there is no live
	// widget (also matches the base's own MyComboBox.IsValid() guard pattern).
	if (!MyComboBox.IsValid())
	{
		return;
	}
	const bool bLight = ResolveIsLight();

	// Re-theme the style MEMBERS. StyleComboBoxForBuild's SetWidgetStyle/SetItemStyle only Invalidate(Layout)
	// on the SComboBox (never the SMenuAnchor); the surface material is idempotent, and the row colours +
	// menu-border outline are read live via the style pointers on the next paint/open.
	UUIThemeSubsystem::StyleComboBoxForBuild(this, bLight);

	ThemeTextColor = FSlateColor(MobiusThemePalette::Color(EMobiusPaletteRole::InputText, bLight));

	// Selected-value text: the one piece that needs an explicit live set.
	ApplySelectedTextColor();
}

void UMobiusThemedComboBox::ApplySelectedTextColor()
{
	// ComboBoxContent (protected SBox from the base) hosts the generated selected-value widget. On the
	// default path (no OnGenerateWidgetEvent) that child is an STextBlock; recolour it directly.
	// SetColorAndOpacity Assigns + Invalidate(Paint) only — no SComboButton / SMenuAnchor involvement.
	if (!ComboBoxContent.IsValid())
	{
		return;
	}
	FChildren* Children = ComboBoxContent->GetChildren();
	if (Children && Children->Num() > 0)
	{
		TSharedRef<SWidget> Content = Children->GetChildAt(0);
		if (Content->GetType() == TEXT("STextBlock"))
		{
			StaticCastSharedRef<STextBlock>(Content)->SetColorAndOpacity(ThemeTextColor);
		}
	}
}

void UMobiusThemedComboBox::HandleSelectionChanged(TSharedPtr<FString> Item, ESelectInfo::Type SelectionType)
{
	// The base regenerates the selected-value STextBlock (with no ColorAndOpacity), dropping our explicit
	// colour — re-apply it afterwards.
	Super::HandleSelectionChanged(Item, SelectionType);
	ApplySelectedTextColor();
}

void UMobiusThemedComboBox::ReleaseSlateResources(bool bReleaseChildren)
{
	if (bThemeBound)
	{
		if (UUIThemeSubsystem* Theme = GetThemeSubsystem())
		{
			Theme->OnThemeChanged.RemoveDynamic(this, &UMobiusThemedComboBox::HandleThemeChanged);
		}
		bThemeBound = false;
	}
	Super::ReleaseSlateResources(bReleaseChildren);
}

void UMobiusThemedComboBox::BeginDestroy()
{
	// Safety net: unbind before destruction so a later broadcast never fires into a torn-down widget
	// (the subsystem outlives this widget).
	if (bThemeBound)
	{
		if (UUIThemeSubsystem* Theme = GetThemeSubsystem())
		{
			Theme->OnThemeChanged.RemoveDynamic(this, &UMobiusThemedComboBox::HandleThemeChanged);
		}
		bThemeBound = false;
	}
	Super::BeginDestroy();
}
