// Fill out your copyright notice in the Description page of Project Settings.


#include "ErrorHandling/ErrorWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Slate/Components/SWindowContentPanel.h"
#include "Slate/Components/MobiusWindowButtonStyle.h"
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
#include "Widgets/Text/STextBlock.h"

namespace
{
	// The error window is Slate chrome (not UMG), so it cannot ride the palette walk. Mirror the
	// SWindowTitleBarWidget idiom: resolve the theme subsystem from any live game world and poll it
	// per-paint via a colour lambda, so the body surface follows a live theme toggle.
	UUIThemeSubsystem* FindThemeSubsystemForErrorWindow()
	{
		// Cached for the same reason as SWindowTitleBarWidget's copy: the callers are per-paint colour
		// lambdas. The weak pointer self-clears with the GameInstance, so the walk re-runs only then.
		static TWeakObjectPtr<UUIThemeSubsystem> CachedTheme;
		if (UUIThemeSubsystem* Cached = CachedTheme.Get())
		{
			return Cached;
		}

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
						CachedTheme = Theme;
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

        // Theme + typography come from the shared style set. We copy the ramp styles into members and
        // bump the font size LOCALLY: this native SMoveableWindow gets no UMG UI-scale/DPI multiplier,
        // so the shared 12/10 sizes render tiny here. Members (not stack-locals) because the content
        // panel keeps the style pointers past this scope. Colour is left untouched (the shared ramp is
        // retinted per theme in ApplySharedStyles at open time), so the polled colour is preserved.
        ErrorWindowStyle = FMobiusStyle::Get().GetWidgetStyle<FWindowStyle>("Mobius.Window.Error");
        TitleTextStyle = FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Title");
        MessageTextStyle = FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Body");
        LocationTextStyle = FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Error.Location");
        TitleTextStyle.Font.Size = 16.0f;
        MessageTextStyle.Font.Size = 13.0f;
        LocationTextStyle.Font.Size = 12.0f;

        // A18: title-bar × reads as the destructive affordance it is. Stamped at open (like the rest of
        // this window style), so a theme toggle while the window is up is picked up on the next open.
        MobiusWindowButtonStyle::ApplyDangerCloseGlyph(ErrorWindowStyle, FindThemeSubsystemForErrorWindow());

        // Themed Close button (member: SButton caches raw pointers into the style's brushes).
        CloseButtonStyle = MobiusWindowButtonStyle::MakeWindowButtonStyle(FindThemeSubsystemForErrorWindow());

        const FMargin WindowPadding = FMobiusStyle::Get().GetMargin("Mobius.Padding.Window");

        // Body surface: a flat WhiteBrush tinted per-theme via a poll lambda (RibbonBg = panel surface) so
        // the dialog follows the theme instead of the fixed engine-dark ToolPanel.GroupBorder. Body TEXT is
        // themed through the shared ramp (retinted per theme in ApplySharedStyles) at open time.
        TSharedRef<SWidget> BodyPanel = SNew(SBorder)
                .Padding(FMargin(0.0f))
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([]()
                {
                        return FSlateColor(PollErrorWindowColor(EMobiusPaletteRole::RibbonBg, FLinearColor(0.03955f, 0.03955f, 0.03955f)));
                })
                [
                        SNew(SVerticalBox)
                        // Top accent — the ONLY live emphasis cue (the old red title chrome is dead:
                        // SWindowTitleBarWidget forces title brushes to NoBrush and polls TitlebarText for
                        // the title colour). Driven by severity so a Warning-level "Performance Notice" is
                        // NOT painted error-red: Error/Fatal = red, Warning = amber, Info = accent (blue).
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                                SNew(SColorBlock)
                                .Color_Lambda([Severity = CurrentSeverity]() -> FLinearColor
                                {
                                        switch (*Severity)
                                        {
                                        case EMobiusErrorSeverity::Warning:
                                                return FLinearColor(0.9f, 0.35f, 0.0f); // amber warning cue
                                        case EMobiusErrorSeverity::Info:
                                                return PollErrorWindowColor(EMobiusPaletteRole::Accent, MobiusThemePalette::Color(EMobiusPaletteRole::Accent, /*bLight=*/true));
                                        case EMobiusErrorSeverity::Error:
                                        case EMobiusErrorSeverity::Fatal:
                                        default:
                                                return FLinearColor(0.8f, 0.1f, 0.1f); // error red (== Mobius.Color.Error)
                                        }
                                })
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
                                        .TitleTextStyle(&TitleTextStyle)
                                        .MessageTextStyle(&MessageTextStyle)
                                        .LocationTextStyle(&LocationTextStyle)
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .HAlign(HAlign_Right)
                                .Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))
                                [
                                        SAssignNew(CloseButton, SButton)
                                        .ButtonStyle(&CloseButtonStyle)
                                        .OnClicked(this, &SErrorWindowWidget::HandleCloseClicked)
                                        [
                                                // Explicit label so the text colour follows a live theme toggle
                                                // (same poll idiom as the body). Background brushes are stamped
                                                // for the current theme at construct.
                                                SNew(STextBlock)
                                                .Text(FText::FromString("Close"))
                                                .Justification(ETextJustify::Center)
                                                .ColorAndOpacity_Lambda([]()
                                                {
                                                        return FSlateColor(PollErrorWindowColor(EMobiusPaletteRole::ButtonText, MobiusThemePalette::Color(EMobiusPaletteRole::ButtonText, /*bLight=*/true)));
                                                })
                                        ]
                                ]
                        ]
                ];

        // #11b: the title bar reads the same as the body (TitlebarBg == RibbonBg by palette design). Wrap
        // the body in a 1px WindowBorder-coloured frame so the two surfaces read as separate. WhiteBrush
        // (stable engine pointer, like the body) + FMargin(1) gives the 1px edge; colour polled live.
        TSharedRef<SWidget> WindowPanel = SNew(SBorder)
                .Padding(FMargin(1.0f))
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([]()
                {
                        return FSlateColor(PollErrorWindowColor(EMobiusPaletteRole::WindowBorder, MobiusThemePalette::Color(EMobiusPaletteRole::WindowBorder, /*bLight=*/true)));
                })
                [
                        BodyPanel
                ];

        // Live re-theme for the Close button, event-driven off OnThemeChangedNative. The bind normally
        // succeeds right here, since the window only opens once there is a game instance to report an error
        // from. If it does not, fall back to a 2 Hz bootstrap timer that stops the moment it binds.
        //
        // The timer has to be registered on CloseButton, not on this controller: this widget is never
        // slotted under any window, so it is never painted, and SWidget::Paint is what pumps active timers.
        // CloseButton lives inside WindowPanel, which is painted.
        if (!TryBindThemeChanged() && CloseButton.IsValid())
        {
                CloseButton->RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SErrorWindowWidget::EnsureThemeBinding));
        }

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
	// Unconditional: the binding outlives the window pointer, and OpenErrorWindow re-binds on reopen.
	UnbindThemeChanged();

	if (!ErrorWindowPtr.IsValid() || !FSlateApplication::IsInitialized())
	{
		ErrorWindowPtr.Reset();
		return;
	}

	FSlateApplication::Get().RequestDestroyWindow(ErrorWindowPtr.ToSharedRef());
	ErrorWindowPtr.Reset();
}

