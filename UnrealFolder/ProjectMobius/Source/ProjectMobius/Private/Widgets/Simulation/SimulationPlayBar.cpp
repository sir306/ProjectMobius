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
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/SubSystems/MassEntitySpawnSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"

namespace
{
	/**
	 * Stops the playbar clock from "bouncing" horizontally without changing digit spacing.
	 *
	 * Root cause (confirmed from a frame-by-frame capture): the clock string has a constant character
	 * count (MM:SS.mm / HH:MM:SS.mm, all fields zero-padded) but the UI font has proportional digits
	 * ('1' narrower than '8'/'0'), so the total width changes every tick. With centre justification the
	 * whole string re-centres each frame, translating left/right — the visible bounce.
	 *
	 * Fix (keeps the current font and its natural, tight kerning — no extra spacing between digits):
	 *   1) Left-justify, so the leading significant digits (minutes/seconds) are anchored and the string
	 *      no longer translates as a whole.
	 *   2) Reserve a constant width (the widest possible string for the current format, font-measured)
	 *      via SetMinDesiredWidth, so the box itself can't resize / re-centre within its parent. The
	 *      reserved slack is invisible trailing space on the right — it does NOT separate the digits.
	 * Purely visual — no timing/playback behaviour changes.
	 *
	 * @param TextBlock     The current-time text block (no-op if null).
	 * @param bHoursNeeded  True when the format includes an hours field (HH:MM:SS.mm).
	 */
	void ReserveStableTimeWidth(UTextBlock* TextBlock, const bool bHoursNeeded)
	{
		if (!TextBlock)
		{
			return;
		}

		// Anchor left so the significant digits stay put instead of the whole string re-centring.
		TextBlock->SetJustification(ETextJustify::Left);

		// Width reservation needs the font measure service; skip in headless/cook contexts where there
		// is no renderer (the left-justify above already removes the dominant bounce on its own).
		if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
		{
			return;
		}

		const TSharedRef<FSlateFontMeasure> FontMeasure =
			FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FSlateFontInfo Font = TextBlock->GetFont();

		// Widest single digit in this font (covers non-tabular fonts where '1' is narrower than '8').
		double WidestDigit = 0.0;
		for (TCHAR Digit = TEXT('0'); Digit <= TEXT('9'); ++Digit)
		{
			WidestDigit = FMath::Max(WidestDigit, FontMeasure->Measure(FString::Chr(Digit), Font).X);
		}
		const double ColonWidth = FontMeasure->Measure(TEXT(":"), Font).X;
		const double DotWidth   = FontMeasure->Measure(TEXT("."), Font).X;

		// Format is "MM:SS.mm" (+ "HH:" when hours are shown). Reserve up to 3 hour digits so the width
		// stays constant even if the hour field grows from 2 to 3 digits during long playback.
		int32 DigitCount = 6; // MM + SS + mm
		int32 ColonCount = 1; // ':' between MM and SS  (the '.' before mm is counted separately)
		if (bHoursNeeded)
		{
			DigitCount += 3;  // up to HHH
			ColonCount += 1;  // extra ':' between hours and minutes
		}

		const double ReservedWidth = (DigitCount * WidestDigit) + (ColonCount * ColonWidth) + DotWidth;
		TextBlock->SetMinDesiredWidth(FMath::CeilToFloat(static_cast<float>(ReservedWidth)));
	}
}

FOnSimulationPlayBarLifecycle& USimulationPlayBar::OnPlayBarConstructed()
{
        static FOnSimulationPlayBarLifecycle Delegate;
        return Delegate;
}

FOnSimulationPlayBarLifecycle& USimulationPlayBar::OnPlayBarDestructed()
{
        static FOnSimulationPlayBarLifecycle Delegate;
        return Delegate;
}

