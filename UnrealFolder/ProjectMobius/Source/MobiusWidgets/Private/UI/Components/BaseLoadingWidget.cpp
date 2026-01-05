// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Components/BaseLoadingWidget.h"

#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

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
