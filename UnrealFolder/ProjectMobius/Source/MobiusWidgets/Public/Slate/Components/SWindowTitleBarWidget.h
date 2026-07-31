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
        TSharedPtr<SWindowTitleBar> TitleBarWidget;
        TSharedPtr<STextBlock> TitleTextBlock;
        FWindowStyle WindowStyle;
        FTextBlockStyle TitleTextStyle;
};
