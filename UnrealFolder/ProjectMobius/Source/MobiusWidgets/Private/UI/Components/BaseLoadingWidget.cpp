// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Components/BaseLoadingWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/** M_WidgetBackground's two colour params, shared by MI_LoadingInner/OuterBackground. */
	static const FName GCardFillParam(TEXT("Background Color Tint"));
	static const FName GCardOutlineParam(TEXT("Border Color Tint"));
}

void UBaseLoadingWidget::ThemeMaterialCard(UBorder* Border, const FLinearColor& Fill, const FLinearColor& Outline)
{
	if (!Border)
	{
		return;
	}

	// SBorder renders Background.TintColor x BrushColor x material output, so the brush tint has to stay
	// neutral or the MID params get multiplied twice (see MEMORY reference-umg-border-double-tint).
	Border->SetBrushColor(FLinearColor::White);

	if (UMaterialInstanceDynamic* CardMID = Border->GetDynamicMaterial())
	{
		CardMID->SetVectorParameterValue(GCardFillParam, Fill);
		CardMID->SetVectorParameterValue(GCardOutlineParam, Outline);
	}
}

void UBaseLoadingWidget::ApplyMobiusTheme_Implementation()
{
	// The "Loading Geometry: <name>" line is secondary information (SublabelText); the percent readout is
	// the value, so it keeps the stronger LabelText. LoadingInfiniteImage is a functional spinner graphic
	// (not a background), so it is deliberately left untinted.
	if (LoadingText)
	{
		LoadingText->SetColorAndOpacity(FSlateColor(GetThemeColor(EMobiusPaletteRole::SublabelText)));
	}
	if (LoadedAmount)
	{
		LoadedAmount->SetColorAndOpacity(FSlateColor(GetThemeColor(EMobiusPaletteRole::LabelText)));
	}

	// The bar baked the LIGHT Accent (0, 0.13563, 0.52712) into FillColorAndOpacity in the asset, so it
	// never flipped in dark. The track brush has no per-property setter in 5.5 — copy the style struct,
	// retint, assign it back.
	if (LoadingBar)
	{
		LoadingBar->SetFillColorAndOpacity(GetThemeColor(EMobiusPaletteRole::Accent));

		FProgressBarStyle BarStyle = LoadingBar->GetWidgetStyle();
		BarStyle.BackgroundImage.TintColor = FSlateColor(GetThemeColor(EMobiusPaletteRole::SliderTrack));
		LoadingBar->SetWidgetStyle(BarStyle);
	}

	// The card frame is an unbound designer Border (one per loading WBP, currently "Border_135"), so it is
	// matched by TYPE rather than name — a rename in the asset can't silently drop the theming.
	if (WidgetTree)
	{
		WidgetTree->ForEachWidget([this](UWidget* Widget)
		{
			if (UBorder* Card = Cast<UBorder>(Widget))
			{
				ThemeMaterialCard(Card, GetThemeColor(EMobiusPaletteRole::WellBg),
					GetThemeColor(EMobiusPaletteRole::PanelDivider));
			}
		});
	}
}

void UBaseLoadingWidget::UpdateLoading(float NewLoadPercent)
{
	// clamp value between 0 and 1 -> this is so we don't go over 100% or below 0% as this wouldn't make sense
	LoadPercent = FMath::Clamp(NewLoadPercent, 0.0f, 1.0f);

	// update the loading bool
	UpdateLoading(LoadPercent < 1.0f);

	// update the loading text and update loading bar if it exists
	if (LoadedAmount != nullptr)
	{
		LoadedAmount->SetText(FText::AsPercent(LoadPercent));
	}
	if (LoadingBar != nullptr)
	{
		LoadingBar->SetPercent(LoadPercent);
	}
}

void UBaseLoadingWidget::UpdateLoading(bool bNewLoading)
{
	bIsLoading = bNewLoading;

	// Notify any listeners that the loading state has changed
	OnLoadingStateChanged.Broadcast(bIsLoading);

	// if it is an infinite loading widget then we can show/hide the infinite image
	if (bIsInfiniteLoadingWidget && LoadingInfiniteImage != nullptr)
	{
		// TODO: Future will customize this image to do more effects like change colour
		LoadingInfiniteImage->SetVisibility(bIsLoading ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UBaseLoadingWidget::UpdateLoadingText(FText& NewLoadingText)
{
	if (LoadingText != nullptr && !LoadingText->GetText().IdenticalTo(NewLoadingText))
	{
		LoadingText->SetText(NewLoadingText);
	}
}
