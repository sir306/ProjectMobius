// Fill out your copyright notice in the Description page of Project Settings.

#include "Style/MobiusButtonGeometry.h"

#include "Sound/SoundBase.h"
#include "UObject/SoftObjectPath.h"

namespace MobiusButtonGeometry
{
	// Values measured from SWS_PanelButtonStyle on disk (see the header for why the authored
	// PressedPadding 6,2,6,2 is not among them).
	const FMobiusButtonGeometry Chip
	{
		/* CornerRadius      */ 4.0f,
		/* HoverCornerRadius */ -1.0f, // same as CornerRadius: this family is 4px in every state
		/* OutlineWidth      */ 1.0f,
		/* NormalPadding     */ FMargin(8.0f, 4.0f)
	};

	// Values measured from SWS_SettingButtonStyle on disk 2026-08-06. Square at rest, rounded on hover,
	// and no outline at all — writing OutlineWidth 1 here would give this family a ring it never had.
	const FMobiusButtonGeometry Tab
	{
		/* CornerRadius      */ 0.0f,
		/* HoverCornerRadius */ 4.0f,
		/* OutlineWidth      */ 0.0f,
		/* NormalPadding     */ FMargin(0.0f, 20.0f)
	};
}

FMargin FMobiusButtonGeometry::GetStablePressedPadding() const
{
	// Same arithmetic as UBaseButton::StabilisePressedPadding, so a style authored from this struct is
	// already at its fixed point and that repair leaves it alone. Clamped by Bottom so a zero-padding
	// geometry cannot go negative.
	const float Shift = FMath::Min(1.0f, NormalPadding.Bottom);
	return FMargin(NormalPadding.Left, NormalPadding.Top + Shift,
		NormalPadding.Right, NormalPadding.Bottom - Shift);
}

void FMobiusButtonGeometry::ApplyToBrush(FSlateBrush& Brush, const float Radius) const
{
	Brush.SetResourceObject(nullptr);
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
	Brush.OutlineSettings.Width = OutlineWidth;
	// TintColor and OutlineSettings.Color are deliberately NOT touched — they are the palette's.
}

void FMobiusButtonGeometry::ApplyToButtonStyle(FButtonStyle& Style) const
{
	const float HoverRadius = HoverCornerRadius >= 0.0f ? HoverCornerRadius : CornerRadius;

	ApplyToBrush(Style.Normal, CornerRadius);
	ApplyToBrush(Style.Hovered, HoverRadius);
	ApplyToBrush(Style.Pressed, HoverRadius);

	// Disabled paints nothing in both SWS assets. Preserve that rather than handing it the shape — see
	// the header: a disabled button that suddenly draws a fill only shows in the disabled state.
	Style.Disabled.SetResourceObject(nullptr);
	Style.Disabled.DrawAs = ESlateBrushDrawType::NoDrawType;

	Style.NormalPadding = NormalPadding;
	Style.PressedPadding = GetStablePressedPadding();
}

namespace MobiusButtonSound
{
	const TCHAR* const PressedCuePath = TEXT("/Game/01_Dev/Widgets/Audio/click_Cue.click_Cue");

	void ApplyPressedCue(FButtonStyle& Style)
	{
		// Resolved once and cached. A failed load is left silent rather than asserted: a missing cue
		// should cost the click sound, not the button.
		static TWeakObjectPtr<USoundBase> CachedCue;
		static bool bResolved = false;
		if (!bResolved || !CachedCue.IsValid())
		{
			CachedCue = Cast<USoundBase>(FSoftObjectPath(PressedCuePath).TryLoad());
			bResolved = true;
		}

		if (USoundBase* Cue = CachedCue.Get())
		{
			Style.PressedSlateSound.SetResourceObject(Cue);
		}
		// HoveredSlateSound is left untouched — both assets author it empty, and giving buttons a hover
		// sound they never had would be an unrequested behaviour change, not a migration.
	}
}
