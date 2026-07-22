// Fill out your copyright notice in the Description page of Project Settings.


#include "ErrorHandling/ErrorWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Slate/Components/SWindowContentPanel.h"
#include "Style/MobiusStyle.h"
#include "Styling/CoreStyle.h"
#include "UI/Theme/UIThemeSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

namespace
{
	// The error window is Slate chrome (not UMG), so it cannot ride the palette walk. Mirror the
	// SWindowTitleBarWidget idiom: resolve the theme subsystem from any live game world and poll it
	// per-paint via a colour lambda, so the body surface follows a live theme toggle.
	UUIThemeSubsystem* FindThemeSubsystemForErrorWindow()
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

	FLinearColor PollErrorWindowColor(EMobiusPaletteRole Role, const FLinearColor& Fallback)
	{
		if (const UUIThemeSubsystem* Theme = FindThemeSubsystemForErrorWindow())
		{
			return Theme->GetPaletteColor(Role);
		}
		return Fallback;
	}
}


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

        // Theme + typography come from the shared style set. Style pointers passed to the content
        // panel now reference set-owned styles with static lifetime — the previous stack-local
        // FTextBlockStyle instances dangled if the panel kept the pointer past this scope.
        ErrorWindowStyle = FMobiusStyle::Get().GetWidgetStyle<FWindowStyle>("Mobius.Window.Error");
        const FTextBlockStyle& TitleStyle = FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Title");
        const FTextBlockStyle& MessageStyle = FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Body");
        const FTextBlockStyle& LocationStyle = FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Error.Location");

        // Error identity red (theme-independent danger cue) — == the shared "Mobius.Color.Error" token.
        const FLinearColor ErrorRed(0.8f, 0.1f, 0.1f);
        const FMargin WindowPadding = FMobiusStyle::Get().GetMargin("Mobius.Padding.Window");

        // Body surface: a flat WhiteBrush tinted per-theme via a poll lambda (RibbonBg = panel surface) so
        // the dialog follows the theme instead of the fixed engine-dark ToolPanel.GroupBorder. Body TEXT is
        // themed through the shared ramp (retinted per theme in ApplySharedStyles) at open time.
        TSharedRef<SWidget> WindowPanel = SNew(SBorder)
                .Padding(FMargin(0.0f))
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([]()
                {
                        return FSlateColor(PollErrorWindowColor(EMobiusPaletteRole::RibbonBg, FLinearColor(0.03955f, 0.03955f, 0.03955f)));
                })
                [
                        SNew(SVerticalBox)
                        // Red top accent — restores an unmistakable error affordance (the old red title
                        // chrome is dead: SWindowTitleBarWidget forces the title brushes to NoBrush).
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                                SNew(SColorBlock)
                                .Color(ErrorRed)
                                .Size(FVector2D(1.0f, 3.0f))
                        ]
                        + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                        .Padding(WindowPadding)
                        [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .VAlign(VAlign_Fill)
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
                        ]
                ];

        SAssignNew(ErrorWindowPtr, SMoveableWindow)
                .Title(FText::FromString("Error Window"))
                .Style(&ErrorWindowStyle)
                .SupportsMaximize(false)
                .SupportsMinimize(false)
                .IsTopmostWindow(true)
                .SizingRule(ESizingRule::UserSized)
                .ClientSize(FVector2D(520.0f, 300.0f))
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
