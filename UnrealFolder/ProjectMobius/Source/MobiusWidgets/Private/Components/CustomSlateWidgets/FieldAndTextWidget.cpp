// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/CustomSlateWidgets/FieldAndTextWidget.h"
#include "Components/CustomSlateComponents/SFieldAndTitleText.h"

TSharedRef<SWidget> UFieldAndTextWidget::RebuildWidget()
{
	FieldAndTextWidget = SNew(SFieldAndTitleText)
		.FieldText(FieldText)
		.TitleText(TitleText)
		.VerticalStacking(bIsTitleAboveField)
		.TitleTextStyle(TitleTextStyle ? TitleTextStyle->GetStyle<FTextBlockStyle>() : &FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		.FieldTextStyle(FieldTextStyle ? FieldTextStyle->GetStyle<FTextBlockStyle>() : &FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"));
	
	return FieldAndTextWidget.ToSharedRef();
}

void UFieldAndTextWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (FieldAndTextWidget.IsValid())
	{
		FieldAndTextWidget->SetTitleText(TitleText);
		FieldAndTextWidget->SetFieldText(FieldText);
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

void UFieldAndTextWidget::SetTitleText(FText InTitleText)
{
	TitleText = InTitleText;
	FieldAndTextWidget->SetTitleText(TitleText);
}

void UFieldAndTextWidget::SetFieldText(FText InFieldText)
{
	FieldText = InFieldText;
	FieldAndTextWidget->SetFieldText(FieldText);
}

FVector2D UFieldAndTextWidget::GetTextSize() const
{
	return FieldAndTextWidget->GetTextSize();
}

void UFieldAndTextWidget::SetFontSize(float InFontSize) const
{
	FieldAndTextWidget->SetFontSize(InFontSize);
}

float UFieldAndTextWidget::GetFontSize()
{
	return FieldAndTextWidget->GetFontSize();
}
