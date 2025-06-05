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
#include "MassAI/SubSystems/TimeDilationSubSystem.h"
#include "SimulationPlayBar.generated.h"

class USlateBrushAsset;
class UButton;
class USlider;
class UImage;
class UTextBlock;
/**
 * 
 */
UCLASS(Blueprintable)
class PROJECTMOBIUS_API USimulationPlayBar : public UUserWidget
{
	GENERATED_BODY()

#pragma region METHODS
public:
	
	// Constructor 
	virtual void NativeConstruct() override;

	// Tick Method for in C++ for the widget
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Synchronize
	virtual void SynchronizeProperties() override;

protected:
	/**
	 * Method that starts the simulation and calls the initial spawning of agents
	 */
	UFUNCTION()
	void StartSimulation();
	
	/**
	 * Method to call when the PlayPauseButton is clicked -- this will play or pause the simulation
	 */
	UFUNCTION(BlueprintCallable, Category="SimulationPlayBar|Methods")
	void OnPlayPauseButtonClicked();

	/**
	 * Method to call when the slider is clicked -- this will pause the simulation
	 */
	UFUNCTION(Category="SimulationPlayBar|Methods")
	void OnPlaybackSliderCaptureBegin();

	/**
	 * Once a user releases the slider this will update the simulation time to the new time
	 */
	UFUNCTION(Category="SimulationPlayBar|Methods")
	void OnPlaybackSliderCaptureEnd();

	/**
	 * Increment the current playback by 0.1 seconds
	 *
	 * @param[int32] IncrementAmount Defaults to 1, this value is used to specify the number of steps to increment by
	 */
	UFUNCTION(BlueprintCallable, Category="SimulationPlayBar|Methods")
	void IncrementPlayback(int32 IncrementAmount = 1);
	
	 /**
	 * Decrement the current playback by 0.1 seconds - TODO: see whether its better to do by a timestep or by a fixed value
     *
	 * @param[int32] DecrementAmount Defaults to 1, this value is used to specify the number of steps to decrement by
	 */
	UFUNCTION(BlueprintCallable, Category="SimulationPlayBar|Methods")
	void DecrementPlayback(int32 DecrementAmount = 1);

	/**
	 * Helper function to ensure the next increment or decrement goes to the next valid increment value
	 *
	 * @param[int32] NumSteps The number of steps to increment or decrement by
	 */
	void AdjustPlaybackSteps(int32 NumSteps);
	
	/**
	 * Gets the max time step from the simulation and assigns it to the max time step text block
	 *
	 * @return int32 The max time step from the simulation
	 */
	UFUNCTION(BlueprintCallable, Category="SimulationPlayBar|Setup")
	int32 GetMaxTimeStep();

	/**
	 * Get the simulation Subsystem and assign it to the simulation subsystem ptr
	 */
	UFUNCTION(BlueprintCallable, Category="SimulationPlayBar|Setup")
	void SetTimeDilationSubsystem();

	/**
	 * Update the current time text block and slider current value with the current time from the simulation
	 * This can be manually called or left to be bound to the delegate binding
	 *
	 * @param[float] NewCurrentTime The new time to update the text block and slider current value with
	 */
	UFUNCTION(BlueprintCallable, Category="SimulationPlayBar|Update")
	void UpdateCurrentTime(float NewCurrentTime);

	/**
	 * Update the max time text block and slider max value with new max time from the simulation
	 * This can be manually called or left to be bound to the delegate binding
	 *
	 * @param[float] NewMaxTime The new max time to update the text block and slider max value with
	 */
	UFUNCTION(BlueprintCallable, Category="SimulationPlayBar|Update")
	void UpdateMaxTime(float NewMaxTime);

	/**
	 * Assigns the style assets to the widgets buttons and background image
	 * primarily to clean up synchronize properties and constructors
	 */
	UFUNCTION()
	void AssignStyleAssets() const;

