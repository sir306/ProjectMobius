// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Components/VerticalTextBlock.h"

#include "Slate/Components/SRotatedText.h"
#include "Style/MobiusStyle.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Text/STextBlock.h"

void UVerticalTextBlock::SetText(FText InText)
{
	Text = MoveTemp(InText);
	PushTextToSlate();
}

void UVerticalTextBlock::RefreshThemedStyle()
{
	// Stacked mode only — SRotatedText exposes no style setter (the rails use stacked). The style
	// struct is copied at construct, so the colour ApplySharedStyles retints must be re-pushed here
	// or the label keeps the previous theme until a full rebuild.
	if (StackedText.IsValid())
	{
		StackedText->SetTextStyle(TextStyle
			? TextStyle->GetStyle<FTextBlockStyle>()
			: &FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.RailButton"));
	}
}

void UVerticalTextBlock::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	PushTextToSlate();
}

void UVerticalTextBlock::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	RotatedText.Reset();
	StackedText.Reset();
}

#if WITH_EDITOR
const FText UVerticalTextBlock::GetPaletteCategory()
{
	return NSLOCTEXT("Mobius", "MobiusPaletteCategory", "Mobius");
}
#endif

FText UVerticalTextBlock::BuildStackedText(const FText& InText)
{
	const FString Source = InText.ToString();
	FString Stacked;
	Stacked.Reserve(Source.Len() * 2);
	for (int32 i = 0; i < Source.Len(); ++i)
	{
		if (i > 0)
		{
			Stacked.AppendChar(TEXT('\n'));
		}
		// A space becomes an empty line — a visual gap between stacked words.
		if (Source[i] != TEXT(' '))
		{
			Stacked.AppendChar(Source[i]);
		}
	}
	return FText::FromString(Stacked);
}

void UVerticalTextBlock::PushTextToSlate()
{
	if (RotatedText.IsValid())
	{
		RotatedText->SetText(Text);
	}
	if (StackedText.IsValid())
	{
		StackedText->SetText(BuildStackedText(Text));
	}
}

TSharedRef<SWidget> UVerticalTextBlock::RebuildWidget()
{
	// BW7/D138: rail labels (Floor Stats / Flow Counter) fall back to the dedicated "Mobius.Text.RailButton"
	// (Inter Regular 10) rather than the shared "Mobius.Text.Label" (12, ribbon tabs + Browse) so the rails
	// read at the owner's 10 without shrinking the tabs.
	const FTextBlockStyle* Style = TextStyle
		? TextStyle->GetStyle<FTextBlockStyle>()
		: &FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.RailButton");

	if (Mode == EVerticalTextMode::Rotated)
	{
		StackedText.Reset();
		RotatedText = SNew(SRotatedText)
			.Text(Text)
			.TextStyle(TextStyle ? TextStyle->GetStyle<FTextBlockStyle>() : nullptr)
			.RotationDegrees(RotationDegrees);
		return RotatedText.ToSharedRef();
	}

	RotatedText.Reset();
	StackedText = SNew(STextBlock)
		.Text(BuildStackedText(Text))
		.TextStyle(Style)
		.Justification(ETextJustify::Center)
		.LineHeightPercentage(0.9f); // stacked single glyphs read tighter than prose lines
	return StackedText.ToSharedRef();
}