void USimulationPlayBar::NativeConstruct()
{
        Super::NativeConstruct();

        // Configure the number format
        NumberFormat.MinimumIntegralDigits = 2;
        NumberFormat.MaximumIntegralDigits = 3;

	

	// Bind the time dilation subsystem to the widget
	SetTimeDilationSubsystem();

	// Set Current & Max time text to 0 (Q41b: moved AFTER SetTimeDilationSubsystem so the boot
	// value formats through FormatSimTime's zero-pad instead of the null-subsystem "--:--:--.--"
	// fallback). Subsystem may still be updating, so zero is the correct initial display.
	UpdateCurrentTime(0.0f);
	UpdateMaxTime(0.0f);
	
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
			// Q51/C4: keep the accent scrub fill in step with live dragging.
			PlaybackSlider->OnValueChanged.AddDynamic(this, &USimulationPlayBar::OnPlaybackSliderValueChanged);
		}

	}

	// Initialise the scrub fill to the current slider state.
	UpdateScrubFill();

	// Get the project mobius game instance and bind the loading state to the play button
        if (UWorld* World = GetWorld())
        {
                if (UProjectMobiusGameInstance* ProjectMobiusGameInstance = Cast<UProjectMobiusGameInstance, UGameInstance>(World->GetGameInstance()))
                {
                        // Bind the loading state to the play button
                        ProjectMobiusGameInstance->OnDataLoading.AddDynamic(this, &USimulationPlayBar::SetPlayButtonEnabled);
                        ProjectMobiusGameInstance->OnPedestrianVectorFileUpdated.AddDynamic(this, &USimulationPlayBar::FileChanging);
                }
        }

        OnPlayBarConstructed().Broadcast(this);
}

void USimulationPlayBar::SynchronizeProperties()
{
        Super::SynchronizeProperties();

	// Assign the style assets
        AssignStyleAssets();

}

void USimulationPlayBar::NativeDestruct()
{
        Super::NativeDestruct();

        OnPlayBarDestructed().Broadcast(this);

        // Unbind delegates from the time dilation subsystem
        if (TimeDilationSubsystem)
        {
                TimeDilationSubsystem->OnNewCurrentTime.RemoveDynamic(this, &USimulationPlayBar::UpdateCurrentTime);
                TimeDilationSubsystem->OnNewMaxTime.RemoveDynamic(this, &USimulationPlayBar::UpdateMaxTime);
                TimeDilationSubsystem->OnNewTimeBetweenData.RemoveDynamic(this, &USimulationPlayBar::UpdatePlayBarStepSize);
        }

        // Unbind button delegates
        if (PlayPauseButton)
        {
                PlayPauseButton->OnClicked.RemoveDynamic(this, &USimulationPlayBar::OnPlayPauseButtonClicked);
        }

        if (PlaybackSlider)
        {
                PlaybackSlider->OnMouseCaptureBegin.RemoveDynamic(this, &USimulationPlayBar::OnPlaybackSliderCaptureBegin);
                PlaybackSlider->OnMouseCaptureEnd.RemoveDynamic(this, &USimulationPlayBar::OnPlaybackSliderCaptureEnd);
                PlaybackSlider->OnValueChanged.RemoveDynamic(this, &USimulationPlayBar::OnPlaybackSliderValueChanged);
        }

        // Unbind game instance delegates
        if (UWorld* World = GetWorld())
        {
                if (UProjectMobiusGameInstance* ProjectMobiusGameInstance = Cast<UProjectMobiusGameInstance>(World->GetGameInstance()))
                {
                        ProjectMobiusGameInstance->OnDataLoading.RemoveDynamic(this, &USimulationPlayBar::SetPlayButtonEnabled);
                        ProjectMobiusGameInstance->OnPedestrianVectorFileUpdated.RemoveDynamic(this, &USimulationPlayBar::FileChanging);
                }
        }
}

void USimulationPlayBar::HandleMoveableWindowActivityChanged(bool bIsActive)
{
        if (!TimeDilationSubsystem)
        {
                SetTimeDilationSubsystem();
        }

        if (!TimeDilationSubsystem)
        {
                return;
        }

        if (bIsActive)
        {
                if (SimulationPaused == 0)
                {
                        bPausedForWindowActivity = true;
                        SimulationPaused = 1;
                        TimeDilationSubsystem->SetSimulationPaused(true);
                        SetPlayButtonStyle();
                }
                else
                {
                        bPausedForWindowActivity = false;
                }
        }
        else if (bPausedForWindowActivity)
        {
                bPausedForWindowActivity = false;
                SimulationPaused = 0;
                TimeDilationSubsystem->SetSimulationPaused(false);
                SetPlayButtonStyle();
        }
}

