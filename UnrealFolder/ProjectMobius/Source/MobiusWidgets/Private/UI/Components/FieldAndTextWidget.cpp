// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Components/FieldAndTextWidget.h"
#include "Slate/Components/SFieldAndTitleText.h"
#include "Style/MobiusStyle.h"
#include "UI/Theme/UIThemeSubsystem.h"
#include "UI/Theme/MobiusThemePalette.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

TSharedRef<SWidget> UFieldAndTextWidget::RebuildWidget()
{
	// Editor-assigned SWS_* style assets take precedence; shared Mobius style set is the fallback
	// so unstyled instances still match the app theme (was FCoreStyle NormalText for both).
	FieldAndTextWidget = SNew(SFieldAndTitleText)
		.FieldText(FieldText)
		.TitleText(TitleText)
		.VerticalStacking(bIsTitleAboveField)
		.AutoCenterTextToWidget(bAutoCenter)
		.TitleTextStyle(TitleTextStyle ? TitleTextStyle->GetStyle<FTextBlockStyle>() : &FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Header"))
		.FieldTextStyle(FieldTextStyle ? FieldTextStyle->GetStyle<FTextBlockStyle>() : &FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Field"));

	// Cold-start correctness: colour the freshly built blocks for the current theme (the walk relands
	// again on every toggle).
	RefreshThemedStyle();

	return FieldAndTextWidget.ToSharedRef();
}

void UFieldAndTextWidget::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();

	if (IsDesignTime())
	{
		return;
	}

	// A5: event-driven theming (mirrors UBaseButton::OnWidgetRebuilt). AddUnique because a rebuild re-runs
	// this and the subsystem outlives the widget.
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UUIThemeSubsystem* Theme = GameInstance->GetSubsystem<UUIThemeSubsystem>())
			{
				CachedThemeSubsystem = Theme;
				Theme->OnThemeChanged.AddUniqueDynamic(this, &UFieldAndTextWidget::HandleThemeChanged);
			}
		}
	}
}

void UFieldAndTextWidget::BeginDestroy()
{
	if (UUIThemeSubsystem* Theme = CachedThemeSubsystem.Get())
	{
		Theme->OnThemeChanged.RemoveDynamic(this, &UFieldAndTextWidget::HandleThemeChanged);
	}
	Super::BeginDestroy();
}

void UFieldAndTextWidget::HandleThemeChanged()
{
	// Theme comes from the subsystem we are BOUND to, which is by definition the one that just changed —
	// no GetWorld() round trip, so an in-world card on a UWidgetComponent gets the same answer as a
	// viewport row. That was the whole reason the walk had to pass bLight in explicitly.
	if (const UUIThemeSubsystem* Theme = CachedThemeSubsystem.Get())
	{
		RefreshThemedStyle(Theme->GetTheme() == EMobiusUITheme::Light);
	}
}

void UFieldAndTextWidget::RefreshThemedStyle()
{
	// No-arg (build/sync cold-start): resolve the current theme via any live game world, then apply.
	bool bLight = true;
	if (const UWorld* World = GetWorld())
	{
		if (const UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (const UUIThemeSubsystem* Theme = GameInstance->GetSubsystem<UUIThemeSubsystem>())
			{
				bLight = Theme->GetTheme() == EMobiusUITheme::Light;
			}
		}
	}
	RefreshThemedStyle(bLight);
}

void UFieldAndTextWidget::RefreshThemedStyle(const bool bLight)
{
	if (!FieldAndTextWidget.IsValid())
	{
		return;
	}
	// Title + field both = LabelText (primary text). SublabelText for the title read as "barely readable"
	// on the in-world flow-counter card over a bright sky (muted #666 grey on a light card); LabelText is
	// near-black in light / light-grey in dark — strong contrast on the card AND on the agent-stat panels.
	const FSlateColor TextColor(MobiusThemePalette::Color(EMobiusPaletteRole::LabelText, bLight));
	FieldAndTextWidget->SetTextColors(TextColor, TextColor);
}

void UFieldAndTextWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (FieldAndTextWidget.IsValid())
	{
		FieldAndTextWidget->SetTitleText(TitleText);
		FieldAndTextWidget->SetFieldText(FieldText);
		RefreshThemedStyle();
		// // If SFieldAndTitleText exposes setters:
		// FieldAndTextWidget->SetTitleText(TitleText);
		// FieldAndTextWidget->SetFieldText(FieldText);
		// FieldAndTextWidget->SetVerticalStacking(bIsTitleAboveField);
  //       
		// // Store the style in a local variable first
		// const FTextBlockStyle& Style = TextStyle 
		// 	? *TextStyle->GetStyle<FTextBlockStyle>()
		// 	: FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
		// FieldAndTextWidget->SetTextStyle(&Style);
	}

}

void UFieldAndTextWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	// Reset the FieldAndTextWidget to release its resources from slate!!
	if (FieldAndTextWidget.IsValid())
	{
		FieldAndTextWidget.Reset();
	}
}

void UFieldAndTextWidget::SetUpdateTitleText(FText InTitleText)
{
	// Always keep the UObject-side value authoritative
	TitleText = MoveTemp(InTitleText);
	
	// Only touch Slate if it exists
	if (FieldAndTextWidget.IsValid())
	{
		FieldAndTextWidget->SetTitleText(TitleText);
	}
}

void UFieldAndTextWidget::SetUpdateFieldText(FText InFieldText)
{
	// Always keep the UObject-side value authoritative
	FieldText = MoveTemp(InFieldText);

	// Only touch Slate if it exists
	if (FieldAndTextWidget.IsValid())
	{
		FieldAndTextWidget->SetFieldText(FieldText);
	}
}

FVector2D UFieldAndTextWidget::GetTextSize() const
{
	// Safe fallback while not constructed yet
	return FieldAndTextWidget.IsValid() ? FieldAndTextWidget->GetTextSize() : FVector2D::ZeroVector;
}

void UFieldAndTextWidget::SetFontSize(float InFontSize) const
{
	if (FieldAndTextWidget.IsValid())
	{
		FieldAndTextWidget->SetFontSize(InFontSize);
	}
}

void UFieldAndTextWidget::SetFieldFontFace(FName InTypeface) const
{
	if (FieldAndTextWidget.IsValid())
	{
		FieldAndTextWidget->SetFieldFontFace(InTypeface);
	}
}

float UFieldAndTextWidget::GetFontSize()
{
	return FieldAndTextWidget.IsValid() ? FieldAndTextWidget->GetFontSize() : 0.f;
}
