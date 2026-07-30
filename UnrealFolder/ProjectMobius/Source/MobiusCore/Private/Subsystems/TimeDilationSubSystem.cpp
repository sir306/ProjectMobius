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

#include "SubSystems/TimeDilationSubSystem.h"
#include "MassEntitySubsystem.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "GameInstances/ProjectMobiusGameInstance.h"


UTimeDilationSubSystem::UTimeDilationSubSystem() : 
	TotalTime(0.0f),
	TimeBetweenSteps(0.1f), 
	TimeDialation(1.0f),
	CurrentTimeStep(0),
	MaxTimeSteps(0),
	SixtySecondTimeSteps(0.0f),
	CurrentSimHours(0),
	CurrentSimMinutes(0),
	CurrentSimSeconds(0),
	CurrentSimMilliseconds(0),
	CurrentSimTimeStr(TEXT("00:00:00")),
	AmountOfTimePaused(0.0f)
{
	// Log that the subsystem has been created
	UE_LOG(LogTemp, Warning, TEXT("Time Dilation Subsystem Created"));

	// calculate number of time steps for 60 seconds
	SixtySecondTimeSteps = FMath::FloorToInt32(60 / TimeBetweenSteps);

	// Configure the max time steps
	UpdateMaxTimeSteps(FMath::CeilToInt(TotalTime / TimeBetweenSteps)); //TODO: TimeBetweenSteps should be read from file or a setting for the user

	// Log the max time steps
	UE_LOG(LogTemp, Warning, TEXT("Max Time Steps: %d"), MaxTimeSteps);	
	
	
}

void UTimeDilationSubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Log that the subsystem has been created
	UE_LOG(LogTemp, Warning, TEXT("----- Time Dilation Subsystem Initialized -----"));

	// Add the MassSubsystem to the collection Dependency
	auto MassSubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	
	// If we have other subsystems that we depend on we can initialize them here before super
	Super::Initialize(Collection);
	
	// Get the Game Instance 
	UProjectMobiusGameInstance* GameInst = GetMobiusGameInstance(GetWorld());
	if(GameInst)
	{
		// Bind the required Game Instance Delegates
		// so we know when the time dilation scale factor has changed
		GameInst->OnTimeDilationScaleFactorChanged.AddDynamic(this, &UTimeDilationSubSystem::GetUpdatedTimeDilation);

		// When a file is changed we want to pause the simulation and reset
		GameInst->OnPedestrianVectorFileUpdated.AddDynamic(this, &UTimeDilationSubSystem::FileChanging);

		// A new B-RISK scenario gets the SAME reset, for the same reason plus one specific to tenability.
		//
		// Loading one bumps ScenarioGeneration, which makes the precomputed agent timelines stale; while
		// they rebuild, the health processor writes the no-data state to EVERY entity, including
		// DeathTimeSeconds = -1. That de-latches the failure-pose freeze in PedestrianMovementProcessor,
		// so any agent whose trajectory has already ended at the current playhead stops being rendered -
		// and the health processor skips unrendered entities, so when the rebuild lands there is nothing
		// left to re-arm DeathTimeSeconds. Projection needs rendering, rendering needs the projection: the
		// agent stays invisible with no fail marker until a scrub happens to put it back on-dataset.
		// Resetting to t=0 breaks that cycle by construction - at 0 every agent is present, so the
		// rebuild's first projected frame re-arms everything - and it matches what the agent-file path
		// already does, so both loads leave the app in the same visible state.
		GameInst->OnBRiskFileChanged.AddDynamic(this, &UTimeDilationSubSystem::FileChanging);

		// log that it has binded
		UE_LOG(LogTemp, Warning, TEXT("Time Dilation Scale Factor Changed Delegate Binded"));
	}
	
	
	// Get the Time Dilation from the ProjectMobius Game Instance
	GetUpdatedTimeDilation();
	bLastBroadcastPauseState = bIsPaused;
}

