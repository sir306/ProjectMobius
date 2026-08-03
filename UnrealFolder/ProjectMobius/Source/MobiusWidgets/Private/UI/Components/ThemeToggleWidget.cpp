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

#include "Blueprint/WidgetTree.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Style/MobiusStyle.h"
#include "Styling/SlateTypes.h"
#include "TimerManager.h"
#include "UI/Components/ButtonWithText.h"
#include "UI/Components/MobiusThemedBorder.h"
#include "UI/Theme/UIThemeSubsystem.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** Container radius from the mockup (border-radius: 3px). Segments inset by the 1px hairline. */
	constexpr float GContainerCornerRadius = 3.0f;
	constexpr float GSegmentCornerRadius = GContainerCornerRadius - 1.0f;

	/** Mockup seam between segments is a 1px rule; a UBorder with no child sizes to its padding. */
	constexpr float GSeamHalfWidth = 0.5f;

	/** Equal Normal/Pressed padding — a shrinking pressed box drops clicks (see StabilisePressedPadding). */
	const FMargin GSegmentPadding(8.0f, 3.0f);

	/** Flat rounded fill with no outline: the hairline belongs to the container, not to a segment. */
	FSlateBrush MakeSegmentBrush(const FLinearColor& Fill, const FVector4& Radii)
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
}

void UThemeToggleWidget::NativeConstruct()
{
	// BEFORE Super: see the header. Super themes the standard controls in this tree and then calls
	// ApplyMobiusTheme, and both of those need the final tree — segments present, checkbox gone.
	BuildSegmentedControl();

	Super::NativeConstruct();

	UUIThemeSubsystem* ThemeSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UUIThemeSubsystem>() : nullptr;
	if (!ThemeSubsystem)
	{
		return;
	}

	if (ThemeSubsystem->GetTheme() == EMobiusUITheme::Light)
	{
		// Widgets construct with dark design-time defaults; repaint the saved theme once the
		// full tree has finished constructing. THEME PERSISTENCE across restarts — not styling.
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(ThemeSubsystem, &UUIThemeSubsystem::ReapplyTheme));
		}
	}
}

