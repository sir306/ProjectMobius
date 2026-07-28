// Log viewer window for Mobius custom logger output.

#include "Logging/LogWindow.h"

#include "Framework/Application/SlateApplication.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Slate/Components/MobiusWindowButtonStyle.h"
#include "Styling/CoreStyle.h"
#include "Containers/Array.h"
#include "UI/Theme/UIThemeSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
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
	// Unconditional: the binding outlives the window pointer, and OpenLogWindow re-binds on reopen.
	UnbindThemeChanged();

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

	// Use member variable for style to ensure it persists after this function returns.
	// (No title-brush tints here: SWindowTitleBarWidget forces the title brushes to NoBrush and polls
	// the palette for the title bar / title text, so any tint set here would be dead.)
	LogWindowStyle = FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");

	// A18: title-bar × reads as the destructive affordance it is. Stamped at open (like the rest of this
	// window style), so a theme toggle while the window is up is picked up on the next open.
	MobiusWindowButtonStyle::ApplyDangerCloseGlyph(LogWindowStyle, FindThemeSubsystemForLogWindow());

	// App typeface: Inter (composite UFont at /Game). Regular face, keep the ~10px log size. Colour is
	// polled per-paint below (InputMonoText). Falls back to the engine default face if the asset is
	// unavailable (early cook/commandlet path).
	FSlateFontInfo LogFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	if (UFont* Inter = LoadObject<UFont>(nullptr, TEXT("/Game/01_Dev/Widgets/Fonts/Font_Inter.Font_Inter")))
	{
		LogFont = FSlateFontInfo(Inter, 10, FName("Regular"));
	}
	const FTextBlockStyle LogTextStyle = FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		.SetFont(LogFont);

	// Themed Close button (stored as a member: SButton caches raw pointers into the style's brushes).
	// Background brushes are stamped for the theme current at construct; the label colour follows live.
	CloseButtonStyle = MobiusWindowButtonStyle::MakeWindowButtonStyle(FindThemeSubsystemForLogWindow());

	TSharedRef<SWidget> WindowPanel = SNew(SBorder)
		.Padding(FMargin(12.0f))
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([]()
		{
			// Fallback is the LIGHT palette value (product default): an early/unresolved window must not
			// render dark in Light theme just because the subsystem wasn't reachable yet.
			return FSlateColor(PollLogWindowColor(EMobiusPaletteRole::RibbonBg, MobiusThemePalette::Color(EMobiusPaletteRole::RibbonBg, /*bLight=*/true)));
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
						return FSlateColor(PollLogWindowColor(EMobiusPaletteRole::InputMonoText, MobiusThemePalette::Color(EMobiusPaletteRole::InputMonoText, /*bLight=*/true)));
					})
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
			[
				SAssignNew(CloseButton, SButton)
				.ButtonStyle(&CloseButtonStyle)
				.OnClicked(this, &SLogWindowWidget::HandleCloseClicked)
				[
					// Explicit label so the text colour follows a live theme toggle (same poll idiom as
					// the body). The button background brushes are stamped for the current theme at
					// construct (CloseButtonStyle above).
					SNew(STextBlock)
					.Text(FText::FromString("Close"))
					.Justification(ETextJustify::Center)
					.ColorAndOpacity_Lambda([]()
					{
						return FSlateColor(PollLogWindowColor(EMobiusPaletteRole::ButtonText, MobiusThemePalette::Color(EMobiusPaletteRole::ButtonText, /*bLight=*/true)));
					})
				]
			]
		];

	// Live re-theme for the Close button, event-driven off OnThemeChangedNative. The bind normally succeeds
	// right here; if it does not, fall back to a 2 Hz bootstrap timer that stops the moment it binds.
	//
	// The timer has to be registered on CloseButton, not on this controller: this widget is never slotted
	// under any window, so it is never painted, and SWidget::Paint is what pumps active timers. CloseButton
	// lives inside WindowPanel, which is painted.
	if (!TryBindThemeChanged() && CloseButton.IsValid())
	{
		CloseButton->RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SLogWindowWidget::EnsureThemeBinding));
	}

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

void SLogWindowWidget::ApplyCloseButtonTheme()
{
	// Only rebuild when the theme actually changed (sentinel forces the first apply). The CloseButtonStyle
	// built at construct is frozen at the construct-time theme; a reused window shown in another theme would
	// otherwise keep the wrong button background (the label already polls ButtonText live).
	if (const UUIThemeSubsystem* ThemeSubsystem = FindThemeSubsystemForLogWindow())
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

bool SLogWindowWidget::TryBindThemeChanged()
{
	if (ThemeChangedHandle.IsValid())
	{
		return true;
	}

	UUIThemeSubsystem* ThemeSubsystem = FindThemeSubsystemForLogWindow();
	if (!ThemeSubsystem)
	{
		return false;
	}

	BoundThemeSubsystem = ThemeSubsystem;
	ThemeChangedHandle = ThemeSubsystem->OnThemeChangedNative.AddSP(this, &SLogWindowWidget::ApplyCloseButtonTheme);

	// The event only fires on a later toggle, so stamp the current theme now.
	ApplyCloseButtonTheme();
	return true;
}

EActiveTimerReturnType SLogWindowWidget::EnsureThemeBinding(double InCurrentTime, float InDeltaTime)
{
	return TryBindThemeChanged() ? EActiveTimerReturnType::Stop : EActiveTimerReturnType::Continue;
}

void SLogWindowWidget::UnbindThemeChanged()
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