	/**
	 * Function to create an FText from a float of total time into hh:mm:ss::ms format
	 *
	 * @param[float] TotalTime - The total time in seconds
	 * @return FText - The formatted time text
	 */
	UFUNCTION(BlueprintCallable, Category="SimulationPlayBar|TextFormatting")
	FText FormatTime(float TotalTime) const;

	/**
	 * Function is bound to the game instance to disable or enable the play button based on the loading state
	 * of simulation data
	 *
	 * @param[bool] bLoadingState - The new loading state of the simulation data
	 */
	UFUNCTION(BlueprintCallable, Category="SimulationPlayBar|Setup")
	void SetPlayButtonEnabled(const bool bLoadingState);

	/**
	 * When the file is changing we need the UI to revert to the initial state
	 */
	UFUNCTION(Category="SimulationPlayBar|Setup")
	void FileChanging();

	/**
	 * This method handles the styling of the play button so when states change the correct style is applied
	 */
	UFUNCTION(BlueprintCallable, Category="SimulationPlayBar|Design")
	void SetPlayButtonStyle() const;

	/**
	 * Update the simulation time step value - bound to the time dilation subsystem via a delegate
	 * 
	 * @param[float] NewTimeBetweenData Update the slider step size
	 */
	UFUNCTION(BlueprintCallable, Category="SimulationPlayBar|Design")
	void UpdatePlayBarStepSize(float NewTimeBetweenData);
	
private:
	void PauseSimulationAndUpdateTimeBegin();
	void PauseSimulationAndUpdateTimeEnd();

	static FORCEINLINE float Round3DP( float In )
	{
		// scale up, round, scale back down
		constexpr float Scale = 1000.0f;
		return FMath::RoundToFloat( In * Scale ) / Scale;
	}

	static FORCEINLINE float Round2DP( float In )
	{
		// scale up, round, scale back down
		constexpr float Scale = 100.0f;
		return FMath::RoundToFloat( In * Scale ) / Scale;
	}

#pragma endregion METHODS

#pragma region PROPERTIES_COMPONENTS
public:
	/** The button to play and pause the simulation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimComponents", meta = (BindWidget))
	TObjectPtr<UButton> PlayPauseButton;

	/** Slider to represent simulation playback */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimComponents", meta = (BindWidget))
	TObjectPtr<USlider> PlaybackSlider;

	/** Text block that represents the current simulation time step */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimComponents", meta = (BindWidget))
	TObjectPtr<UTextBlock> CurrentTimeTextBlock;

	/** Text block to represent the max simulation time step */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimComponents", meta = (BindWidget))
	TObjectPtr<UTextBlock> MaxTimeTextBlock;
 
	/** Background Image to improve the visuals of the UI */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimComponents", meta = (BindWidget))
	TObjectPtr<UImage> BackgroundImage;

	/** Ptr to the time dilation subsystem - this is where we can get the max time steps and current time step */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SimProperties", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTimeDilationSubSystem> TimeDilationSubsystem;

private:
	/** Value to represent if the simulation is paused or not */
	UPROPERTY()
	uint8 SimulationPaused : 1 = 1;

	/** As the slider movement may occur when already paused to prevent un pausing another variable is used to calculate this */
	UPROPERTY()
	uint8 PreviouslyPaused : 1 = 0;

	/** Value to represent if hours is needed in simulation time text or not */
	UPROPERTY()
	uint8 HoursNeeded : 1 = 0;

	// Number Format
	FNumberFormattingOptions* NumberFormat = new FNumberFormattingOptions();
	
#pragma region STYLE_PROPERTIES
public:
	/** Slate style sheet that is used to keep design uniform for the buttons */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimComponents|Design", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USlateWidgetStyleAsset> SlatePlayButtonStyle;

	/** Slate brush asset that is used to keep design uniform for the background image */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimComponents|Design", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USlateBrushAsset> SlateBackgroundStyle;

	/** Slate style sheet that is used to switch with in pause states */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimComponents|Design", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USlateWidgetStyleAsset> SlatePauseButtonStyle;
	
#pragma endregion STYLE_PROPERTIES

#pragma endregion PROPERTIES_COMPONENTS
};