void UThemeToggleWidget::BuildSegmentedControl()
{
	if (SegmentContainer)
	{
		return; // already built — NativeConstruct can run again after a remove/add from the viewport
	}
	if (!WidgetTree)
	{
		return;
	}

	// The asset's root row. Falling back through the vestigial checkbox's parent and then the tree root
	// keeps this working if the row is ever renamed in the designer, which is exactly the failure mode
	// the widget-NAME special cases this task deleted used to have.
	UPanelWidget* Row = ThemeToggleRow;
	if (!Row && ThemeToggleCheckBox)
	{
		Row = ThemeToggleCheckBox->GetParent();
	}
	if (!Row)
	{
		Row = Cast<UPanelWidget>(WidgetTree->RootWidget);
	}
	if (!Row)
	{
		return;
	}

	// A20: the pill is gone. Nothing binds or styles the checkbox any more, so take it off screen.
	if (ThemeToggleCheckBox)
	{
		ThemeToggleCheckBox->RemoveFromParent();
	}

	// ---- container: hairline-bordered well, rounded 3px, fill = the inactive-segment surface ----
	SegmentContainer = WidgetTree->ConstructWidget<UMobiusThemedBorder>(
		UMobiusThemedBorder::StaticClass(), TEXT("ThemeSegmentContainer"));
	if (!SegmentContainer)
	{
		return;
	}
	{
		FSlateBrush Background;
		Background.DrawAs = ESlateBrushDrawType::RoundedBox;
		Background.SetResourceObject(nullptr);
		// White TINT with the colour in BrushColor — SBorder paints TintColor * BrushColor, and two
		// non-white values multiply into near-black (the D169 double-tint trap).
		Background.TintColor = FSlateColor(FLinearColor::White);
		Background.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Background.OutlineSettings.CornerRadii = FVector4(GContainerCornerRadius, GContainerCornerRadius,
			GContainerCornerRadius, GContainerCornerRadius);
		Background.OutlineSettings.Width = 1.0f;
		SegmentContainer->SetBrush(Background);
	}
	SegmentContainer->bThemeFill = true;
	SegmentContainer->FillRole = EMobiusPaletteRole::InputBg;
	SegmentContainer->bThemeOutline = true;
	SegmentContainer->OutlineRole = EMobiusPaletteRole::ButtonBorder;
	// 1px inset so a segment fill cannot paint over the hairline it sits inside.
	SegmentContainer->SetPadding(FMargin(1.0f));

	UHorizontalBox* SegmentBox = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("ThemeSegmentBox"));
	if (!SegmentBox)
	{
		return;
	}
	SegmentContainer->AddChild(SegmentBox);

	LightSegment = MakeSegment(SegmentBox, TEXT("LightSegment"),
		NSLOCTEXT("MobiusThemeToggle", "ThemeSegmentLight", "Light"));
	if (LightSegment)
	{
		LightSegment->OnClicked.AddDynamic(this, &UThemeToggleWidget::HandleLightSegmentClicked);
	}

	// ---- seam: the mockup's 1px rule between segments ----
	if (UMobiusThemedBorder* Seam = WidgetTree->ConstructWidget<UMobiusThemedBorder>(
		UMobiusThemedBorder::StaticClass(), TEXT("ThemeSegmentSeam")))
	{
		FSlateBrush SeamBrush;
		SeamBrush.DrawAs = ESlateBrushDrawType::Image;
		SeamBrush.SetResourceObject(nullptr);
		SeamBrush.TintColor = FSlateColor(FLinearColor::White);
		Seam->SetBrush(SeamBrush);
		Seam->bThemeFill = true;
		Seam->FillRole = EMobiusPaletteRole::PanelDivider;
		Seam->bThemeOutline = false;
		// No child, so the desired size IS the padding: 1px wide, height from the Fill alignment below.
		Seam->SetPadding(FMargin(GSeamHalfWidth, 0.0f, GSeamHalfWidth, 0.0f));
		if (UHorizontalBoxSlot* SeamSlot = Cast<UHorizontalBoxSlot>(SegmentBox->AddChild(Seam)))
		{
			SeamSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			SeamSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}

	DarkSegment = MakeSegment(SegmentBox, TEXT("DarkSegment"),
		NSLOCTEXT("MobiusThemeToggle", "ThemeSegmentDark", "Dark"));
	if (DarkSegment)
	{
		DarkSegment->OnClicked.AddDynamic(this, &UThemeToggleWidget::HandleDarkSegmentClicked);
	}

	// Added last so the row's own layout is only invalidated once.
	if (UHorizontalBoxSlot* ContainerSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(SegmentContainer)))
	{
		// Fill the width the checkbox pill used to right-align into, so the control reads as the
		// full-width segmented block the mockup draws rather than a small chip.
		ContainerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ContainerSlot->SetHorizontalAlignment(HAlign_Fill);
		ContainerSlot->SetVerticalAlignment(VAlign_Center);
	}
}

UButtonWithText* UThemeToggleWidget::MakeSegment(UPanelWidget* Box, const FName WidgetName, const FText& Label)
{
	if (!Box || !WidgetTree)
	{
		return nullptr;
	}

	UButtonWithText* Segment = WidgetTree->ConstructWidget<UButtonWithText>(
		UButtonWithText::StaticClass(), WidgetName);
	if (!Segment)
	{
		return nullptr;
	}

	// Set before the Slate build: RebuildWidget reads ButtonTextValue when it creates the label.
	Segment->ButtonTextValue = Label;
	// THIS widget owns the segment's state colours (active = accent, inactive = transparent), so the
	// base class's flat ButtonBg/ButtonText re-stamp must not run — it would repaint that meaning away
	// on construct AND on every OnThemeChanged, and binding order between parent and child is not
	// something to rely on. This is precisely the case bFollowThemePalette documents.
	Segment->bFollowThemePalette = false;

	if (UHorizontalBoxSlot* SegmentSlot = Cast<UHorizontalBoxSlot>(Box->AddChild(Segment)))
	{
		SegmentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SegmentSlot->SetHorizontalAlignment(HAlign_Fill);
		SegmentSlot->SetVerticalAlignment(VAlign_Fill);
	}

	return Segment;
}

