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
#include "LoadingNotifyWidget.generated.h"

class UOverlay;
class UProgressBar;
class UTextBlock;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API ULoadingNotifyWidget : public UUserWidget
{
	GENERATED_BODY()

	
#pragma region METHODS
public:
#pragma region PUBLIC_METHODS
	// Native Pre Construct
	virtual void NativePreConstruct() override;

	// Native Constructor 
	virtual void NativeConstruct() override;

	// Tick Method for in C++ for the widget
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Synchronize properties with the widget
	virtual void SynchronizeProperties() override;

	/**
	 * Update Load percent value
	 *
	 * @param[float] NewLoadPercent - New Load Percent Value
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void UpdateLoadPercent(float NewLoadPercent);

	/**
	 * To simplify the loading widget we can auto hide it when the load is complete
	 */
	void IsLoadingComplete();

	/**
	 * Reset the loading percent to 0
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void ResetLoadPercent();

	/**
	 * Method to set the loading text and title for the loading widget
	 *
	 * @param[FString] NewLoadingText - New Loading Text
	 * @param[FString] NewLoadingTitle - New Loading Title
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void SetLoadingTextAndTitle(FString NewLoadingText, FString NewLoadingTitle);

	/**
	 * Method to get the current load percent
	 * 
	 * @return float - The current load percent 
	 */
	UFUNCTION()
	float GetLoadPercent() const { return LoadPercent; }
	
	
#pragma endregion PUBLIC_METHODS

#pragma endregion METHODS

#pragma region PROPERTIES
#pragma region PUBLIC_PROPERTIES
	/** Text block to show current Load Text - this will inform the user what loading action is being done */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<UTextBlock> LoadingText;

	/** Text block to show Title for this loading - Default Title to show this a loading widget */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<UTextBlock> LoadingTitle;

	/** Text block to show how much is loaded as a percent - this will inform the user what loading action is being done */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<UTextBlock> LoadedAmount;
	
	/** Percent value to display how complete a load is */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	float LoadPercent;

	/** Overlay panel for holding all the loading notify widget components on */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<UOverlay> LoadingNotifyOverlay;

	/** Progress Bar to show load progress */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<UProgressBar> LoadingBar;

	/** Ptr to the game instance to prevent recasting */
	UPROPERTY()
	TObjectPtr<class UProjectMobiusGameInstance> ProjectMobiusGameInstance;
	
#pragma endregion PUBLIC_PROPERTIES

#pragma endregion PROPERTIES
};
