// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "MobiusErrorTypes.h"   // EMobiusErrorSeverity (member default-init)

class SButton;
class SMoveableWindow;
class SWindow;
class SWindowContentPanel;
class UUIThemeSubsystem;
enum class EMobiusUITheme : uint8;

/**
 * 
 */
class MOBIUSWIDGETS_API SErrorWindowWidget final : public SCompoundWidget
{
public:
	/**  */
	SLATE_BEGIN_ARGS(SErrorWindowWidget)
		{}
	SLATE_END_ARGS()
	
	/** Default constructor. */
	SErrorWindowWidget();
	/** Destructor. */
	~SErrorWindowWidget();
	
	/**
	 * Constructs and initializes the widget.
	 * @param InArgs The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs);

	/**
	 * Update the window title bar text.
	 * @param TitleText Text to display in the title bar.
	 */
	void SetTitleBarText(const FText& TitleText);

	/**
	 * Update the error title text.
	 * @param TitleText Error title to display.
	 */
	void SetErrorTitleText(const FText& TitleText);

	/**
	 * Update the error message text.
	 * @param MessageText Error message to display.
	 */
	void SetErrorMessageText(const FText& MessageText);

	/**
	 * Update the optional error location text.
	 * @param LocationText Optional location text for where the error occurred.
	 */
	void SetErrorLocationText(const FText& LocationText);

	/**
	 * Ensure the window is visible and in front.
	 */
	void ShowErrorWindow();

	/**
	 * True while this widget owns a live native window. Used by UMobiusWidgetSubsystem to decide whether a
	 * NEW error needs its own window rather than overwriting the message already on screen.
	 */
	bool IsWindowOpen() const { return ErrorWindowPtr.IsValid(); }

	/**
	 * Nudge the window away from its auto-centred position, so stacked error windows do not land exactly
	 * on top of each other. Applied after the window is added; a no-op if there is no live window.
	 * @param Delta Screen-space offset in pixels.
	 */
	void OffsetWindowPosition(const FVector2D& Delta);

	/**
	 * Set the severity that drives the emphasis cue (top accent bar): Error/Fatal = red, Warning =
	 * amber, Info = accent. Read live by the accent colour lambda, so this is safe to call after the
	 * window is shown (the window is reused across errors, not rebuilt each time).
	 * @param InSeverity Severity of the currently displayed message.
	 */
	void SetSeverity(EMobiusErrorSeverity InSeverity);
	
	/**
	 * Paints this widget in the game viewport.
	  * @param Args The paint arguments
	  * @param AllottedGeometry The space allotted for this widget
	  * @param MyCullingRect The culling rect for this widget
	  * @param OutDrawElements A list of elements to draw
	  * @param LayerId The layer to draw on
	  * @param InWidgetStyle The style for the widget
	  * @param bParentEnabled True if the parent is enabled
	  * @return The layer ID that was drawn on
	  */
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	                      FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
	                      bool bParentEnabled) const override;


private:
	/**
	 * Re-apply everything in this window that is SNAPSHOTTED rather than polled: the Close button style,
	 * the three body text colours (A19) and the window chrome. Event-driven: bound to
	 * UUIThemeSubsystem::OnThemeChangedNative, and also called once at bind time for the initial apply.
	 *
	 * Was ApplyCloseButtonTheme, which covered only the button — the title / message / reporter text stayed
	 * at their open-time theme, so a toggle with the window up moved half the window and left the other
	 * half behind. That asymmetry is invisible if you only ever open in one theme, which is why it survived.
	 *
	 * This used to be an active timer with a 0-second period registered on the Close button (this widget is
	 * free-standing chrome, never slotted under any window, so its own timers never fire — the OnPaint
	 * override above is dead). A 0-second timer fires every frame and holds the owning window in Slate's
	 * must-tick set; only the style rebuild was theme-guarded, not the poll itself.
	 */
	void ApplyThemeToWindow();

	/**
	 * Bind ApplyThemeToWindow to the theme subsystem and do the first apply. Returns false if the
	 * subsystem does not exist yet — it is a GameInstanceSubsystem, so there may be no game instance.
	 * Idempotent: a second call while already bound is a no-op that returns true.
	 */
	bool TryBindThemeChanged();

	/** Bootstrap retry for TryBindThemeChanged; returns Stop as soon as it binds, so this is not a poll. */
	EActiveTimerReturnType EnsureThemeBinding(double InCurrentTime, float InDeltaTime);

	/** Drop the OnThemeChangedNative binding, if any. Safe to call when never bound. */
	void UnbindThemeChanged();

	/** Creates and shows the test error window. */
	void OpenErrorWindow();
	/** Requests the test window to close. */
	void CloseErrorWindow();
	/** Handles the Close button click. */
	FReply HandleCloseClicked();

	/**
	 * Drops the theme binding and every handle into the window's widget tree. Does NOT destroy the
	 * window — this is the teardown half that both close routes share, so it is safe to run from
	 * inside SWindow::NotifyWindowBeingDestroyed.
	 */
	void ReleaseWindowState();

	/**
	 * Bound to the window's OnWindowClosed event, which is the ONLY hook every close route reaches:
	 * the title-bar x, Alt+F4, an OS close, and Slate tearing down a parent window all end at
	 * SWindow::NotifyWindowBeingDestroyed. Without it, a title-bar x destroyed the native window and
	 * left ErrorWindowPtr valid, so OpenErrorWindow's IsValid() early-out made the window
	 * unreopenable for the rest of the session.
	 */
	void HandleWindowClosed(const TSharedRef<SWindow>& InWindow);

	TSharedPtr<SMoveableWindow> ErrorWindowPtr;
	TSharedPtr<SWindowContentPanel> ContentPanel;
	FWindowStyle ErrorWindowStyle;

	// Text styles are stored as members (locally size-bumped copies of the shared ramp): this native
	// window gets no UMG UI-scale/DPI multiplier, so the shared 12/10 sizes render tiny. The content
	// panel is fed pointers to these, so they must outlive OpenErrorWindow.
	FTextBlockStyle TitleTextStyle;
	FTextBlockStyle MessageTextStyle;
	FTextBlockStyle LocationTextStyle;

	/** Themed Close-button style (member so SButton's cached brush pointers stay valid). */
	FButtonStyle CloseButtonStyle;

	/** Close button, stored so its style can be re-applied on a live theme change (see ApplyThemeToWindow). */
	TSharedPtr<SButton> CloseButton;

	/**
	 * Theme last stamped by ApplyThemeToWindow. Initialised to (and reset by ReleaseWindowState back to) an
	 * out-of-range sentinel (Dark=0/Light=1) so the first apply after every open always re-stamps.
	 */
	EMobiusUITheme LastAppliedTheme = static_cast<EMobiusUITheme>(0xFF);

	/** Subsystem we bound OnThemeChangedNative on, and the handle to remove. Weak: the GameInstance can go
	 *  first (PIE stop) and this window is free-standing chrome that outlives nothing in particular. */
	TWeakObjectPtr<UUIThemeSubsystem> BoundThemeSubsystem;
	FDelegateHandle ThemeChangedHandle;

	/**
	 * Drives the top accent colour by severity. Held in a shared value (not a plain member) so the
	 * accent colour lambda can capture it by value: FSlateApplication::RequestDestroyWindow can defer
	 * the window's destruction past this widget's own destructor, and the codebase's window chrome
	 * deliberately avoids capturing raw `this` in paint lambdas for exactly that lifetime reason.
	 */
	TSharedRef<EMobiusErrorSeverity> CurrentSeverity = MakeShared<EMobiusErrorSeverity>(EMobiusErrorSeverity::Error);
};
