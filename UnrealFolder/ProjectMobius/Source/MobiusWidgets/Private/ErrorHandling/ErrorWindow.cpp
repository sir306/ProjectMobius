// Fill out your copyright notice in the Description page of Project Settings.


#include "ErrorHandling/ErrorWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SWindowTitleBar.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"


SErrorWindowWidget::SErrorWindowWidget()
{
}

SErrorWindowWidget::~SErrorWindowWidget()
{
	CloseErrorWindow();
}

void SErrorWindowWidget::Construct(const FArguments& InArgs)
{
	OpenErrorWindow();
}

int32 SErrorWindowWidget::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle,
	                                bParentEnabled);
}

void SErrorWindowWidget::SetTitleBarText(const FText& TitleText)
{
	if (ErrorWindowPtr.IsValid())
	{
		ErrorWindowPtr->SetTitle(TitleText);
	}

	if (TitleBarTextBlock.IsValid())
	{
		TitleBarTextBlock->SetText(TitleText);
	}
}

void SErrorWindowWidget::SetErrorTitleText(const FText& TitleText)
{
	if (ErrorTitleTextBlock.IsValid())
	{
		ErrorTitleTextBlock->SetText(TitleText);
	}
}

void SErrorWindowWidget::SetErrorMessageText(const FText& MessageText)
{
	if (ErrorMessageTextBlock.IsValid())
	{
		ErrorMessageTextBlock->SetText(MessageText);
	}
}

void SErrorWindowWidget::SetErrorLocationText(const FText& LocationText)
{
	const bool bHasLocation = !LocationText.IsEmptyOrWhitespace();

	if (ErrorLocationTextBlock.IsValid())
	{
		ErrorLocationTextBlock->SetText(LocationText);
		ErrorLocationTextBlock->SetVisibility(bHasLocation ? EVisibility::Visible : EVisibility::Collapsed);
	}

	if (ErrorLocationContainer.IsValid())
	{
		ErrorLocationContainer->SetVisibility(bHasLocation ? EVisibility::Visible : EVisibility::Collapsed);
	}
}

void SErrorWindowWidget::ShowErrorWindow()
{
	OpenErrorWindow();

	if (ErrorWindowPtr.IsValid())
	{
		ErrorWindowPtr->BringToFront(true);
	}
}

void SErrorWindowWidget::OpenErrorWindow()
{
	if (ErrorWindowPtr.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	SAssignNew(ErrorWindowPtr, SWindow)
		.Title(FText::FromString("Error Window"))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.IsTopmostWindow(true)
		.SizingRule(ESizingRule::FixedSize)
		.ClientSize(FVector2D(420.0f, 200.0f))
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.HasCloseButton(true);

	ErrorWindowPtr->SetContent(
		SNew(SBorder)
		.Padding(FMargin(16.0f))
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(ErrorTitleTextBlock, STextBlock)
				.Text(FText::FromString("Error Window (Test)"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
			[
				SAssignNew(ErrorMessageTextBlock, STextBlock)
				.Text(FText::FromString("This is a test popup for error handling."))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
			[
				SAssignNew(ErrorLocationContainer, SBox)
				[
					SAssignNew(ErrorLocationTextBlock, STextBlock)
					.Text(FText::GetEmpty())
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.1f, 0.1f)))
					.AutoWrapText(true)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))
			[
				SNew(SButton)
				.Text(FText::FromString("Close"))
				.OnClicked(this, &SErrorWindowWidget::HandleCloseClicked)
			]
		]
	);

	SAssignNew(TitleBarTextBlock, STextBlock)
		.Text(FText::FromString("Error Window"))
		.ColorAndOpacity(FSlateColor(FLinearColor::White))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12));

	ErrorWindowStyle = FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
	ErrorWindowStyle.ActiveTitleBrush.TintColor = FSlateColor(FLinearColor(0.8f, 0.1f, 0.1f));
	ErrorWindowStyle.InactiveTitleBrush.TintColor = FSlateColor(FLinearColor(0.6f, 0.1f, 0.1f));
	ErrorWindowStyle.FlashTitleBrush.TintColor = FSlateColor(FLinearColor(0.9f, 0.2f, 0.2f));
	ErrorWindowStyle.TitleTextStyle.ColorAndOpacity = FSlateColor(FLinearColor::White);

	TSharedRef<SWindowTitleBar> TitleBar = SNew(SWindowTitleBar, ErrorWindowPtr.ToSharedRef(), TitleBarTextBlock, HAlign_Center)
		.Style(&ErrorWindowStyle)
		.ShowAppIcon(false);
	ErrorWindowPtr->SetTitleBar(TitleBar);

	FSlateApplication::Get().AddWindow(ErrorWindowPtr.ToSharedRef());

	SetErrorLocationText(FText::GetEmpty());
}

void SErrorWindowWidget::CloseErrorWindow()
{
	if (!ErrorWindowPtr.IsValid() || !FSlateApplication::IsInitialized())
	{
		ErrorWindowPtr.Reset();
		return;
	}

	FSlateApplication::Get().RequestDestroyWindow(ErrorWindowPtr.ToSharedRef());
	ErrorWindowPtr.Reset();
}

FReply SErrorWindowWidget::HandleCloseClicked()
{
	CloseErrorWindow();
	return FReply::Handled();
}
