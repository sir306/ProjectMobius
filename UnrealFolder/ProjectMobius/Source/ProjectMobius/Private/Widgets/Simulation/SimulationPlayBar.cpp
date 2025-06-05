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

#include "Widgets/Simulation/SimulationPlayBar.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/SubSystems/MassEntitySpawnSubsystem.h"

void USimulationPlayBar::NativeConstruct()
{
	Super::NativeConstruct();

	// Configure the number format
	NumberFormat->MinimumIntegralDigits = 2;
	NumberFormat->MaximumIntegralDigits = 3;

	

	// Set Current & Max time text to 0 as likely the subsystem is not valid or not updated yet
	UpdateCurrentTime(0.0f);
	UpdateMaxTime(0.0f);
	
	// Bind the time dilation subsystem to the widget
	SetTimeDilationSubsystem();
	
	// check if the subsystem is valid and if so bind to the delegates
	if (TimeDilationSubsystem)
	{
		// Bind the current time delegate
		TimeDilationSubsystem->OnNewCurrentTime.AddDynamic(this, &USimulationPlayBar::UpdateCurrentTime); // this could be potentially taxing on the system

		// Bind the max time delegate
		TimeDilationSubsystem->OnNewMaxTime.AddDynamic(this, &USimulationPlayBar::UpdateMaxTime);

		// Bind the time step delegate
		TimeDilationSubsystem->OnNewTimeBetweenData.AddDynamic(this, &USimulationPlayBar::UpdatePlayBarStepSize);
	}

	// Bind the play pause button
	if (PlayPauseButton)
	{
		// Bind the button click event if not in design time as we don't want to spawn agents in the editor and crash
		if(!IsDesignTime())
		{
			//PlayPauseButton->OnClicked.AddDynamic(this, &USimulationPlayBar::StartSimulation);

			PlayPauseButton->OnClicked.AddDynamic(this, &USimulationPlayBar::OnPlayPauseButtonClicked);
		}
	}

	// Bind the slider on capture event to update the current time
	if (PlaybackSlider)
	{
		// Bind the button click event if not in design time as we don't want to spawn/pause/update agents in the editor and crash
		if(!IsDesignTime())
		{
			PlaybackSlider->OnMouseCaptureBegin.AddDynamic(this, &USimulationPlayBar::OnPlaybackSliderCaptureBegin);
			PlaybackSlider->OnMouseCaptureEnd.AddDynamic(this, &USimulationPlayBar::OnPlaybackSliderCaptureEnd);
		}
		
	}

	// Get the project mobius game instance and bind the loading state to the play button
	if (UProjectMobiusGameInstance* ProjectMobiusGameInstance = Cast<UProjectMobiusGameInstance, UGameInstance>(GetWorld()->GetGameInstance()))
	{
		// Bind the loading state to the play button
		ProjectMobiusGameInstance->OnDataLoading.AddDynamic(this, &USimulationPlayBar::SetPlayButtonEnabled);
		ProjectMobiusGameInstance->OnPedestrianVectorFileUpdated.AddDynamic(this, &USimulationPlayBar::FileChanging);
	}
}

void USimulationPlayBar::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

}

void USimulationPlayBar::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// Assign the style assets
	AssignStyleAssets();
	
}

void USimulationPlayBar::StartSimulation()
{
	// check if the world is valid
	if(!GetWorld())
	{
		return; // prevent unbinding and binding of delegates if the world is not valid
	}

	// get the MassEntitySubsystem from the world
	UMassEntitySpawnSubsystem* MassEntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>();

	// check if the MassEntitySubsystem is valid
	if(!MassEntitySubsystem)
	{
		// TODO: add Error message 
		return; // prevent unbinding and binding of delegates if the MassEntitySubsystem is not valid
	}

	MassEntitySubsystem->SpawnMaxPedestrians();

	// call this so it gets the time dilation subsystem unpaused for the first call
	OnPlayPauseButtonClicked();

	// Unbind this method from the button click event
	//PlayPauseButton->OnClicked.RemoveDynamic(this, &USimulationPlayBar::StartSimulation);

	// Bind the new play pause button click event
	//PlayPauseButton->OnClicked.AddDynamic(this, &USimulationPlayBar::OnPlayPauseButtonClicked);
}