void UTimeDilationSubSystem::Deinitialize()
{
	// If we have delegates we can unbind them here before super
	UProjectMobiusGameInstance* GameInst = IProjectMobiusInterface::GetMobiusGameInstance(GetWorld());
	if (GameInst)
	{
		// UnBind the required Game Instance Delegates
		GameInst->OnTimeDilationScaleFactorChanged.RemoveDynamic(this, &UTimeDilationSubSystem::GetUpdatedTimeDilation);
		GameInst->OnPedestrianVectorFileUpdated.RemoveDynamic(this, &UTimeDilationSubSystem::FileChanging);
		GameInst->OnBRiskFileChanged.RemoveDynamic(this, &UTimeDilationSubSystem::FileChanging);
	}

	// If we have other subsystems that we depend on we can deinitialize them here after super
	Super::Deinitialize();
}

void UTimeDilationSubSystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	BroadcastPauseStateIfChanged();
	
	// Check if we are paused
	// if (!UGameplayStatics::IsGamePaused(GetWorld()) || !bIsPaused)
	// {
	// 	// Update the simulation time
	// 	UpdateSimulationTime();
	// }
	// Update the simulation time
	UpdateSimulationTime();
	BroadcastPauseStateIfChanged();
}

void UTimeDilationSubSystem::CalculateCurrentTimeStep(float SimCurrentTime)
{
	int32 TotalTimeStep = 0;
	// Time step calculation
	// TotalTimeStep += FMath::FloorToInt32(CurrentSimHours * (SixtySecondTimeSteps * 60));
	// TotalTimeStep += FMath::FloorToInt32(CurrentSimMinutes * SixtySecondTimeSteps);
	// TotalTimeStep += FMath::FloorToInt32(CurrentSimSeconds / TimeBetweenSteps);
	// TotalTimeStep += FMath::FloorToInt32(CurrentSimMilliseconds / (TimeBetweenSteps * 1000));

	TotalTimeStep = FMath::FloorToInt32(SimCurrentTime / TimeBetweenSteps);

	// Calculate the current time step and keep it within the bounds of the max time steps
	CurrentTimeStep = FMath::Clamp(TotalTimeStep, 0, MaxTimeSteps);
	
	// if we are at the max time step we need to pause the simulation time
	if (CurrentTimeStep >= MaxTimeSteps) // we only pause as if this occurs and not unpause as other system will have to handle this
	{
		// Pause the simulation
		SetSimulationPaused(true);
	}
}

void UTimeDilationSubSystem::GetUpdatedTimeDilation()
{
	// If in world get the game instance and set the time dilation
	if (GetWorld())
	{
		TimeDialation = IProjectMobiusInterface::GetMobiusGameInstanceSimulationTimeDilatationFactor(GetWorld());
	}
}

void UTimeDilationSubSystem::UpdateMaxTimeSteps(const int32 NewMaxTimeSteps)
{
	MaxTimeSteps = NewMaxTimeSteps;
}

void UTimeDilationSubSystem::UpdateTotalTime(float NewTotalTime)
{
	// Set the new total time
	TotalTime = NewTotalTime;
	
	// Broadcast the new max time
	OnNewMaxTime.Broadcast(TotalTime);

	// Reconfigure the max time steps
	UpdateMaxTimeSteps(FMath::CeilToInt32(TotalTime / TimeBetweenSteps)); //TODO: TimeBetweenSteps should be read from file or a setting for the user

}

void UTimeDilationSubSystem::OverrideCurrentTime(float NewSimulationTime, const uint8 PreviouslyPaused)
{
	// Pause the simulation regardless of the previous state
	SetSimulationPaused(true);

	// Set the new time
	CurrentSimulationTime = NewSimulationTime;

	// Get Elapsed Game Time in seconds
	//float RealtimeSeconds = UGameplayStatics::GetRealTimeSeconds(GetWorld()) * TimeDialation;
	float RealtimeSeconds = UGameplayStatics::GetTimeSeconds(GetWorld()) * TimeDialation; // Doing this means we don't need to do all the nonsense in slate
	AmountOfTimePaused = CurrentSimulationTime - RealtimeSeconds; // When the time is overridden the pause amount needs to be updated

	// Calculate the time in hours, mins, seconds, milliseconds -- we floor to the nearest int as we dont want to skip a time step or jump
	CurrentSimHours = FMath::FloorToInt32(CurrentSimulationTime / 3600);
	CurrentSimMinutes = FMath::FloorToInt32(fmod(CurrentSimulationTime, 3600) / 60);
	CurrentSimSeconds = fmod(CurrentSimulationTime, 60);
	CurrentSimMilliseconds = fmod(CurrentSimulationTime, 1) * 1000;
	
	// Broadcast the new current time
	OnNewCurrentTime.Broadcast(CurrentSimulationTime);

	CalculateCurrentTimeStep(CurrentSimulationTime);

	if(!PreviouslyPaused)
	{
		SetSimulationPaused(false);
		// log current time
		//UE_LOG(LogTemp, Warning, TEXT("Current Time: %f"), CurrentSimulationTime);
	}
}

