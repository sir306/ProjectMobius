// Log viewer window for Mobius custom logger output.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"

class FActiveTimerHandle;
class ITableRow;
class SButton;
class SMoveableWindow;
class STableViewBase;
class UUIThemeSubsystem;
template <typename ItemType> class SListView;
enum class EMobiusUITheme : uint8;

DECLARE_DELEGATE(FOnLogWindowClosed);

class MOBIUSWIDGETS_API SLogWindowWidget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLogWindowWidget)
		{}
		SLATE_EVENT(FOnLogWindowClosed, OnLogWindowClosed)
	SLATE_END_ARGS()

	SLogWindowWidget();
	virtual ~SLogWindowWidget();

	void Construct(const FArguments& InArgs);

	/** Append a single log line to the viewer. */
	void AppendLine(const FString& Line);

	/** Replace the current log contents with a cached list. */
	void SetLines(const TArray<FString>& Lines);

	/** Bring the log window to the front. */
	void ShowLogWindow();

	/** Close the log window if open. */
	void CloseLogWindow();

	/** Enable or disable the log window. */
	void SetEnabled(bool bEnabled);

	/** True if the log window is currently open. */
	bool IsOpen() const;

private:
	void OpenLogWindow();
	FReply HandleCloseClicked();

	/** Row factory for LogListView: one STextBlock per line, built only for rows on screen. */
	TSharedRef<ITableRow> GenerateLogRow(TSharedPtr<FString> Line, const TSharedRef<STableViewBase>& OwnerTable);

	/** Drop the oldest lines down to MaxLines - LogTrimBatch. Returns how many were removed. */
	int32 TrimBacklog();

	/**
	 * Tell the list its items changed. Cheap and idempotent - the list regenerates once, on its next
	 * paint, for the visible rows only, however many lines arrived in between. Follows the end of the
	 * log unless the reader has scrolled away from it (bForceToEnd overrides that); a reader who has
	 * scrolled up keeps their place even when RemovedFromFront lines were trimmed above them.
	 */
	void RefreshLogList(int32 RemovedFromFront, bool bForceToEnd);

	/** ScrollToBottom, flagged as ours so HandleLogListScrolled does not read it as the reader's. */
	void PinLogListToEnd();

	/** Start (or extend) the SettleLogListEnd timer on LogListView. */
	void ArmLogListSettle();

	/**
	 * Re-pins the end for a few frames after a change. A new row's text is measured before it has a wrap
	 * width (AutoWrapText learns it from the row's first paint), so the frame that adds wrapping lines
	 * fits too many rows and the newest ones slide below the fold once they wrap. Registered on
	 * LogListView, which is painted - this widget never is.
	 */
	EActiveTimerReturnType SettleLogListEnd(double InCurrentTime, float InDeltaTime);

	/**
	 * Bound to the list's OnListViewScrolled. Only the reader's own scroll input (wheel, thumb drag,
	 * keys) stops the follow: our own scrolls are flagged, and the list's end-of-list clamp fires this
	 * from inside its Tick, i.e. during paint, never while Slate is processing input.
	 */
	void HandleLogListScrolled(double InScrollOffset);

	/**
	 * Re-theme the Close button for the current theme. Event-driven: bound to
	 * UUIThemeSubsystem::OnThemeChangedNative, and also called once at bind time for the initial apply.
	 *
	 * This used to be an active timer with a 0-second period registered on the Close button (this widget is
	 * free-standing chrome, never slotted under any window, so its own timers never fire). A 0-second timer
	 * fires every frame and holds the owning window in Slate's must-tick set; only the style rebuild was
	 * theme-guarded, not the poll itself.
	 */
	void ApplyCloseButtonTheme();

	/**
	 * Bind ApplyCloseButtonTheme to the theme subsystem and do the first apply. Returns false if the
	 * subsystem does not exist yet — it is a GameInstanceSubsystem, so there may be no game instance.
	 * Idempotent: a second call while already bound is a no-op that returns true.
	 */
	bool TryBindThemeChanged();

	/** Bootstrap retry for TryBindThemeChanged; returns Stop as soon as it binds, so this is not a poll. */
	EActiveTimerReturnType EnsureThemeBinding(double InCurrentTime, float InDeltaTime);

	/** Drop the OnThemeChangedNative binding, if any. Safe to call when never bound. */
	void UnbindThemeChanged();

	/**
	 * Drops the theme binding and every handle into the window's widget tree. Does NOT destroy the
	 * window — this is the teardown half that both close routes share, so it is safe to run from
	 * inside SWindow::NotifyWindowBeingDestroyed.
	 */
	void ReleaseWindowState();

	/**
	 * Bound to the window's OnWindowClosed event, which is the ONLY hook every close route reaches:
	 * the title-bar x, Alt+F4, an OS close, and Slate tearing down a parent window all end at
	 * SWindow::NotifyWindowBeingDestroyed. It used to only forward OnLogWindowClosed, so a title-bar x
	 * destroyed the native window and left LogWindowPtr valid — making OpenLogWindow's IsValid()
	 * early-out permanent for any holder that reused this widget.
	 */
	void HandleLogWindowClosedEvent(const TSharedRef<SWindow>& InWindow);
	FOnLogWindowClosed OnLogWindowClosed;

	TSharedPtr<SMoveableWindow> LogWindowPtr;

	/**
	 * Virtualized: one row per line, laid out only while on screen. This used to be a single STextBlock
	 * holding the whole backlog, rebuilt and re-laid out on EVERY appended line - with a full 1000-line
	 * backlog that is ~90 KB of text per line, and the logger delivers its queue one line at a time.
	 */
	TSharedPtr<SListView<TSharedPtr<FString>>> LogListView;

	/** The viewer's contents, oldest first; LogListView's items source. Survives a close/reopen. */
	TArray<TSharedPtr<FString>> LogItems;

	/** Hard cap on LogItems. */
	int32 MaxLines = 1000;

	/**
	 * Trim granularity: overflowing MaxLines drops this many extra lines at once, so the front-of-array
	 * memmove is paid once per batch instead of once per appended line.
	 */
	int32 LogTrimBatch = 128;

	/** True while the view follows the newest line. Cleared only by the reader scrolling; restored when
	 *  they scroll back to the end, and by every (re)open. */
	bool bFollowLogEnd = true;

	/** Set around our own scroll calls, so HandleLogListScrolled can tell them from the reader's. */
	bool bScrollingLogListProgrammatically = false;

	/** Frames SettleLogListEnd still has to re-pin, and its timer (owned by LogListView, dies with it). */
	int32 LogSettleFramesLeft = 0;
	TWeakPtr<FActiveTimerHandle> LogSettleTimer;

	bool bIsEnabled = true;
	FWindowStyle LogWindowStyle;

	/** Members, not locals: OnGenerateRow builds rows long after OpenLogWindow returns, and STableRow /
	 *  SListView keep a pointer to the style they were given. */
	FTextBlockStyle LogTextStyle;
	FTableRowStyle LogRowStyle;
	FTableViewStyle LogListViewStyle;
	/** Themed Close-button style (member so SButton's cached brush pointers stay valid). */
	FButtonStyle CloseButtonStyle;

	/** Close button, stored so its style can be re-applied on a live theme change (see PollCloseButtonTheme). */
	TSharedPtr<SButton> CloseButton;

	/**
	 * Theme last stamped into CloseButtonStyle. Initialised to an out-of-range sentinel (Dark=0/Light=1)
	 * so the first apply always rebuilds the button style.
	 */
	EMobiusUITheme LastAppliedCloseButtonTheme = static_cast<EMobiusUITheme>(0xFF);

	/** Subsystem we bound OnThemeChangedNative on, and the handle to remove. Weak: the GameInstance can go
	 *  first (PIE stop) and this window is free-standing chrome that outlives nothing in particular. */
	TWeakObjectPtr<UUIThemeSubsystem> BoundThemeSubsystem;
	FDelegateHandle ThemeChangedHandle;
};
