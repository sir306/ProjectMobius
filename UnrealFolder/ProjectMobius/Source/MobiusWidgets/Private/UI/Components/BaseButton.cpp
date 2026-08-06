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

#include "UI/Components/BaseButton.h"

#include "Blueprint/UserWidget.h"
#include "Diagnostics/MobiusClickLog.h"
#include "Engine/GameInstance.h"
#include "Style/MobiusButtonGeometry.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "UI/Theme/MobiusThemePalette.h"
#include "UI/Theme/UIThemeSubsystem.h"

namespace
{
	/** Outline wider than the shared 1px chrome line = a meaningful accent ring, not themeable chrome. */
	constexpr float GChromeOutlineWidthMax = 1.5f;
}

void UBaseButton::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	ApplyMobiusButtonStyle();
}

const FMobiusButtonGeometry* UBaseButton::ResolveButtonGeometry() const
{
	switch (GeometryFamily)
	{
	case EMobiusButtonGeometryFamily::Panel:
		return &MobiusButtonGeometry::Chip;
	case EMobiusButtonGeometryFamily::Tab:
		return &MobiusButtonGeometry::Tab;
	case EMobiusButtonGeometryFamily::Shared:
		return nullptr;
	case EMobiusButtonGeometryFamily::FromAsset:
	default:
		break;
	}

	// Transitional: map the bound asset by NAME. Both C++ geometries were measured field-for-field
	// against these two assets before the swap, so this arm is shape-identical to the snapshot it
	// replaced. An unbound button returns null and keeps the shared "Mobius.Button" style — that is what
	// the 12 asset-less UBaseButtons (tool-panel rows, Custom Display link, Reset/Confirm bar) do today,
	// and giving them a shape here would re-lay-out ten live buttons for no requested reason.
	if (!SlateButtonStyle)
	{
		return nullptr;
	}
	const FName AssetName = SlateButtonStyle->GetFName();
	if (AssetName == TEXT("SWS_PanelButtonStyle"))
	{
		return &MobiusButtonGeometry::Chip;
	}
	if (AssetName == TEXT("SWS_SettingButtonStyle"))
	{
		return &MobiusButtonGeometry::Tab;
	}
	return nullptr;
}

void UBaseButton::ApplyMobiusButtonStyle()
{
	// A10b/T3 (2026-08-06): the button's SHAPE comes from C++, not from a snapshot of the bound SWS
	// asset. `SetStyle(*SlateButtonStyle->GetStyle<FButtonStyle>())` used to run here and copied the
	// asset's whole FButtonStyle — geometry, padding AND sound — which is the last thing tying a Mobius
	// button to a style asset now that colour is the palette's. Building the shape here instead means a
	// new widget is correct with no asset bound at all.
	if (const FMobiusButtonGeometry* Geometry = ResolveButtonGeometry())
	{
		FButtonStyle Style = GetStyle();
		Geometry->ApplyToButtonStyle(Style);
		// Carried explicitly: both assets author PressedSlateSound = click_Cue, and the snapshot above was
		// the ONLY route from that cue to a button. Without this the migration would have silently taken
		// the click sound off every Mobius button.
		MobiusButtonSound::ApplyPressedCue(Style);
		SetStyle(Style);
	}

	// Order matters: fix the hit-rect FIRST (unconditional — see StabilisePressedPadding), then recolour.
	// RefreshThemedButtonStyle reads GetStyle(), so it carries the corrected padding forward.
	// Both named geometries derive their pressed padding at equal totals, so this is a no-op for them —
	// kept because it must still catch anything that reaches a button by another route.
	StabilisePressedPadding();

	// The SWS snapshot above (or the shared "Mobius.Button" fallback) supplies the GEOMETRY; the colours
	// come from the palette so a theme switch no longer needs the value-matching walk to find this button.
	RefreshThemedButtonStyle();
}

void UBaseButton::StabilisePressedPadding()
{
	const FButtonStyle& Current = GetStyle();
	if (Current.NormalPadding == Current.PressedPadding)
	{
		return; // already stable: pressing does not resize the hit rect
	}

	FButtonStyle Style = Current;
	const FMargin& Normal = Style.NormalPadding;

	// Same totals as Normal (so GetCombinedPadding returns an identically sized box in both states) with the
	// content nudged 1px down, which reads as "pressed in" without moving a single edge of the hit rect.
	const float Shift = FMath::Min(1.0f, Normal.Bottom);
	Style.PressedPadding = FMargin(Normal.Left, Normal.Top + Shift, Normal.Right, Normal.Bottom - Shift);

	SetStyle(Style);
}

