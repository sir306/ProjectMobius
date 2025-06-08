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
#include "Subsystems/WorldSubsystem.h"
#include "MobiusWidgetSubsystem.generated.h"

class ULoadingNotifyWidget;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UMobiusWidgetSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
#pragma region METHODS
public:
	UMobiusWidgetSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	/**
	 * Simple method to add the Error Widget to the Subsystem
	 * - this ensures that the Error Widget is always available to the project
	 *
	 * @param NewErrorWidget - New Error Widget to add to the Subsystem - by design there is only ever one error widget
	 */
	UFUNCTION(BlueprintCallable, Category = "Error Widget")
	void AddErrorWidget(UMoveableErrorWidget* NewErrorWidget);

	/**
	 * Method to get the Error Widget
	 *
	 * @return Error Widget
	 */
	UFUNCTION(BlueprintCallable, Category = "Error Widget")
	UMoveableErrorWidget* GetErrorWidget() const;

	/**
	 * Display Error Widget with the new title and message
	 *
	 * @param Title - Title of the Error
	 * @param Message - Message of the Error
	 */
	UFUNCTION(BlueprintCallable, Category = "Error Widget")
	void DisplayErrorWidget(const FText Title, const FText Message);

	/**
	 * Add the loading widget to the subsystem
	 *
	 * @param NewLoadingWidget - New Loading Widget to add to the Subsystem - by design there is only ever one loading widget
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget")
	void AddLoadingWidget(ULoadingNotifyWidget* NewLoadingWidget);

	/**
	 * Get the Loading Widget
	 *
	 * @return Loading Widget
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget")
	ULoadingNotifyWidget* GetLoadingWidget() const;
	
	/**
	 * Update Load percent value used for binding with external delegates
	 *
	 * @param[float] NewLoadPercent - New Load Percent Value
	 */
	UFUNCTION()
	void UpdateLoadPercent(float NewLoadPercent);

	/**
	 * Update the loading text and title for the loading widget
	 *
	 * @param[FString] NewLoadingText - New Loading Text
	 * @param[FString] NewLoadingTitle - New Loading Title
	 */
	UFUNCTION()
	void SetLoadingTextAndTitle(FString NewLoadingText, FString NewLoadingTitle);

private:
	
	/**
	 * Internal method to get a center position of the screen for the specified widget
	 *
	 * @param Widget - Widget to get the center position for
	 * @return Center Position of the Widget
	 */
	FVector2D GetCenterPosition(UUserWidget* Widget);

	/**
	 * Internal method to get a center position of the screen for the specified widget
	 *
	 * @param WidgetPanel - Widget panel to get the center position for
	 * @return Center Position of the Widget
	 */
	FVector2D GetCenterPosForWidgetPanel(class UPanelWidget* WidgetPanel);

#pragma endregion METHODS
	
#pragma region PROPERTIES
public:
	// Error Widget
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Error Widget")
	UMoveableErrorWidget* ErrorWidget;

	// Loading Notify Widget
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LoadingNotifyWidget")
	ULoadingNotifyWidget* LoadingNotifyWidget;
#pragma endregion PROPERTIES
};
