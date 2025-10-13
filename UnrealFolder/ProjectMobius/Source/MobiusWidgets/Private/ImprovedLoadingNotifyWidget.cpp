// Fill out your copyright notice in the Description page of Project Settings.


#include "ImprovedLoadingNotifyWidget.h"

#include "Components/BaseLoadingWidget.h"

void UImprovedLoadingNotifyWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
}

void UImprovedLoadingNotifyWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void UImprovedLoadingNotifyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

void UImprovedLoadingNotifyWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}

void UImprovedLoadingNotifyWidget::UpdateLoadPercent(float NewLoadPercent)
{
}

void UImprovedLoadingNotifyWidget::IsLoadingComplete()
{
}

void UImprovedLoadingNotifyWidget::ResetLoadPercent()
{
}

void UImprovedLoadingNotifyWidget::SetLoadingTextAndTitle(FString NewLoadingText, FString NewLoadingTitle)
{
}

void UImprovedLoadingNotifyWidget::UpdateLoadingWidgets()
{
	// calculate the loading bools
	
	SetLoadingWidgetVisibility(LoadingBarWidget, bIsLoadingPedestrianVectors);
	SetLoadingWidgetVisibility(LoadingInfiniteWidget, bIsLoadingGeometry);
}

void UImprovedLoadingNotifyWidget::SetLoadingWidgetVisibility(TObjectPtr<UBaseLoadingWidget> LoadingWidget,
	bool bIsVisible)
{
	if (LoadingWidget)
	{
		if (bIsVisible)
		{
			LoadingWidget->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			LoadingWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}
