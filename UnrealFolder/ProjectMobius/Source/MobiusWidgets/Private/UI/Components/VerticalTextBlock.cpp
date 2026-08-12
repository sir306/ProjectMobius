// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Components/VerticalTextBlock.h"

#include "Slate/Components/SRotatedText.h"
#include "Style/MobiusStyle.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UI/Components/ButtonWithText.h"   // A5: ribbon-owner check in HandleThemeChanged
#include "UI/Theme/UIThemeSubsystem.h"

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
		const FTextBlockStyle* Style = TextStyle
			? TextStyle->GetStyle<FTextBlockStyle>()
			: &FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.RailButton");
		StackedText->SetTextStyle(Style);
		// FIX (2026-07-22): SetTextStyle re-pushes the style STRUCT but NOT the STextBlock's separately
		// captured ColorAndOpacity attribute (taken from the style at construct), so after a theme toggle
		// the rail label kept the PREVIOUS theme's colour — the identical half-fix that MyButtonText needed
		// (RefreshTextStyle vs ApplyThemedLabelColor). Re-land the colour explicitly. Harmless for a
		// TextStyle-assigned label (it just re-lands its own baked colour).
		StackedText->SetColorAndOpacity(Style->ColorAndOpacity);
	}
}

void UVerticalTextBlock::SetThemedLabelColor(FLinearColor Color)
{
	// Rails are stacked; SRotatedText exposes no colour setter. Direct set on the live label so a ribbon
	// button can paint its active/inactive tab-text colour (the walk's RefreshThemedStyle uses the uniform
	// RailButton colour; this override runs after it — see UUIThemeSubsystem::ApplyRibbonTabStyle).
	if (StackedText.IsValid())
	{
		StackedText->SetColorAndOpacity(FSlateColor(Color));
	}
}

void UVerticalTextBlock::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();

	if (IsDesignTime())
	{
		return;
	}

	// A5: event-driven theming. Mirrors UBaseButton::OnWidgetRebuilt — AddUnique because a rebuild re-runs
	// this and the subsystem outlives the widget.
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UUIThemeSubsystem* Theme = GameInstance->GetSubsystem<UUIThemeSubsystem>())
			{
				CachedThemeSubsystem = Theme;
				Theme->OnThemeChanged.AddUniqueDynamic(this, &UVerticalTextBlock::HandleThemeChanged);
			}
		}
	}
}

void UVerticalTextBlock::BeginDestroy()
{
	if (UUIThemeSubsystem* Theme = CachedThemeSubsystem.Get())
	{
		Theme->OnThemeChanged.RemoveDynamic(this, &UVerticalTextBlock::HandleThemeChanged);
	}
	Super::BeginDestroy();
}

void UVerticalTextBlock::HandleThemeChanged()
{
	// OWNERSHIP RULE, not an optimisation: a side-rail ribbon button paints its child label's colour
	// (TabActiveText vs TabInactiveText) from ApplyRibbonTabStyle on this SAME event. Refreshing here too
	// would make the outcome depend on delegate registration order — and the uniform Mobius.Text.RailButton
	// colour would erase the active-tab accent whenever this handler happened to run last. The button is the
	// single owner for those labels (it calls RefreshThemedStyle itself, then the override); this handler
	// covers stand-alone vertical labels.
	if (const UButtonWithText* OwningButton = Cast<UButtonWithText>(GetParent()))
	{
		if (OwningButton->bIsRibbonButton)
		{
			return;
		}
	}

	RefreshThemedStyle();
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