void USimulationPlayBar::StartSimulation()
{
	// check if the world is valid
	if(!GetWorld())
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Simulation Error"),
				FText::FromString("World not available"),
				FText::FromString("Cannot start the simulation without a valid world."),
				FText::FromString("SimulationPlayBar"));
		}
		return; // prevent unbinding and binding of delegates if the world is not valid
	}

	// get the MassEntitySubsystem from the world
	UMassEntitySpawnSubsystem* MassEntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>();

	// check if the MassEntitySubsystem is valid
	if(!MassEntitySubsystem)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Simulation Error"),
				FText::FromString("Spawn subsystem missing"),
				FText::FromString("Mass entity spawn subsystem is not available."),
				FText::FromString("SimulationPlayBar"));
		}
		return; // prevent unbinding and binding of delegates if the MassEntitySubsystem is not valid
	}

	//MassEntitySubsystem->SpawnMaxPedestrians();

	// call this so it gets the time dilation subsystem unpaused for the first call
	OnPlayPauseButtonClicked();

	// Unbind this method from the button click event
	//PlayPauseButton->OnClicked.RemoveDynamic(this, &USimulationPlayBar::StartSimulation);

	// Bind the new play pause button click event
	//PlayPauseButton->OnClicked.AddDynamic(this, &USimulationPlayBar::OnPlayPauseButtonClicked);
}

