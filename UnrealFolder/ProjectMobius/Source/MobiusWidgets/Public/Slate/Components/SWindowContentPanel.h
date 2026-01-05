// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"

class SFieldAndTitleText;
class STextBlock;
class SWidget;

/**
 * Reusable window content panel with title, message, and optional location text.
 */
class MOBIUSWIDGETS_API SWindowContentPanel : public SCompoundWidget
{
public:
	/** Slate arguments for SWindowContentPanel. */
	SLATE_BEGIN_ARGS(SWindowContentPanel)
		: _TitleText(FText::GetEmpty())
		, _MessageText(FText::GetEmpty())
		, _LocationText(FText::GetEmpty())
		, _TitleTextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		, _MessageTextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		, _LocationTextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
	{
	}

	/** Header text to display. */
	SLATE_ATTRIBUTE(FText, TitleText)

	/** Main message text to display. */
	SLATE_ATTRIBUTE(FText, MessageText)

	/** Optional location text to display. */
	SLATE_ATTRIBUTE(FText, LocationText)

	/** Style for the header text. */
	SLATE_STYLE_ARGUMENT(FTextBlockStyle, TitleTextStyle)

	/** Style for the message text. */
	SLATE_STYLE_ARGUMENT(FTextBlockStyle, MessageTextStyle)

	/** Style for the location text. */
	SLATE_STYLE_ARGUMENT(FTextBlockStyle, LocationTextStyle)
	SLATE_END_ARGS()

	/** Default constructor. */
	SWindowContentPanel();

	/** Destructor. */
	~SWindowContentPanel();

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
	 * Update the message text.
	 * @param InMessageText New message text to display.
	 */
	void SetMessageText(const FText& InMessageText);

	/**
	 * Update the optional location text.
	 * @param InLocationText New location text to display.
	 */
	void SetLocationText(const FText& InLocationText);

private:
	TSharedPtr<SFieldAndTitleText> TitleMessageWidget;
	TSharedPtr<STextBlock> LocationTextBlock;
	TSharedPtr<SWidget> LocationContainer;
};
