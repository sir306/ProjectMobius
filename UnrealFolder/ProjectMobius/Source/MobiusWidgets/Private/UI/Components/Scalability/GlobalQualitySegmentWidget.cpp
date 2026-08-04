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

#include "UI/Components/Scalability/GlobalQualitySegmentWidget.h"

#include "Style/MobiusStyle.h"
#include "Styling/SlateTypes.h"
#include "Subsystems/PerformanceUtilSubsystem.h"
#include "UI/Components/ButtonWithText.h"
#include "UI/Theme/UIThemeSubsystem.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/**
	 * Deliberately NOT reusing ThemeToggleWidget.cpp's identically-shaped helpers: both files sit in the
	 * same module and MobiusWidgets builds with unity, so two anonymous namespaces exporting
	 * `MakeSegmentBrush` / `GSegmentPadding` collide in one translation unit. Distinct names, same values.
	 */
	constexpr float GQualityContainerRadius = 3.0f;
	constexpr float GQualitySegmentRadius = GQualityContainerRadius - 1.0f;

	/** Equal Normal/Pressed padding — a shrinking pressed box drops clicks (StabilisePressedPadding). */
	const FMargin GQualitySegmentPadding(8.0f, 3.0f);

	/** Flat rounded fill, no outline: the hairline belongs to the container the segments sit in. */
	FSlateBrush MakeQualitySegmentBrush(const FLinearColor& Fill, const FVector4& Radii)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.SetResourceObject(nullptr);
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = Radii;
		Brush.OutlineSettings.Width = 0.0f;
		Brush.OutlineSettings.Color = FSlateColor(FLinearColor::Transparent);
		return Brush;
	}

	/** The nine per-feature categories, matching UScalabilityPanelWidget's matrix. */
	constexpr EScalabilityCategories GQualityCategories[] = {
		ESc_GlobalIllumination,
		ESc_PostProcessing,
		ESc_Shadows,
		ESc_AntiAliasing,
		ESc_Reflections,
		ESc_Textures,
		ESc_Effects,
		ESc_Shading,
		ESc_ViewDistance
	};
}

void UGlobalQualitySegmentWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	// The brief's copy, set here rather than in the .uasset so the labels cannot drift between the two.
	// "Ultra" is the user-facing name for the engine's Epic tier; the widget names keep the engine word.
	if (LowSetting_Button)
	{
		LowSetting_Button->SetButtonWithNewText(NSLOCTEXT("MobiusGlobalQuality", "TierLow", "Low"));
	}
	if (MedSetting_Button)
	{
		MedSetting_Button->SetButtonWithNewText(NSLOCTEXT("MobiusGlobalQuality", "TierMedium", "Medium"));
	}
	if (HighSetting_Button)
	{
		HighSetting_Button->SetButtonWithNewText(NSLOCTEXT("MobiusGlobalQuality", "TierHigh", "High"));
	}
	if (EpicSetting_Button)
	{
		EpicSetting_Button->SetButtonWithNewText(NSLOCTEXT("MobiusGlobalQuality", "TierUltra", "Ultra"));
	}
	if (CustomSetting_Button)
	{
		CustomSetting_Button->SetButtonWithNewText(NSLOCTEXT("MobiusGlobalQuality", "TierCustom", "Custom"));
	}
}

