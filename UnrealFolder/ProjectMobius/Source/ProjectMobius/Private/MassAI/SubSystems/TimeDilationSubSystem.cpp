// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/SubSystems/TimeDilationSubSystem.h"
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
		
		// log that it has binded
		UE_LOG(LogTemp, Warning, TEXT("Time Dilation Scale Factor Changed Delegate Binded"));
	}
	
	
	// Get the Time Dilation from the ProjectMobius Game Instance
	GetUpdatedTimeDilation();
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
	}

	// If we have other subsystems that we depend on we can deinitialize them here after super
	Super::Deinitialize();
}

void UTimeDilationSubSystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	// Check if we are paused
	// if (!UGameplayStatics::IsGamePaused(GetWorld()) || !bIsPaused)
	// {
	// 	// Update the simulation time
	// 	UpdateSimulationTime();
	// }
	// Update the simulation time
	UpdateSimulationTime();
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
		bIsPaused = true;
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
	//UpdateMaxTimeSteps((TotalTime / TimeBetweenSteps)); //TODO: TimeBetweenSteps should be read from file or a setting for the user

}

void UTimeDilationSubSystem::OverrideCurrentTime(float NewSimulationTime, const uint8 PreviouslyPaused)
{
	// Pause the simulation regardless of the previous state
	bIsPaused = true;

	// Set the new time
	CurrentSimulationTime = NewSimulationTime;

	// Get Elapsed Game Time in seconds
	float RealtimeSeconds = UGameplayStatics::GetRealTimeSeconds(GetWorld()) * TimeDialation;
	AmountOfTimePaused = CurrentSimulationTime - RealtimeSeconds; // When the time is overriden the pause amount needs to be updated

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
		bIsPaused = false;
		// log current time
		//UE_LOG(LogTemp, Warning, TEXT("Current Time: %f"), CurrentSimulationTime);
	}
}

float UTimeDilationSubSystem::GetCurrentTimeStepPercentage() const
{
	// Calculate the current time step percentage
	float CurrentTimeStepPercentage = fmod(CurrentSimulationTime, TimeBetweenSteps) / TimeBetweenSteps;
	
	return CurrentTimeStepPercentage;
}

void UTimeDilationSubSystem::FileChanging()
{
	bIsPaused = true;

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
		bIsPaused = true;

		CurrentTimeStep = MaxTimeSteps;

		// Broadcast the new current time
		OnNewCurrentTime.Broadcast(TotalTime);
	}
}

float UTimeDilationSubSystem::GetGameElapsedTime() 
{
	// Get Elapsed Game Time in seconds
	float RealtimeSeconds = UGameplayStatics::GetRealTimeSeconds(GetWorld()) * TimeDialation;

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

// GetAccurateRealTime may be used if greater accuracy is needed