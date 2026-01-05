// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;
class SWidget;

/**
 * 
 */
class ERRORHANDLING_API SErrorWindowWidget final : public SCompoundWidget
{
public:
	/**  */
	SLATE_BEGIN_ARGS(SErrorWindowWidget)
		{}
	SLATE_END_ARGS()
	
	SErrorWindowWidget();
	~SErrorWindowWidget();
	
	/** Constructs and initializes the widget
	 * @param InArgs The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs);

	/** Update the window title bar text. */
	void SetTitleBarText(const FText& TitleText);

	/** Update the error title text. */
	void SetErrorTitleText(const FText& TitleText);

	/** Update the error message text. */
	void SetErrorMessageText(const FText& MessageText);

	/** Update the optional error location text. */
	void SetErrorLocationText(const FText& LocationText);

	/** Ensure the window is visible and in front. */
	void ShowErrorWindow();
	
	/** Paints this widget in the game viewport
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
	TSharedPtr<STextBlock> TitleBarTextBlock;
	TSharedPtr<STextBlock> ErrorTitleTextBlock;
	TSharedPtr<STextBlock> ErrorMessageTextBlock;
	TSharedPtr<STextBlock> ErrorLocationTextBlock;
	TSharedPtr<SWidget> ErrorLocationContainer;
	FWindowStyle ErrorWindowStyle;
};
