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

#include "UI/Components/ButtonWithText.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Style/MobiusStyle.h"
#include "UI/Theme/UIThemeSubsystem.h"
#include "Widgets/SWidget.h"
#include "Slate.h"
#include "Components/ButtonSlot.h"

UButtonWithText::UButtonWithText()
{

}

void UButtonWithText::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	ApplyMobiusButtonStyle();

	// bind the button clicked event to the update style method
	//OnClicked.AddDynamic(this, &UButtonWithText::ButtonClickedUpdateStyle);
}

void UButtonWithText::ApplyMobiusButtonStyle()
{
	// Ribbon tabs self-theme from the subsystem (themed tab style + explicit label colour) instead of the
	// SWS snapshot — the snapshot is what fought the BP tab-swap and left the tab text invisible.
	if (bIsRibbonButton)
	{
		RefreshRibbonAppearance();
		// Ribbon tabs skip the SWS snapshot but NOT the hit-rect fix: GetThemedTabStyle happens to author
		// equal Normal/Pressed padding today, and this keeps that from silently regressing into dropped clicks.
		StabilisePressedPadding();
		return;
	}
	Super::ApplyMobiusButtonStyle();
}

TSharedRef<SWidget> UButtonWithText::RebuildWidget()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Custom Button Text. Editor-assigned SWS_* style assets take precedence; the shared Mobius
	// style set is the fallback so unstyled buttons still match the app theme (was FCoreStyle).
	MyButtonText =
		SNew(STextBlock)
		.Text(ButtonTextValue)
		.TextStyle(MobiusButtonTextStyle ? MobiusButtonTextStyle->GetStyle<FTextBlockStyle>()
			           : &FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Label"))
		// Label FOLLOWS the button style foreground (themed by GetThemedTabStyle / ApplySharedStyles)
		// rather than the SWS text style's baked colour. FIX (2026-07-21): ribbon-tab labels rendered
		// WHITE/invisible because the walk's per-widget ApplyThemedLabelColor wasn't landing on them
		// (stale MyButtonText) — yet the button foreground was correctly themed. UseForeground makes the
		// label track that foreground. Where the walk DOES set a specified colour it still overrides;
		// where it doesn't, this gives the correct themed colour instead of construct-time white.
		.ColorAndOpacity(FSlateColor::UseForeground())
		.TextShapingMethod(ETextShapingMethod::FullShaping);

	MyButton = SNew(SButton)
		// Construction-time style only: ApplyMobiusButtonStyle re-shapes this from FMobiusButtonGeometry and
		// recolours it from the palette on the same SynchronizeProperties pass. The ButtonStyleDefault
		// override that used to sit here was never set on any widget and had no writer left.
		.ButtonStyle(&FMobiusStyle::Get().GetWidgetStyle<FButtonStyle>("Mobius.Button"))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ContentPadding(FMobiusStyle::Get().GetMargin("Mobius.Padding.Button"))
		[
			MyButtonText.ToSharedRef()
		]
		.OnClicked(BIND_UOBJECT_DELEGATE(FOnClicked, SlateHandleClicked))
		.OnPressed(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandlePressed))
		.OnReleased(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandleReleased))
		.OnHovered_UObject(this, &ThisClass::SlateHandleHovered)
		.OnUnhovered_UObject(this, &ThisClass::SlateHandleUnhovered)
		.ClickMethod(ClickMethod)
		.TouchMethod(TouchMethod)
		.PressMethod(PressMethod)
		.IsFocusable(IsFocusable);
	;

	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if ( GetChildrenCount() > 0 )
	{
		Cast<UButtonSlot>(GetContentSlot())->BuildSlot(MyButton.ToSharedRef());
	}

	// W2 (2026-07-21): the per-click full ReapplyTheme (HandleThemeRefreshAfterClick) is REMOVED. It
	// scheduled a whole-UI walk on EVERY button click, turning OnThemeChanged into a per-click firehose
	// and re-churning combos (the crash). It existed to re-theme the ribbon tab-swap's baked DarkTheme
	// brushes; the ribbon BP is now rewired to GetThemedTabStyle (produces a themed style directly), and
	// no other click re-bakes a theme brush (gating checks 2026-07-21), so it is no longer load-bearing.

	// Ribbon tabs: apply the themed look now that MyButtonText exists. The OnThemeChanged subscription is
	// UBaseButton's (bound in OnWidgetRebuilt, which runs immediately after this) — every Mobius button now
	// follows the theme by event, so there is no ribbon-specific bind here any more.
	if (bIsRibbonButton)
	{
		RefreshRibbonAppearance();
	}
	else
	{
		// A5: land the themed label colour now that MyButtonText exists, so the label does not depend on
		// ApplySharedStyles having retinted the shared SWS text asset first.
		RefreshThemedLabelStyle();
	}

	return MyButton.ToSharedRef();
}

void UButtonWithText::SetButtonWithNewText(FText NewButtonText)
{
	ButtonTextValue = NewButtonText;

	if (MyButtonText.IsValid())
	{
		MyButtonText->SetText(NewButtonText);
	}
}