void USimulationPlayBar::OnPlayPauseButtonClicked()
{
	if(SimulationPaused)
	{
		// Set the simulation paused to false
		SimulationPaused = 0;

		// Unpause the simulation
		TimeDilationSubsystem->bIsPaused = false;
	}
	else
	{
		// Set the simulation paused to true
		SimulationPaused = 1;
		
		// Pause the simulation
		TimeDilationSubsystem->bIsPaused = true;
	}

	// update the play button style
	SetPlayButtonStyle();
}

void USimulationPlayBar::OnPlaybackSliderCaptureBegin()
{
	PauseSimulationAndUpdateTimeBegin();
}

//TODO: THIS NEEDS TO BE FIXED - currently only works when sim is paused first then dragged and unpaused not live adjust(somewhere is updating to previous value after new value)
void USimulationPlayBar::OnPlaybackSliderCaptureEnd()
{
	PauseSimulationAndUpdateTimeEnd();
}

void USimulationPlayBar::IncrementPlayback(int32 IncrementAmount)
{
	AdjustPlaybackSteps(IncrementAmount);
}

void USimulationPlayBar::DecrementPlayback(int32 DecrementAmount)
{
	AdjustPlaybackSteps(-DecrementAmount);
}

void USimulationPlayBar::AdjustPlaybackSteps(int32 NumSteps)
{
	//TODO: this method now works but needs to be cleaned up and simplified ChatGPT is not your friend!
	// prepare update and get current values
	PauseSimulationAndUpdateTimeBegin();
	
	const float StepSize = PlaybackSlider->GetStepSize();
	const float Current  = PlaybackSlider->GetValue();

	int32 MaxIndex = FMath::RoundToInt( PlaybackSlider->GetMaxValue() / StepSize );

	// Round current step to the nearest step size - TODO: this 
	int32 CurrentStepIndex = FMath::RoundToInt( Current/ StepSize );

	// create updown step index so we can check if we are going up or down and make sure if we between steps it won't round up or down and then skip
	int32 DoubleUpDownIndex = FMath::FloorToInt(Current / StepSize);

	bool bMinusCeil = false;
	bool bArithmeticFine = false;
	if (FMath::FloorToInt(Current / StepSize) == FMath::CeilToInt(Current / StepSize))
	{
		bMinusCeil = true;
		bArithmeticFine = true;
	}
	else if(DoubleUpDownIndex == CurrentStepIndex && NumSteps < 0)
	{

		DoubleUpDownIndex = FMath::CeilToInt(Current / StepSize);
		bMinusCeil = true;
		bArithmeticFine = false;
	
	}
	else if(DoubleUpDownIndex != CurrentStepIndex && NumSteps > 0)
	{
		bMinusCeil = false;
	}
	else if(DoubleUpDownIndex != CurrentStepIndex && NumSteps < 0)
	{
		//DoubleUpDownIndex = FMath::CeilToInt(Current / StepSize);
		bMinusCeil = true;
		bArithmeticFine = false;
	}
	else if(DoubleUpDownIndex == CurrentStepIndex && NumSteps > 0)
	{
		//DoubleUpDownIndex = FMath::CeilToInt(Current / StepSize);
		//bMinusCeil = true;
		bArithmeticFine = true;
	}

	// add steps
	int32 NewIndex = FMath::Clamp(CurrentStepIndex + NumSteps, 0, MaxIndex);

	// did it double skip?
	if ((DoubleUpDownIndex + NumSteps) == CurrentStepIndex && !bMinusCeil && !bArithmeticFine)
	{
		NewIndex = CurrentStepIndex;
	}
	else if ((DoubleUpDownIndex + (NumSteps * 2)) == NewIndex && bMinusCeil && !bArithmeticFine)
	{
		NewIndex = CurrentStepIndex;
	}
	else if (bMinusCeil && !bArithmeticFine && DoubleUpDownIndex == CurrentStepIndex)
	{
		NewIndex = CurrentStepIndex;
	}

	// rebuild value
	float NewValue = NewIndex * StepSize;

	// log if new value is same as current value
	//UE_LOG(LogTemp, Warning, TEXT("%s"), Round3DP(NewValue) == Round3DP(PlaybackSlider->GetValue())? TEXT("New Value is same as current value") : TEXT("New Value is different from current value"));
	
	if (Round3DP(NewValue) == Round3DP(PlaybackSlider->GetValue()) && NewValue != 0.0f && NumSteps < 0)
	{
		NewValue += StepSize * NumSteps;
	}

	// round to 3dp
	NewValue = Round3DP(NewValue);

	// set value and update
	PlaybackSlider->SetValue(NewValue);
	PauseSimulationAndUpdateTimeEnd();
}