void UGlobalQualitySegmentWidget::NativeConstruct()
{
	// Before Super: Super themes the tree then calls ApplyMobiusTheme, which restyles the segments — so the
	// derived active tier has to be known by then or the first paint marks the wrong segment.
	auto ClearPalette = [](UButtonWithText* Segment)
	{
		if (Segment)
		{
			// This widget owns the segment's state colours. Without this, UBaseButton re-stamps flat
			// ButtonBg/ButtonText on construct AND on every OnThemeChanged, erasing the accent fill.
			Segment->bFollowThemePalette = false;
		}
	};
	ClearPalette(LowSetting_Button);
	ClearPalette(MedSetting_Button);
	ClearPalette(HighSetting_Button);
	ClearPalette(EpicSetting_Button);
	ClearPalette(CustomSetting_Button);

	if (LowSetting_Button)
	{
		LowSetting_Button->OnClicked.AddUniqueDynamic(this, &UGlobalQualitySegmentWidget::HandleLowClicked);
	}
	if (MedSetting_Button)
	{
		MedSetting_Button->OnClicked.AddUniqueDynamic(this, &UGlobalQualitySegmentWidget::HandleMediumClicked);
	}
	if (HighSetting_Button)
	{
		HighSetting_Button->OnClicked.AddUniqueDynamic(this, &UGlobalQualitySegmentWidget::HandleHighClicked);
	}
	if (EpicSetting_Button)
	{
		EpicSetting_Button->OnClicked.AddUniqueDynamic(this, &UGlobalQualitySegmentWidget::HandleUltraClicked);
	}
	if (CustomSetting_Button)
	{
		CustomSetting_Button->OnClicked.AddUniqueDynamic(this, &UGlobalQualitySegmentWidget::HandleCustomClicked);
	}

	DisplayedLevel = DeriveLowestAppliedLevel();

	Super::NativeConstruct();
}

void UGlobalQualitySegmentWidget::ApplyMobiusTheme_Implementation()
{
	Super::ApplyMobiusTheme_Implementation();

	RestyleSegments();
}

void UGlobalQualitySegmentWidget::RefreshActiveSegment()
{
	DisplayedLevel = DeriveLowestAppliedLevel();
	RestyleSegments();
}

TEnumAsByte<EScalabilitySettings> UGlobalQualitySegmentWidget::DeriveLowestAppliedLevel() const
{
	// Same design-time / game-world guard the rest of the scalability family uses.
	UWorld* World = IsDesignTime() ? nullptr : GetWorld();
	UPerformanceUtilSubsystem* Performance = (World && World->IsGameWorld())
		? World->GetSubsystem<UPerformanceUtilSubsystem>()
		: nullptr;
	if (!Performance)
	{
		return DisplayedLevel;
	}

	// The enum is ordered Low(0) < Medium < High < Epic < Cinematic(4), so "lowest" is a numeric min.
	// ESsl_Default(5) is Hidden and higher than every real tier, so it must not win a min() — skip it.
	uint8 Lowest = static_cast<uint8>(EScalabilitySettings::ESsl_Cinematic);
	bool bFoundAny = false;
	for (const EScalabilityCategories Category : GQualityCategories)
	{
		const uint8 Level = static_cast<uint8>(Performance->GetScalabilityLevel(Category));
		if (Level > static_cast<uint8>(EScalabilitySettings::ESsl_Cinematic))
		{
			continue;
		}
		Lowest = FMath::Min(Lowest, Level);
		bFoundAny = true;
	}

	return bFoundAny
		? TEnumAsByte<EScalabilitySettings>(static_cast<EScalabilitySettings>(Lowest))
		: DisplayedLevel;
}

void UGlobalQualitySegmentWidget::HandleLowClicked()
{
	ApplyGlobalTier(EGlobalScalabilitySettings::EGss_Low);
}

void UGlobalQualitySegmentWidget::HandleMediumClicked()
{
	ApplyGlobalTier(EGlobalScalabilitySettings::EGss_Medium);
}

void UGlobalQualitySegmentWidget::HandleHighClicked()
{
	ApplyGlobalTier(EGlobalScalabilitySettings::EGss_High);
}

void UGlobalQualitySegmentWidget::HandleUltraClicked()
{
	ApplyGlobalTier(EGlobalScalabilitySettings::EGss_Epic);
}

void UGlobalQualitySegmentWidget::HandleCustomClicked()
{
	// Custom is not a tier — it opens the Custom Display window and changes nothing on its own.
	OnCustomQualityRequested.Broadcast();
}

void UGlobalQualitySegmentWidget::ApplyGlobalTier(const TEnumAsByte<EGlobalScalabilitySettings> Tier)
{
	UWorld* World = IsDesignTime() ? nullptr : GetWorld();
	if (UPerformanceUtilSubsystem* Performance = (World && World->IsGameWorld())
		? World->GetSubsystem<UPerformanceUtilSubsystem>()
		: nullptr)
	{
		// Writes all nine per-feature categories; resolution is untouched (it is not a quality tier).
		Performance->UpdateGlobalScalabilitySetting(Tier);
	}

	RefreshActiveSegment();
}