void UButtonWithText::RefreshTextStyle()
{
	if (MyButtonText.IsValid())
	{
		MyButtonText->SetTextStyle(MobiusButtonTextStyle ? MobiusButtonTextStyle->GetStyle<FTextBlockStyle>()
			                           : &FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Label"));
	}
}

void UButtonWithText::RefreshThemedLabelStyle()
{
	// Ribbon tabs: ApplyRibbonTabStyle already re-pushes the style AND paints the active/inactive accent.
	//
	// A20 (2026-08-03): ShouldFollowThemePalette() now gates the LABEL too, not just the button brushes.
	// bFollowThemePalette means "this button's owner drives its colours, so the palette re-stamp must not
	// repaint that meaning away" (see UBaseButton.h) — and that was only half true: RefreshThemedButtonStyle
	// honoured it while this function did not, so an owner-driven label was still overwritten with
	// ButtonText on construct and on every OnThemeChanged. RefreshTextStyle() inside it also re-pushed the
	// asset's text style, which resets the FONT as well as the colour. Measured before widening the gate:
	// ZERO of the 71 /Game Widget Blueprints author bFollowThemePalette (byte-scanned every .uasset for the
	// property name; UE only serialises a non-default value), so no existing button changes behaviour — the
	// first consumer is the A20 theme segments, whose active label is white SemiBold.
	if (bIsRibbonButton || !ShouldFollowThemePalette() || !MyButtonText.IsValid())
	{
		return;
	}

	// Font face/size/typeface stay asset-owned (owner ruling: geometry from the asset, colour from C++).
	RefreshTextStyle();

	const UUIThemeSubsystem* Theme = ResolveThemeSubsystem();
	if (!Theme)
	{
		return; // design time / no game instance — the UseForeground fallback from RebuildWidget stands
	}

	// A destructive action's label is a SIGNAL, not chrome, so it paints from DangerText — theme-correct in
	// both directions rather than one hard-coded red that only has contrast on a dark button.
	//
	// 2026-08-06: the signal is now DECLARED (bIsDangerLabel) instead of DETECTED. It used to be inferred
	// from MobiusButtonTextStyle's authored colour via a greyscale test — a saturated value meant "signal".
	// That inference made an SWS text asset's colour a control channel, which (a) blocked retiring the
	// asset and (b) degraded silently to chrome if anyone normalised the red to grey. Owner ruling: widgets
	// are themed by the palette subsystem, so intent is declared on the widget.
	//
	// Owner-confirmed 2026-07-28: destructive buttons are red TEXT on the normal button surface, NOT a red
	// fill ("i think it looks better than a background red") — so only the label colour moves here.
	EMobiusPaletteRole LabelRole = bIsToolPanelRow ? EMobiusPaletteRole::LabelText : EMobiusPaletteRole::ButtonText;
	if (bIsDangerLabel)
	{
		LabelRole = EMobiusPaletteRole::DangerText;
	}
	ApplyThemedLabelColor(Theme->GetPaletteColor(LabelRole));
}

void UButtonWithText::ApplyThemedLabelColor(FLinearColor Color)
{
	// Direct Slate set — bypasses the STextBlock construction-time style copy that RefreshTextStyle()
	// cannot re-land (see header + Q49/R4). No-op until the Slate label exists (RebuildWidget).
	if (MyButtonText.IsValid())
	{
		MyButtonText->SetColorAndOpacity(FSlateColor(Color));
	}
}

void UButtonWithText::SetIsActiveTab(const bool bNewActive)
{
	bIsActiveTab = bNewActive;
	RefreshRibbonAppearance();
}

void UButtonWithText::RefreshRibbonAppearance()
{
	if (!bIsRibbonButton)
	{
		return;
	}
	UUIThemeSubsystem* ThemeSubsystem = CachedThemeSubsystem.Get();
	if (!ThemeSubsystem)
	{
		if (const UWorld* World = GetWorld())
		{
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				ThemeSubsystem = GameInstance->GetSubsystem<UUIThemeSubsystem>();
				CachedThemeSubsystem = ThemeSubsystem;
			}
		}
	}
	if (ThemeSubsystem)
	{
		// One authoritative apply: themed tab style (brushes + foreground) AND explicit label colour.
		ThemeSubsystem->ApplyRibbonTabStyle(this, bIsActiveTab);
	}
}

bool UButtonWithText::ShouldFollowThemePalette() const
{
	// A ribbon tab's Normal/Hovered/Pressed brushes are the tab MATERIAL and its foregrounds are the
	// tab-text roles, both supplied by ApplyRibbonTabStyle — a flat palette re-stamp on top would fight it
	// (exactly the loop W2 removed).
	return !bIsRibbonButton && Super::ShouldFollowThemePalette();
}

void UButtonWithText::HandleThemeChanged()
{
	Super::HandleThemeChanged();
	RefreshRibbonAppearance();
	// A5: non-ribbon labels re-pull their own colour here (RefreshRibbonAppearance no-ops for them).
	RefreshThemedLabelStyle();
}

// 2026-08-06: ButtonClickedUpdateStyle + bShouldSwitchNormalWithHovered + ButtonStyleDefault deleted here.
// The function swapped Normal and Hovered brushes on click, from a style asset that had NO writer left, so
// it dereferenced a null ButtonStyleDefault on every button — dormant only because its OnClicked binding
// had been commented out. Nothing referenced any of the three: zero hits across Source/ and zero across
// every .uasset/.umap, so no Blueprint graph could call it either. Ribbon "active tab" appearance, which
// this was written for, is owned by ApplyRibbonTabStyle / SetIsActiveTab.
