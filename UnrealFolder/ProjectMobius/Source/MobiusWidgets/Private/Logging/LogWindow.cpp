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
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

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

	// O(1) per line whatever the backlog. The logger's flush calls this once per queued line, so a
	// heatmap load arrives as thousands of calls in one frame; all of them together cost one list
	// regeneration (visible rows only) on the next paint.
	LogItems.Add(MakeShared<FString>(Line));
	const int32 Removed = LogItems.Num() > MaxLines ? TrimBacklog() : 0;

	RefreshLogList(Removed, /*bForceToEnd=*/false);
}

void SLogWindowWidget::SetLines(const TArray<FString>& Lines)
{
	const int32 First = FMath::Max(0, Lines.Num() - MaxLines);
	LogItems.Reset(Lines.Num() - First);
	for (int32 Index = First; Index < Lines.Num(); ++Index)
	{
		LogItems.Add(MakeShared<FString>(Lines[Index]));
	}

	RefreshLogList(/*RemovedFromFront=*/0, /*bForceToEnd=*/true);
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
	// Clear the member BEFORE asking for the destroy, not after. FSlateApplication::RequestDestroyWindow
	// runs DestroyWindowsImmediately() inline (SlateApplication.cpp:2351), so NotifyWindowBeingDestroyed —
	// and therefore HandleLogWindowClosedEvent below — fires on THIS call stack, before the old
	// `LogWindowPtr.Reset()` after the call would have run. SWindow::RequestDestroyWindow's "not destroyed
	// immediately... queue for destruction on next Tick" comment is stale; on the game thread it is
	// synchronous. Handing the destroy a local keeps the window alive across the call.
	//
	// TOUCH NOTHING AFTER THE DESTROY CALL — no member write, no member read, not even a Reset(). The
	// destroy is synchronous, so it runs HandleLogWindowClosedEvent ->
	// UMobiusWidgetSubsystem::LogWindowIsClosing -> LogWindowWidget.Reset(), which drops the last
	// reference to THIS widget; the AddSP pin releases when the broadcast ends and the destructor runs
	// before the stack unwinds back here. `this` is freed memory by the time RequestDestroyWindow
	// returns. Only the stack local WindowToDestroy is safe to name. The previous form did exactly the
	// forbidden thing — `LogWindowPtr.Reset()` on the line after the destroy — so the reordering closed
	// a latent use-after-write as well as the reopen bug. SErrorWindowWidget is not exposed to this
	// (UErrorWindowWidget resets its holder only in ReleaseSlateResources), but keep the two functions
	// shaped the same so the difference is not mistaken for licence to add a trailing line here.
	const TSharedPtr<SMoveableWindow> WindowToDestroy = LogWindowPtr;
	ReleaseWindowState();

	if (WindowToDestroy.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().RequestDestroyWindow(WindowToDestroy.ToSharedRef());
	}
}

void SLogWindowWidget::ReleaseWindowState()
{
	// Unconditional: the binding outlives the window pointer, and OpenLogWindow re-binds on reopen.
	UnbindThemeChanged();

	// LogListView holds a raw pointer to LogItems. Detach it before letting go: on the OnWindowClosed
	// route this widget - and LogItems with it - is destroyed inside the callback while the window's tree
	// is still alive, so a list left pointing at LogItems would outlive the array it reads.
	if (LogListView.IsValid())
	{
		LogListView->ClearItemsSource();
	}

	// LogListView / CloseButton live inside the destroyed window's tree and are rebuilt by
	// OpenLogWindow. LogItems is NOT cleared: it is the viewer's contents, and OpenLogWindow builds the
	// new list over it, so a reopen shows the same backlog.
	LogWindowPtr.Reset();
	LogListView.Reset();
	CloseButton.Reset();
	LogSettleTimer.Reset();
	LogSettleFramesLeft = 0;
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
	LogTextStyle = FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		.SetFont(LogFont);

	// Rows and list draw nothing of their own - no hover, selection or alternating-row brushes, no list
	// background - so the themed RibbonBg border shows through exactly as it did behind the old single
	// text block.
	const FSlateNoResource NoBrush;
	LogRowStyle = FTableRowStyle(FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"))
		.SetSelectorFocusedBrush(NoBrush)
		.SetActiveHoveredBrush(NoBrush)
		.SetActiveBrush(NoBrush)
		.SetInactiveHoveredBrush(NoBrush)
		.SetInactiveBrush(NoBrush)
		.SetEvenRowBackgroundHoveredBrush(NoBrush)
		.SetEvenRowBackgroundBrush(NoBrush)
		.SetOddRowBackgroundHoveredBrush(NoBrush)
		.SetOddRowBackgroundBrush(NoBrush)
		.SetActiveHighlightedBrush(NoBrush)
		.SetInactiveHighlightedBrush(NoBrush);
	LogListViewStyle = FTableViewStyle().SetBackgroundBrush(NoBrush);

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
				SAssignNew(LogListView, SListView<TSharedPtr<FString>>)
				.ListItemsSource(&LogItems)
				.OnGenerateRow(this, &SLogWindowWidget::GenerateLogRow)
				.OnListViewScrolled(this, &SLogWindowWidget::HandleLogListScrolled)
				.SelectionMode(ESelectionMode::None)
				.ListViewStyle(&LogListViewStyle)
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

	// A fresh list starts at the top; open on the newest line, as the old text block's ScrollToEnd did.
	RefreshLogList(/*RemovedFromFront=*/0, /*bForceToEnd=*/true);

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

	// Converge EVERY close route on one teardown, and notify OnLogWindowClosed from it (see
	// HandleLogWindowClosedEvent). AddSP rather than a raw binding or a lambda capturing `this`: the
	// destructor's CloseLogWindow() destroys the window while this widget is already being torn down, and
	// the weak pin makes the callback a no-op then instead of re-entering a half-destructed object. A
	// fresh SMoveableWindow is built per open, so the binding dies with the window — no handle to track.
	LogWindowPtr.ToSharedRef()->GetOnWindowClosedEvent().AddSP(this, &SLogWindowWidget::HandleLogWindowClosedEvent);
}

FReply SLogWindowWidget::HandleCloseClicked()
{
	CloseLogWindow();
	return FReply::Handled();
}

TSharedRef<ITableRow> SLogWindowWidget::GenerateLogRow(TSharedPtr<FString> Line, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		.Style(&LogRowStyle)
		.ShowSelection(false)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Line.IsValid() ? *Line : FString()))
			.AutoWrapText(true)
			.TextStyle(&LogTextStyle)
			// Mono log text follows the theme (readable on either surface) via a live poll.
			.ColorAndOpacity_Lambda([]()
			{
				return FSlateColor(PollLogWindowColor(EMobiusPaletteRole::InputMonoText, MobiusThemePalette::Color(EMobiusPaletteRole::InputMonoText, /*bLight=*/true)));
			})
		];
}