bool UBaseButton::ShouldFollowThemePalette() const
{
	return bFollowThemePalette;
}

UUIThemeSubsystem* UBaseButton::ResolveThemeSubsystem()
{
	if (UUIThemeSubsystem* Cached = CachedThemeSubsystem.Get())
	{
		return Cached;
	}
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			UUIThemeSubsystem* Theme = GameInstance->GetSubsystem<UUIThemeSubsystem>();
			CachedThemeSubsystem = Theme;
			return Theme;
		}
	}
	return nullptr;
}

void UBaseButton::RefreshThemedButtonStyle()
{
	if (IsDesignTime() || !ShouldFollowThemePalette())
	{
		return;
	}
	const UUIThemeSubsystem* Theme = ResolveThemeSubsystem();
	if (!Theme)
	{
		return;
	}

	// Tool-panel rows (spec §3.2) sit ON the pane surface: no fill at rest, hover carries the only fill,
	// and the label is panel text rather than button text. Row emphasis belongs to a sibling themed border
	// with a declared role (the floor-stats Total well), not to the button.
	const FLinearColor Fill   = bIsToolPanelRow
		? FLinearColor::Transparent
		: Theme->GetPaletteColor(EMobiusPaletteRole::ButtonBg);
	const FLinearColor Hover  = Theme->GetPaletteColor(EMobiusPaletteRole::ButtonHoverBg);
	const FLinearColor Press  = Theme->GetPaletteColor(EMobiusPaletteRole::ButtonPressedBg);
	const FLinearColor Border = Theme->GetPaletteColor(EMobiusPaletteRole::ButtonBorder);
	const FSlateColor  Label  = FSlateColor(Theme->GetPaletteColor(
		bIsToolPanelRow ? EMobiusPaletteRole::LabelText : EMobiusPaletteRole::ButtonText));

	// BackgroundColor is a MULTIPLIER, not a fill: it reaches Slate as SButton's BorderBackgroundColor and
	// scales every brush tint written below. A non-neutral authored value therefore silently re-tints the
	// palette colour this function just resolved — ButtonHoverBg through a dark multiplier reads as black,
	// and only on hover, because a row's Normal is transparent so the same factor is invisible at rest.
	// Same discipline the themed UBorders follow: colour in the brush, multiplier left white.
	//
	// Deliberately BEFORE the accent-ring early-out: neutralising a multiplier is right whether or not this
	// button's outline opts it out of recolouring. Guarded by the inequality so a second pass writes nothing.
	//
	// A6b-6a (2026-07-31): EVERY themed button, not just tool-panel rows. This is the re-homing of the last
	// write the legacy value-walk uniquely owned (UIThemeSubsystem.cpp's UBaseButton branch remapped
	// BackgroundColor through SurfaceMap; StyleButtonForTheme early-outs on UBaseButton before its own copy,
	// so nothing else reached it). White rather than a remap because owner-pull puts the fill in the brush
	// TINT above — a neutral multiplier is what makes that tint the colour Slate actually paints — and
	// because SurfaceMap's remap carries the epsilon collision that fails a light->dark->light round trip.
	//
	// Measured across the whole project before widening it (71 WBPs = every /Game Widget Blueprint; the other
	// mounted roots hold only engine/plugin stock, which cannot contain a UBaseButton): 98 UBaseButton
	// archetypes, of which exactly TWO author a non-white BackgroundColor — WBP_NumberOfAgents.CurrentFloorBtn
	// and WBP_HeaderText.BaseButton_146, both tool-panel rows, both (0.006995, 0.007499, 0.008568). No
	// Blueprint or C++ writes the property at runtime (a content-wide scan for the member name and for
	// SetBackgroundColor finds no setter). So dropping the row condition is behaviour-identical today: 96 of
	// 98 buttons already hold White, and Remap() guards pure white anyway, which is why the walk's copy could
	// be deleted without changing a pixel.
	//
	// Two families do NOT reach here, by the same gate that excludes them from every other colour write:
	// ribbon tabs (UButtonWithText::bIsRibbonButton -> ApplyRibbonTabStyle owns their brushes) and buttons
	// with bFollowThemePalette cleared ("colours exactly as authored"). Both author White today, so they lost
	// nothing when the walk's copy went — but they now have NO BackgroundColor writer at all. Author a
	// non-white multiplier on one of those and it will silently re-tint the theme colour with no fallback.
	//
	// RGB ONLY, alpha carried through — deliberately the same semantics as the walk's guard (Remap's
	// bGuardNeutralWhite tests R,G,B > 0.99 and never looked at A). An exact compare against
	// FLinearColor::White would drag an authored translucent multiplier (1,1,1,0.5) up to opaque, which the
	// walk never did: that is a fade the owner asked for, not a surface colour.
	const FLinearColor Multiplier = GetBackgroundColor();
	if (Multiplier.R < 0.99f || Multiplier.G < 0.99f || Multiplier.B < 0.99f)
	{
		SetBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, Multiplier.A));
	}

	FButtonStyle Style = GetStyle();

	// A wide outline carries state MEANING (the scalability "current tier" chip is a 2px accent ring), so
	// leave the whole style alone rather than repainting the signal away.
	if (Style.Normal.OutlineSettings.Width > GChromeOutlineWidthMax)
	{
		return;
	}

	// A tool-panel row is flat: the hairline belongs to the well border beside it, so the row's own outline
	// is painted away. WIDTH is still asset-owned (colours = C++, geometry = asset) — only the colour goes.
	const FLinearColor OutlineColour = bIsToolPanelRow ? FLinearColor::Transparent : Border;

	auto Recolour = [&OutlineColour](FSlateBrush& Brush, const FLinearColor& Tint)
	{
		// Image/material brushes: the asset owns the art (playbar play/pause MIDs, VR button MIs), and the
		// C++ SetPlayButtonStyle swap path re-supplies them. Tinting them would double-multiply the material.
		if (Brush.GetResourceObject() != nullptr)
		{
			return;
		}
		Brush.TintColor = FSlateColor(Tint);
		// Outline WIDTH stays asset-owned (the owner's split: colours = C++, geometry = asset), so only
		// brushes that already draw a line get a themed colour.
		if (Brush.OutlineSettings.Width > 0.0f)
		{
			Brush.OutlineSettings.Color = FSlateColor(OutlineColour);
		}
	};

	Recolour(Style.Normal,   Fill);
	Recolour(Style.Hovered,  Hover);
	Recolour(Style.Pressed,  Press);
	Recolour(Style.Disabled, Fill);

	Style.NormalForeground   = Label;
	Style.HoveredForeground  = Label;
	Style.PressedForeground  = Label;
	Style.DisabledForeground = Label;

	SetStyle(Style);
}

