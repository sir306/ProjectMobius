// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
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
#include "MoveableWidget.generated.h"

/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UMoveableWidget : public UUserWidget
{
	GENERATED_BODY()
#pragma region METHODS
public:
	// Native Pre Construct
	virtual void NativePreConstruct() override;

	// Native Constructor 
	virtual void NativeConstruct() override;

	// Tick Method for in C++ for the widget
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Method to move the widgets around */
	UFUNCTION()
	void OnMoveableButtonPressed();

	UFUNCTION()
	void OnMoveableButtonReleased();

	void UpdateLocation();
#pragma endregion METHODS

#pragma region COMPONENTS_AND_VARIABLES
public:
	/** The Canvas to hold the child widgets, this is how we move all and keep shape */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UCanvasPanel* MoveableCanvas;

	/** The button at the top of this panel - where a user can click and move the widget */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UButton* MoveableButton;

	/** Image to use as a background for the widgets */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UImage* BackgroundImage;

	/** TimerHandle for getting new position of mouse and widget */
	UPROPERTY(BlueprintReadOnly)
	FTimerHandle MoveableTimerHandle;

	/** FVector2D to store mouse position difference when clicked */
	UPROPERTY(BlueprintReadOnly)
	FVector2D MousePositionDifference;
	
#pragma endregion COMPONENTS_AND_VARIABLES
};
