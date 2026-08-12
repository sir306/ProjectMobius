// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;
class SMoveableWindow;
class SWindowTitleBar;
class IWindowTitleBar;
class UUIThemeSubsystem;
enum class EMobiusUITheme : uint8;

/**
 * Reusable window title bar widget with customizable styling.
 */
class MOBIUSWIDGETS_API SWindowTitleBarWidget : public SCompoundWidget
{
public:
	/** Slate arguments for SWindowTitleBarWidget. */
	SLATE_BEGIN_ARGS(SWindowTitleBarWidget)
		: _OwnerWindow()
		, _TitleText(FText::GetEmpty())
                , _TitleTextStyle(&FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window").TitleTextStyle)
		, _WindowStyle(&FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window"))
		, _TitleAlignment(HAlign_Center)
		, _ShowAppIcon(false)
		, _CloseButtonToolTipText()
	{
	}

	/** Window that owns this title bar. */
	SLATE_ARGUMENT(TSharedPtr<SMoveableWindow>, OwnerWindow)

	/** Text displayed in the title bar. */
	SLATE_ATTRIBUTE(FText, TitleText)

	/** Style for the title text. */
	SLATE_STYLE_ARGUMENT(FTextBlockStyle, TitleTextStyle)

	/** Window style used by the title bar. */
	SLATE_STYLE_ARGUMENT(FWindowStyle, WindowStyle)

	/** Horizontal alignment for the title text. */
	SLATE_ARGUMENT(EHorizontalAlignment, TitleAlignment)

	/** Whether to show the application icon. */
	SLATE_ARGUMENT(bool, ShowAppIcon)

	/**
	 * Tooltip for the title bar's close button. Leave unset to keep SWindowTitleBar's default ("Close").
	 * Must be supplied at construction: SWindow exposes only a getter for its own equivalent, and this widget
	 * bypasses FSlateApplication::MakeWindowTitleBar, which is the only consumer of that field.
	 */
	SLATE_ATTRIBUTE(FText, CloseButtonToolTipText)
	SLATE_END_ARGS()

	/** Default constructor. */
	SWindowTitleBarWidget();

	/** Destructor. */
	~SWindowTitleBarWidget();

	/**
	 * Constructs the widget.
	 * @param InArgs Slate argument data.
	 */
	void Construct(const FArguments& InArgs);

	/**
	 * Update the title text.
	 * @param InTitleText New title text to display.
	 */
	void SetTitleText(const FText& InTitleText);

	/**
	 * Get the underlying title bar instance for window integration.
	 * @return Shared pointer to the title bar instance.
	 */
	TSharedPtr<IWindowTitleBar> GetTitleBar() const;

private:
        /**
         * A19-c: re-tint the close (x) glyph for the current theme. Bound to
         * UUIThemeSubsystem::OnThemeChangedNative, and called once at bind time for the initial stamp.
         *
         * Every other colour in this bar is a per-paint poll, but a brush TINT is not an attribute, so the
         * glyph was stamped once at open and a live toggle left it on the other theme's red. It must be
         * re-stamped here rather than by the window that owns the style, because Construct COPIES that
         * style into WindowStyle below. Writing the member in place is sufficient: the engine reads the
         * close-button image through its style pointer every paint (SWindowTitleBar::GetCloseImage).
         */
        void ApplyThemeToTitleBar();

        /** Bind ApplyThemeToTitleBar and do the first stamp. False if there is no GameInstance yet. */
        bool TryBindThemeChanged();

        /** Bootstrap retry for TryBindThemeChanged; returns Stop as soon as it binds, so this is not a poll. */
        EActiveTimerReturnType EnsureThemeBinding(double InCurrentTime, float InDeltaTime);

        /** Drop the OnThemeChangedNative binding, if any. Safe to call when never bound. */
        void UnbindThemeChanged();

        TSharedPtr<SWindowTitleBar> TitleBarWidget;
        TSharedPtr<STextBlock> TitleTextBlock;
        FWindowStyle WindowStyle;

        /** Subsystem we bound OnThemeChangedNative on, and the handle to remove. Weak: the GameInstance can
         *  go first (PIE stop) and window chrome outlives nothing in particular. */
        TWeakObjectPtr<UUIThemeSubsystem> BoundThemeSubsystem;
        FDelegateHandle ThemeChangedHandle;

        /** Theme last stamped into WindowStyle's close glyph. Out-of-range sentinel (Dark=0/Light=1) so the
         *  first apply always runs. */
        EMobiusUITheme LastAppliedTheme = static_cast<EMobiusUITheme>(0xFF);
        FTextBlockStyle TitleTextStyle;
};
