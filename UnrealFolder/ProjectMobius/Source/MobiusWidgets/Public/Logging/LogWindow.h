// Log viewer window for Mobius custom logger output.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"

class SButton;
class SMoveableWindow;
class SScrollBox;
class STextBlock;
class UUIThemeSubsystem;
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
	void RebuildLogText();

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

	void HandleLogWindowClosedEvent(const TSharedRef<SWindow>& InWindow);
	FOnLogWindowClosed OnLogWindowClosed;

	TSharedPtr<SMoveableWindow> LogWindowPtr;
	TSharedPtr<SScrollBox> ScrollBox;
	TSharedPtr<STextBlock> LogTextBlock;
	FString LogText;
	TArray<FString> LogLines;
	int32 MaxLines = 1000;
	bool bIsEnabled = true;
	FWindowStyle LogWindowStyle;
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
