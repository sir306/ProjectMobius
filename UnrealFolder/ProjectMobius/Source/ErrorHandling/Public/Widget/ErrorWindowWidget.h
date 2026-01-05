// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "ErrorWindowWidget.generated.h"

class SErrorWindowWidget;
/**
 * 
 */
UCLASS()
class ERRORHANDLING_API UErrorWindowWidget : public UWidget
{
	GENERATED_BODY()
	
	
protected:
	TSharedPtr<SErrorWindowWidget> ErrorWindowWidget;
	
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

public:
	/** Update the title bar text for the error window. */
	void SetTitleBarText(const FText& TitleText);

	/** Update the error title text. */
	void SetErrorTitleText(const FText& TitleText);

	/** Update the error message text. */
	void SetErrorMessageText(const FText& MessageText);

	/** Update the optional error location text. */
	void SetErrorLocationText(const FText& LocationText);

	/** Ensure the error window is visible and focused. */
	void ShowErrorWindow();
};
