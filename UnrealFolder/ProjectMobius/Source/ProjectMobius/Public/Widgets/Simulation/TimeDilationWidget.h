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
#include "TimeDilationWidget.generated.h"

/**
 * The widget that will overlay the screen and show the current simulation time -- possibly other simulation information
 */
UCLASS()
class PROJECTMOBIUS_API UTimeDilationWidget : public UUserWidget
{
	GENERATED_BODY()

#pragma region METHODS
public:
	// Constructor 
	virtual void NativeConstruct() override;

	// Tick Method for in C++ for the widget
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

protected:
	/** Update simulation time text block with elapsed game time */
	UFUNCTION(BlueprintCallable)
	void UpdateSimulationTimeTextBlock();

private:
	/** This will set the TimeDilationSubsystem ptr if the widget is in game */
	void SetTimeDilationSubsytem();

#pragma endregion METHODS

#pragma region PROPERTIES
public:

protected:
	/**
	* Simulation total time in seconds
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimulationTime|Data", meta = (AllowPrivateAccess = "true"))
	float TotalSimulationTime = 30.0f;

	/**
	* Scale to use for simulation time dilation
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimulationTime|Data", meta = (AllowPrivate = "true"))
	float TimeDilationScale = 100.0f;

private:
#pragma region PRIVATE_CLASS_COMPONENTS
	//TODO: Change UTextblocks to RichTextBlocks for better formatting and styling
	/** Text block to show the current simulation time with scaled time dilation applied */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* CurrentSimTimeTextBlock;
	
	/** Text block to show current simulation time step */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* CurrentTimeStepTextBlock;

#pragma endregion PRIVATE_CLASS_COMPONENTS

#pragma region PRIVATE_VARIABLES
	/** Game elapsed time */
	float GameElapsedTime = 0.0f;

	/** Current simulation time */	
	float CurrentSimulationTime = 0.0f;

	/** Ptr for TimeDilationSubsystem */
	TObjectPtr<class UTimeDilationSubSystem> TimeDilationSubSystem;

#pragma endregion PRIVATE_VARIABLES

#pragma endregion PROPERTIES


#pragma region GETTERS_SETTERS
public:
#pragma region GETTERS
	/** Get the total simulation time */
	UFUNCTION(BlueprintCallable, Category = "SimulationTime|Data")
	FORCEINLINE float GetTotalSimulationTime() const { return TotalSimulationTime; }

	/** Get the time dilation scale */
	UFUNCTION(BlueprintCallable, Category = "SimulationTime|Data")
	FORCEINLINE float GetTimeDilationScale() const { return TimeDilationScale; }
#pragma endregion GETTERS

#pragma region SETTERS
	/** Set the total simulation time */
	UFUNCTION(BlueprintCallable, Category = "SimulationTime|Data")
	FORCEINLINE void SetTotalSimulationTime(float NewTotalSimulationTime) { TotalSimulationTime = NewTotalSimulationTime; }

	/** Set the time dilation scale */
	UFUNCTION(BlueprintCallable, Category = "SimulationTime|Data")
	FORCEINLINE void SetTimeDilationScale(float NewTimeDilationScale) { TimeDilationScale = NewTimeDilationScale; }
#pragma endregion SETTERS
#pragma endregion GETTERS_SETTERS
};
