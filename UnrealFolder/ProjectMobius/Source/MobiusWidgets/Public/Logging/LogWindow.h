// Log viewer window for Mobius custom logger output.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"

class SButton;
class SMoveableWindow;
class SScrollBox;
class STextBlock;
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
	 * Re-theme the Close button when the theme changes. Driven by an active timer registered ON THE CLOSE
	 * BUTTON (in OpenLogWindow), not on this controller: this widget is free-standing chrome — it spawns a
	 * separate SMoveableWindow and is never slotted under any window — so it is never painted, and
	 * SWidget::Paint is what pumps active timers. CloseButton lives inside the painted WindowPanel, so its
	 * timer fires. Cheap: only rebuilds CloseButtonStyle when GetTheme() differs from the cached value.
	 */
	EActiveTimerReturnType PollCloseButtonTheme(double InCurrentTime, float InDeltaTime);
	
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
	 * so the first poll always rebuilds and applies the button style.
	 */
	EMobiusUITheme LastAppliedCloseButtonTheme = static_cast<EMobiusUITheme>(0xFF);
};
