// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class SWindowContentPanel;
class SWindowTitleBarWidget;

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
	/** Creates and shows the test error window. */
	void OpenErrorWindow();
	/** Requests the test window to close. */
	void CloseErrorWindow();
	/** Handles the Close button click. */
	FReply HandleCloseClicked();

	TSharedPtr<SWindow> ErrorWindowPtr;
	TSharedPtr<SWindowTitleBarWidget> TitleBarWidget;
	TSharedPtr<SWindowContentPanel> ContentPanel;
	FWindowStyle ErrorWindowStyle;
};
