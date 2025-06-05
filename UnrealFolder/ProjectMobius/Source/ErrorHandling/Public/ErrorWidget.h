// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 Nicholas R. Harding
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.  
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,  
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL  
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR  
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING  
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS  
 * IN THE SOFTWARE.
 */



#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "ErrorWidget.generated.h"

/**
 * This 
 */
UCLASS()
class ERRORHANDLING_API UErrorWidget : public UUserWidget
{
	GENERATED_BODY()
	
#pragma region METHODS
public:
	// PreConstruct Method
	virtual void NativePreConstruct() override;
	
	// Constructor 
	virtual void NativeConstruct() override;

	// Tick Method for in C++ for the widget
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

protected:

private:
	
#pragma endregion METHODS

#pragma region PROPERTIES
public:

protected:

private:
#pragma region PRIVATE_CLASS_COMPONENTS
	//TODO: Change UTextBlocks to RichTextBlocks for better formatting and styling
	/** Text block for Error Title */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ErrorTitleTextBlock;
	
	/** Text block for the error message */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ErrorMessageTextBlock;

#pragma endregion PRIVATE_CLASS_COMPONENTS

#pragma region PRIVATE_VARIABLES

#pragma endregion PRIVATE_VARIABLES

#pragma endregion PROPERTIES


#pragma region GETTERS_SETTERS
public:
#pragma region GETTERS

#pragma endregion GETTERS

#pragma region SETTERS
	/** Setter to show popup */
	FORCEINLINE void SetPopupVisible(const bool bVisible)
	{
		this->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}

	/** Set the error title */
	FORCEINLINE void SetErrorTitle(const FText& ErrorTitle) const
	{
		if (ErrorTitleTextBlock)
		{
			ErrorTitleTextBlock->SetText(ErrorTitle);
		}
	}

	/** Set the error message */
	FORCEINLINE void SetErrorMessage(const FText& ErrorMessage) const
	{
		if (ErrorMessageTextBlock)
		{
			ErrorMessageTextBlock->SetText(ErrorMessage);
		}
	}
#pragma endregion SETTERS
#pragma endregion GETTERS_SETTERS

};