int32 SLogWindowWidget::TrimBacklog()
{
	const int32 Keep = FMath::Max(0, MaxLines - LogTrimBatch);
	const int32 Removed = FMath::Max(0, LogItems.Num() - Keep);
	if (Removed > 0)
	{
		LogItems.RemoveAt(0, Removed, EAllowShrinking::No);
	}
	return Removed;
}

void SLogWindowWidget::RefreshLogList(int32 RemovedFromFront, bool bForceToEnd)
{
	// Closed: nothing to refresh. OpenLogWindow builds a fresh list over LogItems.
	if (!LogListView.IsValid())
	{
		return;
	}

	if (bForceToEnd)
	{
		bFollowLogEnd = true;
	}
	else if (!bFollowLogEnd && !LogListView->IsUserScrolling()
		&& LogListView->GetScrollDistanceRemaining().Y <= KINDA_SMALL_NUMBER)
	{
		// The reader scrolled back down to the end (scrollbar state from the list's last layout): follow again.
		bFollowLogEnd = true;
	}

	LogListView->RequestListRefresh();

	if (bFollowLogEnd)
	{
		PinLogListToEnd();
		ArmLogListSettle();
	}
	else if (RemovedFromFront > 0)
	{
		// The scroll offset is an item index: shift it with the trim so the lines being read stay put.
		TGuardValue<bool> OwnScroll(bScrollingLogListProgrammatically, true);
		LogListView->SetScrollOffset(FMath::Max(0.0f, LogListView->GetScrollOffset() - static_cast<float>(RemovedFromFront)));
	}
}

void SLogWindowWidget::PinLogListToEnd()
{
	if (LogListView.IsValid())
	{
		TGuardValue<bool> OwnScroll(bScrollingLogListProgrammatically, true);
		LogListView->ScrollToBottom();
	}
}

void SLogWindowWidget::ArmLogListSettle()
{
	// Three frames: the one that generates the new rows (measured unwrapped), the one that re-measures
	// them wrapped, and one spare. A burst re-arms rather than stacking timers.
	LogSettleFramesLeft = 3;
	if (!LogSettleTimer.IsValid() && LogListView.IsValid())
	{
		LogSettleTimer = LogListView->RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SLogWindowWidget::SettleLogListEnd));
	}
}

EActiveTimerReturnType SLogWindowWidget::SettleLogListEnd(double InCurrentTime, float InDeltaTime)
{
	// Runs in the list's Paint before its Tick, so the re-pin is laid out in this same frame.
	if (!bFollowLogEnd || !LogListView.IsValid() || LogSettleFramesLeft <= 0)
	{
		LogSettleFramesLeft = 0;
		return EActiveTimerReturnType::Stop;
	}

	--LogSettleFramesLeft;
	PinLogListToEnd();
	return LogSettleFramesLeft > 0 ? EActiveTimerReturnType::Continue : EActiveTimerReturnType::Stop;
}

void SLogWindowWidget::HandleLogListScrolled(double InScrollOffset)
{
	if (!bScrollingLogListProgrammatically && FSlateApplication::IsInitialized() && FSlateApplication::Get().IsProcessingInput())
	{
		bFollowLogEnd = false;
	}
}

void SLogWindowWidget::HandleLogWindowClosedEvent(const TSharedRef<SWindow>& InWindow)
{
	// Teardown ONLY — deliberately NOT CloseLogWindow(). By the time this fires the window is already
	// inside FSlateApplication::PrivateDestroyWindow and has been popped from WindowDestroyQueue
	// (SlateApplication.cpp:3114), so a second RequestDestroyWindow would re-queue it and the nested
	// DestroyWindowsImmediately would re-run NotifyWindowBeingDestroyed, Renderer->OnWindowDestroyed and
	// NativeWindow->Destroy on the same window.
	//
	// RESET FIRST, NOTIFY SECOND — the order is load-bearing, not tidiness. The subscriber is
	// UMobiusWidgetSubsystem::LogWindowIsClosing, and it drops the last reference to THIS widget. The
	// AddSP pin below keeps us alive only until ExecuteIfSafe returns, so ~SLogWindowWidget (and its
	// CloseLogWindow) runs inside this very callback. With LogWindowPtr already null that destructor is
	// inert; with it still set it would request a destroy of the window currently being destroyed.
	ReleaseWindowState();

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