void USimulationPlayBar::OnPlayPauseButtonClicked()
{
	if(SimulationPaused == 1)
	{
		// Set the simulation paused to false
		SimulationPaused = 0;

		// Set the previous pause state to false
		PreviouslyPaused = 0;

		// Unpause the simulation
		TimeDilationSubsystem->SetSimulationPaused(false);
	}
	else
	{
		// Set the simulation paused to true
		SimulationPaused = 1;

		// Set the previous pause state to true
		PreviouslyPaused = 1;
		
		// Pause the simulation
		TimeDilationSubsystem->SetSimulationPaused(true);
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
	// 1) Early out if time hasn’t changed enough to matter visually.
        // Adjust epsilon to your sim’s granularity. If your time step is, say, 0.01s,
        // an epsilon of 1e-4 is more than enough.
        constexpr float Epsilon = 1e-4f;
        if (FMath::IsNearlyEqual(NewCurrentTime, LastDisplayedCurrentTime, Epsilon))
        {
            // Don’t touch text or slider – no visible change
            return;
        }
    
        LastDisplayedCurrentTime = NewCurrentTime;
    
        // 2) Update the text only when needed
        if (CurrentTimeTextBlock)
        {
            const FText NewText = FormatTime(NewCurrentTime);
    
            // If you want to be extra strict and avoid touching Slate when the string is identical:
            if (!NewText.EqualTo(LastCurrentTimeText))
            {
                LastCurrentTimeText = NewText;
                CurrentTimeTextBlock->SetText(NewText);
            }
    
            // TODO: look into UWidgetUtilHelpers for this functionality and see if its better or useable
            // UWidgetUtilHelpers::UpdateTextIfChanged(CurrentTimeTextBlock, NewText);
        }
    
        // 3) Update the slider only when the value actually changed
        if (PlaybackSlider && !SimulationPaused)
        {
            const float CurrentSliderValue = PlaybackSlider->GetValue();
            if (!FMath::IsNearlyEqual(CurrentSliderValue, NewCurrentTime, Epsilon))
            {
                PlaybackSlider->SetValue(NewCurrentTime);
            }
        }

        // Q51/C4: keep the accent scrub fill in step with playback.
        UpdateScrubFill();
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

	// Keep the current-time clock a constant width and left-anchored so it doesn't bounce as digits
	// change. Done here because the chosen format (and thus the widest possible string) depends on
	// HoursNeeded, set just above. Also covers the initial UpdateMaxTime(0.0f) call in NativeConstruct.
	ReserveStableTimeWidth(CurrentTimeTextBlock, HoursNeeded != 0);

	// Q51/C4: max changed -> the scrub-fill fraction (value/max) must be recomputed.
	UpdateScrubFill();
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
    if (TimeDilationSubsystem)
    {
        return TimeDilationSubsystem->FormatSimTime(TotalTime, HoursNeeded);
    }
	// Return a default value if the subsystem is not valid - or if no data is available
	// (Q41a) match the FormatSimTime zero-padded format branch instead of a dash train.
    return FText::FromString(HoursNeeded != 0 ? TEXT("00:00:00.00") : TEXT("00:00.00"));
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

	// set previously paused to true
	PreviouslyPaused = 1;

	// Unpause the simulation
	TimeDilationSubsystem->SetSimulationPaused(true);
	
	SetPlayButtonStyle();
}

void USimulationPlayBar::SetPlayButtonStyle() const
{
	// Check the PlayPauseButton and the SlatePlayButtonStyle
	if (!PlayPauseButton && !SlatePlayButtonStyle && !SlatePauseButtonStyle)
	{
		return;
	}
	if (!PlayPauseButton->GetIsEnabled())
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

void USimulationPlayBar::UserSelectingAgentFromMousePosition(bool bIsSelecting)
{
	// bIsSelecting is true if the user is selecting an agent, false if they are not selecting an agent
	if (bIsSelecting)
	{
		// if simulation is paused then we can select an agent otherwise we need to pause the simulation
		if (!SimulationPaused)
		{
			// Pause the simulation
			TimeDilationSubsystem->SetSimulationPaused(true);
	
			// Check if the simulation is already paused
			if(SimulationPaused == 1)
			{
				PreviouslyPaused = 1;
			}
			else
			{
				PreviouslyPaused = 0;
		
				// Set the simulation paused to true
				SimulationPaused = 1;
			}
		}
		
		// Disable the play button and playbar
		PlayPauseButton->SetIsEnabled(false);
		PlaybackSlider->SetIsEnabled(false);
	}
	else
	{
		if(PreviouslyPaused == 0)
		{
			// Set the simulation paused to false
			SimulationPaused = 0;
			TimeDilationSubsystem->SetSimulationPaused(false); // Unpause the simulation
		}
		// Enable the play button and playbar
		PlayPauseButton->SetIsEnabled(true);
		PlaybackSlider->SetIsEnabled(true);
	}
	//SetPlayButtonStyle();
}

void USimulationPlayBar::PauseSimulationAndUpdateTimeBegin()
{
	// pause the actual game
	UGameplayStatics::SetGamePaused(GetWorld(), true);
	
	// Pause the simulation
	TimeDilationSubsystem->SetSimulationPaused(true);

	// Unsubscribe from update current time
	TimeDilationSubsystem->OnNewCurrentTime.RemoveDynamic(this, &USimulationPlayBar::UpdateCurrentTime);
	
	// Check if the simulation is already paused
	if(SimulationPaused == 1)
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
	if(PreviouslyPaused == 0)
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

	// Q51/C4: a scrub/step just committed a new slider value -> update the accent fill.
	UpdateScrubFill();
}

void USimulationPlayBar::UpdateScrubFill() const
{
	if (!ScrubFillBar || !PlaybackSlider)
	{
		return;
	}

	const float MaxValue = PlaybackSlider->GetMaxValue();
	const float Percent = (MaxValue > KINDA_SMALL_NUMBER)
		? (PlaybackSlider->GetValue() / MaxValue)
		: 0.0f;
	ScrubFillBar->SetPercent(FMath::Clamp(Percent, 0.0f, 1.0f));
}

void USimulationPlayBar::OnPlaybackSliderValueChanged(float /*NewValue*/)
{
	// Live drag: OnValueChanged fires on user interaction (programmatic SetValue does not), so this
	// covers scrubbing that occurs while the sim clock delegate is temporarily unsubscribed.
	UpdateScrubFill();
}