void UGlobalQualitySegmentWidget::RestyleSegments() const
{
	StyleSegment(LowSetting_Button, DisplayedLevel == EScalabilitySettings::ESsl_Low,
		/*bFirst*/true, /*bLast*/false, /*bAccentLabel*/false);
	StyleSegment(MedSetting_Button, DisplayedLevel == EScalabilitySettings::ESsl_Medium,
		false, false, false);
	StyleSegment(HighSetting_Button, DisplayedLevel == EScalabilitySettings::ESsl_High,
		false, false, false);
	// Cinematic has no segment of its own in the Settings panel (the brief's five are Low..Ultra + Custom),
	// so a Cinematic set of per-feature values shows on Ultra — the nearest tier the control can express.
	StyleSegment(EpicSetting_Button,
		DisplayedLevel == EScalabilitySettings::ESsl_Epic || DisplayedLevel == EScalabilitySettings::ESsl_Cinematic,
		false, false, false);
	StyleSegment(CustomSetting_Button, /*bActive*/false, false, /*bLast*/true, /*bAccentLabel*/true);
}

void UGlobalQualitySegmentWidget::StyleSegment(UButtonWithText* Segment, const bool bActive,
	const bool bFirst, const bool bLast, const bool bAccentLabel) const
{
	if (!Segment)
	{
		return;
	}

	// Outer corners only, so the five read as one bar inside the container's radius.
	FVector4 Radii(0.0, 0.0, 0.0, 0.0);
	if (bFirst)
	{
		Radii.X = GQualitySegmentRadius;
		Radii.W = GQualitySegmentRadius;
	}
	if (bLast)
	{
		Radii.Y = GQualitySegmentRadius;
		Radii.Z = GQualitySegmentRadius;
	}

	const FLinearColor Accent = GetThemeColor(EMobiusPaletteRole::Accent);
	// Inactive is TRANSPARENT, not a fill of its own: the container's InputBg is the surface behind an
	// unselected segment and one surface cannot double-paint itself.
	const FLinearColor Fill = bActive ? Accent : FLinearColor::Transparent;
	const FLinearColor Hover = bActive ? Accent : GetThemeColor(EMobiusPaletteRole::ButtonHoverBg);
	const FLinearColor Press = bActive ? Accent : GetThemeColor(EMobiusPaletteRole::ButtonPressedBg);

	FLinearColor LabelColour = GetThemeColor(EMobiusPaletteRole::TabInactiveText);
	if (bActive)
	{
		LabelColour = FLinearColor::White;
	}
	else if (bAccentLabel)
	{
		// The mockup draws Custom as an accent-coloured label on the panel surface — a link, not a tier.
		LabelColour = Accent;
	}
	const FSlateColor LabelSlateColour(LabelColour);

	FButtonStyle Style = Segment->GetStyle();
	Style.SetNormal(MakeQualitySegmentBrush(Fill, Radii));
	Style.SetHovered(MakeQualitySegmentBrush(Hover, Radii));
	Style.SetPressed(MakeQualitySegmentBrush(Press, Radii));
	Style.SetDisabled(MakeQualitySegmentBrush(Fill, Radii));
	Style.NormalForeground = LabelSlateColour;
	Style.HoveredForeground = LabelSlateColour;
	Style.PressedForeground = LabelSlateColour;
	Style.DisabledForeground = LabelSlateColour;
	// EQUAL by construction — an asymmetric pressed padding shrinks the hit rect mid-press and the click is
	// silently dropped.
	Style.NormalPadding = GQualitySegmentPadding;
	Style.PressedPadding = GQualitySegmentPadding;
	Segment->SetStyle(Style);

	// Weight is the other active signal (600 vs 400). Size and face stay on the Mobius ramp.
	if (Segment->MyButtonText.IsValid())
	{
		FSlateFontInfo Font = FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Label").Font;
		Font.TypefaceFontName = bActive ? FName("SemiBold") : FName("Regular");
		Segment->MyButtonText->SetFont(Font);
	}
	// Explicit: RefreshThemedLabelStyle no longer runs for a button with bFollowThemePalette cleared, so
	// this is the only label-colour writer here.
	Segment->ApplyThemedLabelColor(LabelColour);
}
