// Fill out your copyright notice in the Description page of Project Settings.

#include "Slate/Components/SRotatedText.h"

#include "Style/MobiusStyle.h"
#include "Widgets/Text/STextBlock.h"

void SRotatedText::Construct(const FArguments& InArgs)
{
	// BW7/D138: rail labels fall back to "Mobius.Text.RailButton" (Inter Regular 10), decoupled from the
	// shared "Mobius.Text.Label" (12) so the vertical rail text reads at the owner's 10 without shrinking tabs.
	const FTextBlockStyle* Style = InArgs._TextStyle
		? InArgs._TextStyle
		: &FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.RailButton");

	ChildSlot
	[
		SAssignNew(TextBlock, STextBlock)
		.Text(InArgs._Text)
		.TextStyle(Style)
		.Justification(ETextJustify::Center)
	];

	// Rotation about the child's center: combined with the swapped arrange size below, the rotated
	// text exactly fills this widget's rect. ±90 both work because center rotation is symmetric.
	TextBlock->SetRenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(InArgs._RotationDegrees))));
	TextBlock->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
}

void SRotatedText::SetText(const TAttribute<FText>& InText)
{
	if (TextBlock.IsValid())
	{
		TextBlock->SetText(InText);
	}
}

FVector2D SRotatedText::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	// The layout footprint of rotated text is its natural size with the axes swapped.
	const FVector2D Inner = TextBlock.IsValid() ? TextBlock->GetDesiredSize() : FVector2D::ZeroVector;
	return FVector2D(Inner.Y, Inner.X);
}

void SRotatedText::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	if (!TextBlock.IsValid())
	{
		return;
	}

	// Lay the child out horizontally at the swapped extents, centered; its center-pivot render
	// transform then rotates it into place filling the allotted rect.
	const FVector2D Allotted = AllottedGeometry.GetLocalSize();
	const FVector2D ChildSize(Allotted.Y, Allotted.X);
	const FVector2D Offset((Allotted.X - ChildSize.X) * 0.5f, (Allotted.Y - ChildSize.Y) * 0.5f);

	ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(
		TextBlock.ToSharedRef(), ChildSize, FSlateLayoutTransform(Offset)));
}
