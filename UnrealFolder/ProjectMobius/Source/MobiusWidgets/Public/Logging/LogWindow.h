// Log viewer window for Mobius custom logger output.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"

class SMoveableWindow;
class SScrollBox;
class STextBlock;

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
};
