// Log viewer window for Mobius custom logger output.

#include "Logging/LogWindow.h"

#include "Framework/Application/SlateApplication.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Styling/CoreStyle.h"
#include "Containers/Array.h"
#include "UI/Theme/UIThemeSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// Slate chrome can't ride the UMG palette walk — poll the theme subsystem per-paint (same idiom as
	// SWindowTitleBarWidget) so the log window body + mono text follow a live theme toggle.
	UUIThemeSubsystem* FindThemeSubsystemForLogWindow()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				if (UGameInstance* GameInstance = World->GetGameInstance())
				{
					if (UUIThemeSubsystem* Theme = GameInstance->GetSubsystem<UUIThemeSubsystem>())
					{
						return Theme;
					}
				}
			}
		}
		return nullptr;
	}

	FLinearColor PollLogWindowColor(EMobiusPaletteRole Role, const FLinearColor& Fallback)
	{
		if (const UUIThemeSubsystem* Theme = FindThemeSubsystemForLogWindow())
		{
			return Theme->GetPaletteColor(Role);
		}
		return Fallback;
	}
}

SLogWindowWidget::SLogWindowWidget() = default;

SLogWindowWidget::~SLogWindowWidget()
{
	CloseLogWindow();
}

void SLogWindowWidget::Construct(const FArguments& InArgs)
{
	OnLogWindowClosed = InArgs._OnLogWindowClosed;
	
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
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([]()
		{
			return FSlateColor(PollLogWindowColor(EMobiusPaletteRole::RibbonBg, FLinearColor(0.03955f, 0.03955f, 0.03955f)));
		})
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
					// Mono log text follows the theme (readable on either surface) via a live poll.
					.ColorAndOpacity_Lambda([]()
					{
						return FSlateColor(PollLogWindowColor(EMobiusPaletteRole::InputMonoText, FLinearColor(0.32314f, 0.32314f, 0.32314f)));
					})
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
		.IsTopmostWindow(true)
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(720.0f, 420.0f))
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.HasCloseButton(true)
		.WindowPanelContent(WindowPanel);

	FSlateApplication::Get().AddWindow(LogWindowPtr.ToSharedRef());

	// Bring the window to front when opened
	if (LogWindowPtr.IsValid())
	{
		LogWindowPtr->BringToFront(true);
	}

	// Bind our custom close event handler, so we can notify others when the log window is closed
	LogWindowPtr.ToSharedRef()->GetOnWindowClosedEvent().AddSP(this, &SLogWindowWidget::HandleLogWindowClosedEvent);
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

void SLogWindowWidget::HandleLogWindowClosedEvent(const TSharedRef<SWindow>& InWindow)
{
	OnLogWindowClosed.ExecuteIfBound();
}