void SErrorWindowWidget::SetSeverity(EMobiusErrorSeverity InSeverity)
{
	*CurrentSeverity = InSeverity;
	// The accent bar reads the shared severity via a bound colour attribute; invalidate paint so a
	// reused window (shown again for a different-severity error without being rebuilt) repaints it.
	Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SErrorWindowWidget::HandleCloseClicked()
{
	CloseErrorWindow();
	return FReply::Handled();
}

void SErrorWindowWidget::ApplyCloseButtonTheme()
{
	// Only rebuild when the theme actually changed (sentinel forces the first apply). The CloseButtonStyle
	// built at construct is frozen at the construct-time theme; a reused window shown in another theme
	// would otherwise keep the wrong button background (the label already polls ButtonText live).
	if (const UUIThemeSubsystem* ThemeSubsystem = FindThemeSubsystemForErrorWindow())
	{
		const EMobiusUITheme CurrentThemeValue = ThemeSubsystem->GetTheme();
		if (CurrentThemeValue != LastAppliedCloseButtonTheme)
		{
			CloseButtonStyle = MobiusWindowButtonStyle::MakeWindowButtonStyle(ThemeSubsystem);
			if (CloseButton.IsValid())
			{
				CloseButton->SetButtonStyle(&CloseButtonStyle);
			}
			LastAppliedCloseButtonTheme = CurrentThemeValue;
		}
	}
}

bool SErrorWindowWidget::TryBindThemeChanged()
{
	if (ThemeChangedHandle.IsValid())
	{
		return true;
	}

	UUIThemeSubsystem* ThemeSubsystem = FindThemeSubsystemForErrorWindow();
	if (!ThemeSubsystem)
	{
		return false;
	}

	BoundThemeSubsystem = ThemeSubsystem;
	ThemeChangedHandle = ThemeSubsystem->OnThemeChangedNative.AddSP(this, &SErrorWindowWidget::ApplyCloseButtonTheme);

	// The event only fires on a later toggle, so stamp the current theme now.
	ApplyCloseButtonTheme();
	return true;
}

EActiveTimerReturnType SErrorWindowWidget::EnsureThemeBinding(double InCurrentTime, float InDeltaTime)
{
	return TryBindThemeChanged() ? EActiveTimerReturnType::Stop : EActiveTimerReturnType::Continue;
}

void SErrorWindowWidget::UnbindThemeChanged()
{
	// Removing by handle needs no AsShared(), so this is safe from the destructor. An SP binding would also
	// expire on its own, but a reopened window re-binds and duplicates would stack up.
	if (UUIThemeSubsystem* ThemeSubsystem = BoundThemeSubsystem.Get())
	{
		ThemeSubsystem->OnThemeChangedNative.Remove(ThemeChangedHandle);
	}
	BoundThemeSubsystem.Reset();
	ThemeChangedHandle.Reset();
}