int32 USimulationPlayBar::GetMaxTimeStep()
{
	if (TimeDilationSubsystem)
	{
		// Get the max time step from the simulation
		return TimeDilationSubsystem->MaxTimeSteps;
	}

	return 0; // default return
}

void USimulationPlayBar::SetTimeDilationSubsystem()
{
	// check if the world is valid
	if (GetWorld() == nullptr)
	{
		return;
	}
	
	// Get the TimeDilationSubSystem from the world
	TimeDilationSubsystem = GetWorld()->GetSubsystem<UTimeDilationSubSystem>();
}

void USimulationPlayBar::UpdateCurrentTime(float NewCurrentTime)
{
	// Check if text block valid
	if (CurrentTimeTextBlock)
	{
		// Set the text block to display the new time step
		CurrentTimeTextBlock->SetText(FormatTime(NewCurrentTime));
	}
	
	// Update the current value of slider if valid and not paused
	if (PlaybackSlider && !SimulationPaused)// the check for pause is if the user is dragging the slider
	{
		// Set the current value of the slider
		PlaybackSlider->SetValue(NewCurrentTime);
	}
}

void USimulationPlayBar::UpdateMaxTime(float NewMaxTime)
{
	// Check if text block valid
	if (MaxTimeTextBlock)
	{
		int32 Hours = FMath::FloorToInt32(NewMaxTime / 3600);

		// log total time
		//UE_LOG(LogTemp, Warning, TEXT("Total Time: %f"), NewMaxTime);
		// log milliseconds
		//UE_LOG(LogTemp, Warning, TEXT("Milliseconds: %f"), fmod(NewMaxTime, 1) * 1000);
		
		// See if hours is needed
		if (Hours > 0)
		{
			HoursNeeded = 1;
		}
		else
		{
			HoursNeeded = 0;
		}
		
		// Set the text block to display the new time step
		MaxTimeTextBlock->SetText(FormatTime(NewMaxTime));
	}

	// Update slider values if valid
	if(PlaybackSlider)
	{
		// Set the max value of the slider
		PlaybackSlider->SetMaxValue(NewMaxTime);
	}
}

void USimulationPlayBar::AssignStyleAssets() const
{
	// We need to check if the widget components are valid first before we assign the style assets

	// Check the background image
	if (BackgroundImage)
	{
		// Check the style asset
		if(SlateBackgroundStyle)
		{
			// Assign the background image style asset
			BackgroundImage->SetBrushFromAsset(SlateBackgroundStyle);
		}
	}
	// update the play button style
	SetPlayButtonStyle();
}

