// Fill out your copyright notice in the Description page of Project Settings.


#include "ErrorHandling/ErrorWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Slate/Components/SWindowContentPanel.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"


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
}

void SErrorWindowWidget::SetErrorTitleText(const FText& TitleText)
{
	if (ContentPanel.IsValid())
	{
		ContentPanel->SetTitleText(TitleText);
	}
}

void SErrorWindowWidget::SetErrorMessageText(const FText& MessageText)
{
	if (ContentPanel.IsValid())
	{
		ContentPanel->SetMessageText(MessageText);
	}
}

void SErrorWindowWidget::SetErrorLocationText(const FText& LocationText)        
{
	if (ContentPanel.IsValid())
	{
		ContentPanel->SetLocationText(LocationText);
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

        ErrorWindowStyle = FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
        ErrorWindowStyle.ActiveTitleBrush.TintColor = FSlateColor(FLinearColor(0.8f, 0.1f, 0.1f));
        ErrorWindowStyle.InactiveTitleBrush.TintColor = FSlateColor(FLinearColor(0.6f, 0.1f, 0.1f));
        ErrorWindowStyle.FlashTitleBrush.TintColor = FSlateColor(FLinearColor(0.9f, 0.2f, 0.2f));
        ErrorWindowStyle.TitleTextStyle.ColorAndOpacity = FSlateColor(FLinearColor::Black);
		ErrorWindowStyle.BackgroundColor = FSlateColor(FLinearColor::Red);


        const FTextBlockStyle TitleStyle = FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
                .SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 18));
        const FTextBlockStyle MessageStyle = FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
                .SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 12));
        const FTextBlockStyle LocationStyle = FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
                .SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                .SetColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.1f, 0.1f)));

        TSharedRef<SWidget> WindowPanel = SNew(SBorder)
                .Padding(FMargin(16.0f))
                .BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
                [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                                SAssignNew(ContentPanel, SWindowContentPanel)
                                .TitleText(FText::FromString("Error Window (Test)"))
                                .MessageText(FText::FromString("This is a test popup for error handling."))
                                .LocationText(FText::GetEmpty())
                                .TitleTextStyle(&TitleStyle)
                                .MessageTextStyle(&MessageStyle)
                                .LocationTextStyle(&LocationStyle)
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
                ];

        SAssignNew(ErrorWindowPtr, SMoveableWindow)
                .Title(FText::FromString("Error Window"))
                .Style(&ErrorWindowStyle)
                .SupportsMaximize(false)
                .SupportsMinimize(false)
                .IsTopmostWindow(true)
                .SizingRule(ESizingRule::FixedSize)
                .ClientSize(FVector2D(420.0f, 200.0f))
                .AutoCenter(EAutoCenter::PreferredWorkArea)
                .HasCloseButton(true)
                .WindowPanelContent(WindowPanel);

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
