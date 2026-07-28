// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Components/MobiusThemedBorder.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UI/Theme/UIThemeSubsystem.h"

void UMobiusThemedBorder::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	// Designer + construct path: land the role colours so the first paint is already themed. At design
	// time ResolveThemeSubsystem returns null and this no-ops, leaving the authored colours visible in UMG.
	RefreshThemedBorder();
}

void UMobiusThemedBorder::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();

	// Event-driven theming, same shape as UBaseButton / UVerticalTextBlock. AddUnique because a rebuild
	// re-runs this on a widget that may already be bound.
	if (UUIThemeSubsystem* Theme = ResolveThemeSubsystem())
	{
		Theme->OnThemeChanged.AddUniqueDynamic(this, &UMobiusThemedBorder::HandleThemeChanged);
	}
	RefreshThemedBorder();
}

void UMobiusThemedBorder::BeginDestroy()
{
	if (UUIThemeSubsystem* Theme = CachedThemeSubsystem.Get())
	{
		Theme->OnThemeChanged.RemoveDynamic(this, &UMobiusThemedBorder::HandleThemeChanged);
	}
	Super::BeginDestroy();
}

void UMobiusThemedBorder::HandleThemeChanged()
{
	RefreshThemedBorder();
}

UUIThemeSubsystem* UMobiusThemedBorder::ResolveThemeSubsystem()
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

void UMobiusThemedBorder::RefreshThemedBorder()
{
	// DESIGN-TIME GUARD — load-bearing, not defensive. SynchronizeProperties and OnWidgetRebuilt both run
	// on the UMG designer's preview widget, and RefreshThemedBorder WRITES the BrushColor UPROPERTY. In an
	// editor context that would silently overwrite the authored colour on the template and dirty the WBP —
	// and the authored colour is both the design-time source of truth and the cross-check the A6b role map
	// was built from. Never theme in the designer; the authored colour is what the designer should show.
	if (IsDesignTime())
	{
		return;
	}

	const UUIThemeSubsystem* Theme = ResolveThemeSubsystem();
	if (!Theme)
	{
		return; // no game instance — authored colours stand
	}

	if (bThemeFill)
	{
		// D169 single-multiplier rule, now enforced by construction instead of detected by the walker:
		// SBorder paints Background.TintColor * BrushColor (SBorder::OnPaint), so a Border carrying a
		// non-white value in BOTH fields multiplies them and renders far too dark (~0.63x in light,
		// near-black in dark — the 2026-07-17 RailBg / RailRightBorder bug). The role goes on BrushColor
		// and the tint is pinned white. Guarded to flat fills: a texture/material brush needs its white
		// tint to show the art unmodulated, and its colour is the material's business, not ours.
		SetBrushColor(Theme->GetPaletteColor(FillRole));

		FSlateBrush Brush = Background;
		if (Brush.GetResourceObject() == nullptr
			&& !Brush.TintColor.GetSpecifiedColor().Equals(FLinearColor::White, 0.02f))
		{
			Brush.TintColor = FSlateColor(FLinearColor::White);
			SetBrush(Brush);
		}
	}

	if (bThemeOutline)
	{
		// WIDTH and radii stay asset-owned — only the colour is ours. Skipped entirely when the authored
		// brush draws no outline, so ticking this on a width-0 Border is harmless rather than a surprise.
		FSlateBrush Brush = Background;
		if (Brush.OutlineSettings.Width > 0.0f)
		{
			const FLinearColor OutlineColor = Theme->GetPaletteColor(OutlineRole);
			if (!Brush.OutlineSettings.Color.GetSpecifiedColor().Equals(OutlineColor, 0.002f))
			{
				Brush.OutlineSettings.Color = FSlateColor(OutlineColor);
				SetBrush(Brush);
			}
		}
	}
}
