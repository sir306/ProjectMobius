// Fill out your copyright notice in the Description page of Project Settings.

#include "Style/MobiusButtonGeometry.h"

namespace MobiusButtonGeometry
{
	// Values measured from SWS_PanelButtonStyle on disk (see the header for why the authored
	// PressedPadding 6,2,6,2 is not among them).
	const FMobiusButtonGeometry Chip
	{
		/* CornerRadius */ 4.0f,
		/* OutlineWidth */ 1.0f,
		/* NormalPadding */ FMargin(8.0f, 4.0f)
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

void FMobiusButtonGeometry::ApplyToBrush(FSlateBrush& Brush) const
{
	Brush.SetResourceObject(nullptr);
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.OutlineSettings.CornerRadii = FVector4(CornerRadius, CornerRadius, CornerRadius, CornerRadius);
	Brush.OutlineSettings.Width = OutlineWidth;
	// TintColor and OutlineSettings.Color are deliberately NOT touched — they are the palette's.
}

void FMobiusButtonGeometry::ApplyToButtonStyle(FButtonStyle& Style) const
{
	FSlateBrush* Brushes[] = { &Style.Normal, &Style.Hovered, &Style.Pressed, &Style.Disabled };
	for (FSlateBrush* Brush : Brushes)
	{
		ApplyToBrush(*Brush);
	}

	Style.NormalPadding = NormalPadding;
	Style.PressedPadding = GetStablePressedPadding();
}