void UTimeDilationSubSystem::SetSimulationPaused(bool bPaused)
{
	if (bIsPaused == bPaused)
	{
		BroadcastPauseStateIfChanged();
		return;
	}

	bIsPaused = bPaused;
	BroadcastPauseStateIfChanged();
}

float UTimeDilationSubSystem::GetCurrentTimeStepPercentage() const
{
	// Calculate the current time step percentage
	float CurrentTimeStepPercentage = fmod(CurrentSimulationTime, TimeBetweenSteps) / TimeBetweenSteps;
	
	return CurrentTimeStepPercentage;
}

void UTimeDilationSubSystem::FileChanging()
{
	SetSimulationPaused(true);

	CurrentSimulationTime = 0.0f;
	CurrentTimeStep = 0;

	// Broadcast the new current time
	OnNewCurrentTime.Broadcast(0.0f);
}

void UTimeDilationSubSystem::UpdateTimeBetweenData(float NewTimeBetweenData)
{
	TimeBetweenSteps = NewTimeBetweenData;
	OnNewTimeBetweenData.Broadcast(NewTimeBetweenData);
}

void UTimeDilationSubSystem::UpdateSimulationTime()
{
	// Calculate the current simulation time with time dilation applied
	float NewTime = GetGameElapsedTime();

	// check if new time is equal to current time by 3dp
	if (FMath::IsNearlyEqual(CurrentSimulationTime+NewTime, CurrentSimulationTime, 0.001f) || FMath::IsNearlyZero(NewTime))
	{
		return;
	}

	// check if new time has changed
	if (NewTime == CurrentSimulationTime || bIsPaused)
	{
		return;
	}
	
	// Set the new time
        CurrentSimulationTime += NewTime;

        // Calculate the time in hours, mins, seconds, milliseconds -- we floor to the nearest int as we dont want to skip a time step or jump
        CurrentSimHours = FMath::FloorToInt32(CurrentSimulationTime / 3600);
        CurrentSimMinutes = FMath::FloorToInt32(fmod(CurrentSimulationTime, 3600) / 60);
        CurrentSimSeconds = fmod(CurrentSimulationTime, 60);
        CurrentSimMilliseconds = fmod(CurrentSimulationTime, 1) * 1000;

        // Update the cached string so GetCurrentSimTimeStr() reflects the new
        // simulation time without recalculating.
        CurrentSimTimeStr = FormatSimTime(CurrentSimulationTime, CurrentSimHours > 0).ToString();

	if(CurrentSimulationTime <= TotalTime)
	{
		CalculateCurrentTimeStep(CurrentSimulationTime);

		// log current time
		//UE_LOG(LogTemp, Warning, TEXT("Current Time: %f"), CurrentSimulationTime);

		// Broadcast the new current time
		OnNewCurrentTime.Broadcast(CurrentSimulationTime);
	}
	else // we have reached end of simulation likely out by a few milliseconds
	{
		// TODO make this better
		SetSimulationPaused(true);

		CurrentTimeStep = MaxTimeSteps;

		// Broadcast the new current time
		OnNewCurrentTime.Broadcast(TotalTime);
	}
}

void UTimeDilationSubSystem::BroadcastPauseStateIfChanged()
{
	if (bLastBroadcastPauseState == bIsPaused)
	{
		return;
	}

	bLastBroadcastPauseState = bIsPaused;
	OnSimulationPauseChanged.Broadcast(bIsPaused);
}