void UBaseButton::HandleThemeChanged()
{
	RefreshThemedButtonStyle();
}

void UBaseButton::BeginDestroy()
{
	if (UUIThemeSubsystem* Theme = CachedThemeSubsystem.Get())
	{
		Theme->OnThemeChanged.RemoveDynamic(this, &UBaseButton::HandleThemeChanged);
	}
	Super::BeginDestroy();
}

void UBaseButton::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();

	if (IsDesignTime())
	{
		return;
	}

	// AddUnique: OnWidgetRebuilt runs again on every rebuild, and these UPROPERTY delegates survive it.
	OnPressed.AddUniqueDynamic(this, &UBaseButton::HandleClickLogPressed);
	OnReleased.AddUniqueDynamic(this, &UBaseButton::HandleClickLogReleased);
	OnClicked.AddUniqueDynamic(this, &UBaseButton::HandleClickLogClicked);

	// Event-driven theming (A4): the button re-pulls its own state colours on a theme switch instead of
	// waiting for the whole-UI value-matching walk to recognise it by colour. AddUnique because a rebuild
	// re-runs this and the subsystem outlives the widget.
	if (UUIThemeSubsystem* Theme = ResolveThemeSubsystem())
	{
		Theme->OnThemeChanged.AddUniqueDynamic(this, &UBaseButton::HandleThemeChanged);
	}
	RefreshThemedButtonStyle();

	MobiusClickLog::Log(TEXT("BUTTON"), FString::Printf(TEXT("REBUILD   %s"), *GetClickLogLabel()));
}

void UBaseButton::HandleClickLogPressed()
{
	MobiusClickLog::Log(TEXT("BUTTON"), FString::Printf(TEXT("PRESSED   %s"), *GetClickLogLabel()));
}

void UBaseButton::HandleClickLogReleased()
{
	MobiusClickLog::Log(TEXT("BUTTON"), FString::Printf(TEXT("RELEASED  %s"), *GetClickLogLabel()));
}

void UBaseButton::HandleClickLogClicked()
{
	MobiusClickLog::Log(TEXT("BUTTON"), FString::Printf(TEXT("CLICKED   %s"), *GetClickLogLabel()));
}

FString UBaseButton::GetClickLogLabel() const
{
	const UUserWidget* OwningWidget = GetTypedOuter<UUserWidget>();
	return FString::Printf(TEXT("%s [%s] in %s"), *GetName(), *GetClass()->GetName(),
		OwningWidget ? *OwningWidget->GetClass()->GetName() : TEXT("<no owning widget>"));
}
