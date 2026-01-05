// Fill out your copyright notice in the Description page of Project Settings.


#include "Widget/ErrorWindowWidget.h"
#include "Slate/ErrorWindow.h"

TSharedRef<SWidget> UErrorWindowWidget::RebuildWidget()
{
	ErrorWindowWidget = SNew(SErrorWindowWidget);
	
	return ErrorWindowWidget.ToSharedRef();
}

void UErrorWindowWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	ErrorWindowWidget.Reset();
}

void UErrorWindowWidget::SetTitleBarText(const FText& TitleText)
{
	if (ErrorWindowWidget.IsValid())
	{
		ErrorWindowWidget->SetTitleBarText(TitleText);
	}
}

void UErrorWindowWidget::SetErrorTitleText(const FText& TitleText)
{
	if (ErrorWindowWidget.IsValid())
	{
		ErrorWindowWidget->SetErrorTitleText(TitleText);
	}
}

void UErrorWindowWidget::SetErrorMessageText(const FText& MessageText)
{
	if (ErrorWindowWidget.IsValid())
	{
		ErrorWindowWidget->SetErrorMessageText(MessageText);
	}
}

void UErrorWindowWidget::SetErrorLocationText(const FText& LocationText)
{
	if (ErrorWindowWidget.IsValid())
	{
		ErrorWindowWidget->SetErrorLocationText(LocationText);
	}
}

void UErrorWindowWidget::ShowErrorWindow()
{
	if (!ErrorWindowWidget.IsValid())
	{
		TakeWidget();
	}

	if (ErrorWindowWidget.IsValid())
	{
		ErrorWindowWidget->ShowErrorWindow();
	}
}
