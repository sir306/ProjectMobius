// Fill out your copyright notice in the Description page of Project Settings.


#include "ErrorWidget.h"

void UErrorWidget::NativePreConstruct()
{
	// Set Default Texts
	ErrorTitleTextBlock->SetText(FText::FromString("Error Title"));
	ErrorMessageTextBlock->SetText(FText::FromString("Error Message"));
	Super::NativePreConstruct();
}

void UErrorWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// Set Default Texts
	ErrorTitleTextBlock->SetText(FText::FromString("Error Title"));
	ErrorMessageTextBlock->SetText(FText::FromString("Error Message"));
	this->SetVisibility(ESlateVisibility::Hidden);
}

void UErrorWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}