FText USimulationPlayBar::FormatTime(float TotalTime) const
{
	// Format args for text time
	FFormatNamedArguments TimeFormatArgs;

	// need min, sec and ms by default
	FText Minute = FText::AsNumber(FMath::FloorToInt32(fmod(TotalTime, 3600) / 60), NumberFormat);
	FText Second = FText::AsNumber(FMath::FloorToInt32(fmod(TotalTime, 60)), NumberFormat);
	FText Millisecond = FText::AsNumber(FMath::FloorToInt32(fmod(TotalTime, 1) * 100), NumberFormat); // technically milliseconds is 1000 but we are rounding to 2dp

	// Set the key values for the TimeFormatArgs
	TimeFormatArgs.Add("Minute", Minute);
	TimeFormatArgs.Add("Second", Second);
	TimeFormatArgs.Add("Millisecond", Millisecond);
	
	if(HoursNeeded)
	{
		FText Hour = FText::AsNumber(FMath::FloorToInt32(TotalTime / 3600), NumberFormat);

		// Set the key values for the TimeFormatArgs
		TimeFormatArgs.Add("Hour", Hour);
	
		return FText::Format(NSLOCTEXT("ElapsedTimeSpace", "ElapseTimeFormat", "{Hour}:{Minute}:{Second}.{Millisecond}"), TimeFormatArgs);
	}
	else
	{
		return FText::Format(NSLOCTEXT("ElapsedTimeSpace", "ElapseTimeFormat", "{Minute}:{Second}.{Millisecond}"), TimeFormatArgs);
	}
	
	
}

void USimulationPlayBar::SetPlayButtonEnabled(const bool bLoadingState)
{
	// Set the button to be enabled or disabled
	PlayPauseButton->SetIsEnabled(!bLoadingState);
}

void USimulationPlayBar::FileChanging()
{
	// Set the simulation paused to false
	SimulationPaused = 1;

	// Unpause the simulation
	TimeDilationSubsystem->bIsPaused = true;
	
	SetPlayButtonStyle();
}

void USimulationPlayBar::SetPlayButtonStyle() const
{
	// Check the PlayPauseButton and the SlatePlayButtonStyle
	if (!PlayPauseButton && !SlatePlayButtonStyle && !SlatePauseButtonStyle)
	{
		return;
	}

	// is the simulation paused
	if (SimulationPaused)
	{
		// Assign the play button style asset
		PlayPauseButton->SetStyle(*SlatePlayButtonStyle->GetStyle<FButtonStyle>());
	}
	else
	{
		// Assign the pause button style asset
		PlayPauseButton->SetStyle(*SlatePauseButtonStyle->GetStyle<FButtonStyle>());
	}
}

void USimulationPlayBar::UpdatePlayBarStepSize(float NewTimeBetweenData)
{
	// if play bar is valid
	if (PlaybackSlider)
	{
		// Set the step size of the slider
		PlaybackSlider->SetStepSize(NewTimeBetweenData);
	}
}

void USimulationPlayBar::PauseSimulationAndUpdateTimeBegin()
{
	// pause the actual game
	UGameplayStatics::SetGamePaused(GetWorld(), true);
	
	// Pause the simulation
	TimeDilationSubsystem->bIsPaused = true;

	// Unsubscribe from update current time
	TimeDilationSubsystem->OnNewCurrentTime.RemoveDynamic(this, &USimulationPlayBar::UpdateCurrentTime);
	
	// Check if the simulation is already paused
	if(SimulationPaused)
	{
		PreviouslyPaused = 1;
	}
	else
	{
		PreviouslyPaused = 0;
		
		// Set the simulation paused to true
		SimulationPaused = 1;
	}
	
	
	// update here the simulation time to the new time
	//UE_LOG(LogTemp, Warning, TEXT("Slider Value: %f"), PlaybackSlider->GetValue());
}

void USimulationPlayBar::PauseSimulationAndUpdateTimeEnd()
{
	// update here the simulation time to the new time
	//UE_LOG(LogTemp, Warning, TEXT("Slider Value: %f"), PlaybackSlider->GetValue());
	
	TimeDilationSubsystem->OverrideCurrentTime(PlaybackSlider->GetValue(), PreviouslyPaused);
	
	// Check if the simulation is already paused and un pause if it was not paused before
	if(!PreviouslyPaused)
	{
		// Set the simulation paused to false
		SimulationPaused = 0;
	}

	// Check if text block valid
	if (CurrentTimeTextBlock)
	{
		// Set the text block to display the new time step
		CurrentTimeTextBlock->SetText(FormatTime(PlaybackSlider->GetValue()));
	}

	// Subscribe to the update current time
	TimeDilationSubsystem->OnNewCurrentTime.AddDynamic(this, &USimulationPlayBar::UpdateCurrentTime);
	UGameplayStatics::SetGamePaused(GetWorld(), false);
}
