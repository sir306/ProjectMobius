// Log viewer window for Mobius custom logger output.

#include "Logging/LogWindow.h"

#include "Framework/Application/SlateApplication.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Styling/CoreStyle.h"
#include "Containers/Array.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

SLogWindowWidget::SLogWindowWidget() = default;

SLogWindowWidget::~SLogWindowWidget()
{
	CloseLogWindow();
}

void SLogWindowWidget::Construct(const FArguments& InArgs)
{
	OpenLogWindow();
}

void SLogWindowWidget::AppendLine(const FString& Line)
{
	if (!bIsEnabled)
	{
		return;
	}

	LogLines.Add(Line);
	if (LogLines.Num() > MaxLines)
	{
		const int32 Overflow = LogLines.Num() - MaxLines;
		LogLines.RemoveAt(0, Overflow, EAllowShrinking::No);
	}

	RebuildLogText();
}

void SLogWindowWidget::SetLines(const TArray<FString>& Lines)
{
	LogLines = Lines;
	if (LogLines.Num() > MaxLines)
	{
		const int32 Overflow = LogLines.Num() - MaxLines;
		LogLines.RemoveAt(0, Overflow, EAllowShrinking::No);
	}

	RebuildLogText();
}

void SLogWindowWidget::ShowLogWindow()
{
	OpenLogWindow();

	if (LogWindowPtr.IsValid())
	{
		LogWindowPtr->BringToFront(true);
	}
}

void SLogWindowWidget::CloseLogWindow()
{
	if (!LogWindowPtr.IsValid() || !FSlateApplication::IsInitialized())
	{
		LogWindowPtr.Reset();
		return;
	}

	FSlateApplication::Get().RequestDestroyWindow(LogWindowPtr.ToSharedRef());
	LogWindowPtr.Reset();
}

void SLogWindowWidget::SetEnabled(bool bEnabled)
{
	bIsEnabled = bEnabled;
	if (!bIsEnabled)
	{
		CloseLogWindow();
	}
}

bool SLogWindowWidget::IsOpen() const
{
	return LogWindowPtr.IsValid();
}

void SLogWindowWidget::OpenLogWindow()
{
	if (!bIsEnabled || LogWindowPtr.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	// Use member variable for style to ensure it persists after this function returns
	LogWindowStyle = FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
	LogWindowStyle.ActiveTitleBrush.TintColor = FSlateColor(FLinearColor(0.12f, 0.12f, 0.12f));
	LogWindowStyle.InactiveTitleBrush.TintColor = FSlateColor(FLinearColor(0.08f, 0.08f, 0.08f));
	LogWindowStyle.TitleTextStyle.ColorAndOpacity = FSlateColor(FLinearColor::White);

	const FTextBlockStyle LogTextStyle = FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 10));

	TSharedRef<SWidget> WindowPanel = SNew(SBorder)
		.Padding(FMargin(12.0f))
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(ScrollBox, SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(LogTextBlock, STextBlock)
					.Text(FText::FromString(LogText))
					.AutoWrapText(true)
					.TextStyle(&LogTextStyle)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
			[
				SNew(SButton)
				.Text(FText::FromString("Close"))
				.OnClicked(this, &SLogWindowWidget::HandleCloseClicked)
			]
		];

	SAssignNew(LogWindowPtr, SMoveableWindow)
		.Title(FText::FromString("Mobius Log"))
		.Style(&LogWindowStyle)
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		.IsTopmostWindow(false)
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(720.0f, 420.0f))
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.HasCloseButton(true)
		.WindowPanelContent(WindowPanel);

	FSlateApplication::Get().AddWindow(LogWindowPtr.ToSharedRef());
}

FReply SLogWindowWidget::HandleCloseClicked()
{
	CloseLogWindow();
	return FReply::Handled();
}

void SLogWindowWidget::RebuildLogText()
{
	LogText.Reset();
	for (int32 Index = 0; Index < LogLines.Num(); ++Index)
	{
		LogText.Append(LogLines[Index]);
		if (Index + 1 < LogLines.Num())
		{
			LogText.Append(TEXT("\n"));
		}
	}

	if (LogTextBlock.IsValid())
	{
		LogTextBlock->SetText(FText::FromString(LogText));
	}

	if (ScrollBox.IsValid())
	{
		ScrollBox->ScrollToEnd();
	}
}