float UTimeDilationSubSystem::GetGameElapsedTime()
{
	// Get Elapsed Game Time in seconds
	//float RealtimeSeconds = UGameplayStatics::GetRealTimeSeconds(GetWorld()) * TimeDialation;
	
	float RealtimeSeconds = UGameplayStatics::GetTimeSeconds(GetWorld()) * TimeDialation;
	
	float ElapsedTime = RealtimeSeconds - CurrentSimulationTime;

	// Check if we are paused
	if (UGameplayStatics::IsGamePaused(GetWorld()) || bIsPaused)
	{
		
		// Get the amount of time paused
		AmountOfTimePaused = CurrentSimulationTime - RealtimeSeconds;
		
		// As Time Paused return the current simulation time
		return CurrentSimulationTime;
	}
	else
	{
		return ElapsedTime + AmountOfTimePaused;
	}
}

FText UTimeDilationSubSystem::FormatSimTime(float TotalSeconds, bool bIncludeHours) const
{
       // D1 allocation cache: this is called several times per frame (UpdateSimulationTime, the playbar's
       // OnNewCurrentTime handler, GetCurrentSimTimeStr) and, when paused/idle, repeatedly with the same value —
       // and it used to rebuild FNumberFormattingOptions + FFormatNamedArguments + 3-4 FText every call. We now
       // build the formatting options once and cache the result FText, reusing it whenever the *displayed*
       // components are unchanged. The output is a pure function of (bIncludeHours, hour, minute, second,
       // hundredth), so the cached FText is bit-identical to a fresh build for the same key.

       // Build-once formatting: 2-3 digit, zero-padded integral components (identical to the previous options).
       static const FNumberFormattingOptions NumberFormat = []
       {
               FNumberFormattingOptions Opts;
               Opts.MinimumIntegralDigits = 2;
               Opts.MaximumIntegralDigits = 3;
               return Opts;
       }();

       // Displayed components — floored, computed exactly as before (the millisecond field is hundredths).
       const int32 Hour      = FMath::FloorToInt32(TotalSeconds / 3600.f);
       const int32 Minute    = FMath::FloorToInt32(FMath::Fmod(TotalSeconds, 3600.f) / 60.f);
       const int32 Second    = FMath::FloorToInt32(FMath::Fmod(TotalSeconds, 60.f));
       const int32 Hundredth = FMath::FloorToInt32(FMath::Fmod(TotalSeconds, 1.f) * 100.f); // two dp

       // Cache is only touched on the game thread (all callers are UI/game-thread today). A non-game-thread
       // caller falls through to a fresh, allocation-equivalent build so the static FText can never be raced.
       const bool bUseCache = IsInGameThread();
       static bool  bHasCached = false;
       static bool  CachedIncludeHours = false;
       static int32 CachedHour = -1, CachedMinute = -1, CachedSecond = -1, CachedHundredth = -1;
       static FText CachedText;

       if (bUseCache && bHasCached && bIncludeHours == CachedIncludeHours &&
           Hour == CachedHour && Minute == CachedMinute && Second == CachedSecond && Hundredth == CachedHundredth)
       {
               return CachedText;
       }

       FFormatNamedArguments TimeFormatArgs;
       TimeFormatArgs.Add(TEXT("Minute"), FText::AsNumber(Minute, &NumberFormat));
       TimeFormatArgs.Add(TEXT("Second"), FText::AsNumber(Second, &NumberFormat));
       TimeFormatArgs.Add(TEXT("Millisecond"), FText::AsNumber(Hundredth, &NumberFormat));

       FText Result;
       if (bIncludeHours)
       {
               TimeFormatArgs.Add(TEXT("Hour"), FText::AsNumber(Hour, &NumberFormat));
               Result = FText::Format(NSLOCTEXT("ElapsedTimeSpace", "ElapseTimeFormat", "{Hour}:{Minute}:{Second}.{Millisecond}"), TimeFormatArgs);
       }
       else
       {
               Result = FText::Format(NSLOCTEXT("ElapsedTimeSpace", "ElapseTimeFormat", "{Minute}:{Second}.{Millisecond}"), TimeFormatArgs);
       }

       if (bUseCache)
       {
               bHasCached = true;
               CachedIncludeHours = bIncludeHours;
               CachedHour = Hour; CachedMinute = Minute; CachedSecond = Second; CachedHundredth = Hundredth;
               CachedText = Result;
       }

       return Result;
}


// GetAccurateRealTime may be used if greater accuracy is needed
