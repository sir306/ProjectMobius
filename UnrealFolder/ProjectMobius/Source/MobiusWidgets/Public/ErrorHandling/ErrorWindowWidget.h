// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "ErrorWindowWidget.generated.h"

class SErrorWindowWidget;
class UMobiusWidgetSubsystem;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UErrorWindowWidget : public UWidget
{
	GENERATED_BODY()
	
	
protected:
        TSharedPtr<SErrorWindowWidget> ErrorWindowWidget;
        TWeakObjectPtr<UMobiusWidgetSubsystem> MoveableWindowSubsystem;
	
	/**
	 * Builds the underlying Slate widget.
	 * @return The constructed Slate widget.
	 */
	virtual TSharedRef<SWidget> RebuildWidget() override;
	/**
	 * Release Slate resources and reset any cached widget state.
	 * @param bReleaseChildren True to release child widgets as well.
	 */
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

public:
	/**
	 * Update the title bar text for the error window.
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
	 * Ensure the error window is visible and focused.
	 */
	void ShowErrorWindow();
};