void UThemeToggleWidget::ApplyMobiusTheme_Implementation()
{
	Super::ApplyMobiusTheme_Implementation();

	const UUIThemeSubsystem* ThemeSubsystem = GetThemeSubsystem();
	if (!ThemeSubsystem || !LightSegment || !DarkSegment)
	{
		return;
	}

	// Active state is DERIVED from the theme, never stored, so the fill and the actual theme cannot
	// desync. The startup ticker broadcasts OnThemeChanged up to 100 times; every write below is
	// absolute, so repeats are no-ops.
	const bool bLight = ThemeSubsystem->GetTheme() == EMobiusUITheme::Light;
	StyleSegment(LightSegment, /*bActive*/bLight, /*bLeftEdge*/true);
	StyleSegment(DarkSegment, /*bActive*/!bLight, /*bLeftEdge*/false);

	// The container + seam carry declared roles and self-theme (UMobiusThemedBorder), which is also why
	// StyleBorderForTheme's value remap skips them.
}

void UThemeToggleWidget::StyleSegment(UButtonWithText* Segment, const bool bActive, const bool bLeftEdge) const
{
	if (!Segment)
	{
		return;
	}

	// Outer corners only: the pair reads as one pill inside the container's 3px radius.
	const FVector4 Radii = bLeftEdge
		? FVector4(GSegmentCornerRadius, 0.0, 0.0, GSegmentCornerRadius)
		: FVector4(0.0, GSegmentCornerRadius, GSegmentCornerRadius, 0.0);

	const FLinearColor Accent = GetThemeColor(EMobiusPaletteRole::Accent);
	// Inactive is TRANSPARENT rather than a fill of its own: the container's InputBg is the surface the
	// mockup shows behind an unselected segment, and one surface cannot double-paint itself.
	const FLinearColor Fill = bActive ? Accent : FLinearColor::Transparent;
	const FLinearColor Hover = bActive ? Accent : GetThemeColor(EMobiusPaletteRole::ButtonHoverBg);
	const FLinearColor Press = bActive ? Accent : GetThemeColor(EMobiusPaletteRole::ButtonPressedBg);
	// White on the accent (the mockup's active label) vs the muted inactive tab text.
	const FSlateColor LabelColour = FSlateColor(bActive
		? FLinearColor::White
		: GetThemeColor(EMobiusPaletteRole::TabInactiveText));

	FButtonStyle Style = Segment->GetStyle();
	Style.SetNormal(MakeSegmentBrush(Fill, Radii));
	Style.SetHovered(MakeSegmentBrush(Hover, Radii));
	Style.SetPressed(MakeSegmentBrush(Press, Radii));
	Style.SetDisabled(MakeSegmentBrush(Fill, Radii));
	Style.NormalForeground = LabelColour;
	Style.HoveredForeground = LabelColour;
	Style.PressedForeground = LabelColour;
	Style.DisabledForeground = LabelColour;
	// EQUAL by construction: an asymmetric pressed padding shrinks the hit rect mid-press and the click
	// is silently dropped (UBaseButton::StabilisePressedPadding documents the Slate mechanism).
	Style.NormalPadding = GSegmentPadding;
	Style.PressedPadding = GSegmentPadding;
	Segment->SetStyle(Style);

	// Weight is the mockup's other active signal (600 vs 400). Size/face stay on the Mobius ramp rather
	// than being a literal here; only the typeface entry changes.
	if (Segment->MyButtonText.IsValid())
	{
		FSlateFontInfo Font = FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Label").Font;
		Font.TypefaceFontName = bActive ? FName("SemiBold") : FName("Regular");
		Segment->MyButtonText->SetFont(Font);
	}
	// Explicit rather than relying on the label's UseForeground: RefreshThemedLabelStyle no longer runs
	// for a button with bFollowThemePalette cleared, so this is the only label-colour writer here.
	Segment->ApplyThemedLabelColor(LabelColour.GetSpecifiedColor());
}

void UThemeToggleWidget::HandleLightSegmentClicked()
{
	if (UUIThemeSubsystem* ThemeSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UUIThemeSubsystem>() : nullptr)
	{
		ThemeSubsystem->SetTheme(EMobiusUITheme::Light);
	}
}

void UThemeToggleWidget::HandleDarkSegmentClicked()
{
	if (UUIThemeSubsystem* ThemeSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UUIThemeSubsystem>() : nullptr)
	{
		ThemeSubsystem->SetTheme(EMobiusUITheme::Dark);
	}
}
