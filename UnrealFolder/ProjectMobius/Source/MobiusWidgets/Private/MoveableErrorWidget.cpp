// Fill out your copyright notice in the Description page of Project Settings.


#include "MoveableErrorWidget.h"

#include "MobiusWidgetSubsystem.h"

void UMoveableErrorWidget::NativePreConstruct()
{
	// Set Default Texts
	ErrorTitleTextBlock->SetText(FText::FromString("Error Title"));
	ErrorMessageTextBlock->SetText(FText::FromString("Error Message"));
	
	Super::NativePreConstruct();
}

void UMoveableErrorWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Set Default Texts
	ErrorTitleTextBlock->SetText(FText::FromString("Error Title"));
	ErrorMessageTextBlock->SetText(FText::FromString("Error Message"));
	this->SetVisibility(ESlateVisibility::Hidden);// debug set to hidden when not debugging

	if (UMobiusWidgetSubsystem* MobiusWidgetSubsystem = GetWorld()->GetSubsystem<UMobiusWidgetSubsystem>())
	{
		MobiusWidgetSubsystem->AddErrorWidget(this);
	}
}

void UMoveableErrorWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}
