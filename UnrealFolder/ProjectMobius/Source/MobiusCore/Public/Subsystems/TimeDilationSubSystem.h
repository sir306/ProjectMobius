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
#include "MassEntitySubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "Interfaces/ProjectMobiusInterface.h"
#include "TimeDilationSubSystem.generated.h"

/** Delegate to broadcast the max simulation time when it changes */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNewMaxTime, float, NewMaxTime);

/** Delegate to broadcast the current simulation time when it changes */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNewCurrentTime, float, NewCurrentTime);

/** Delegate to broadcast the simulation time between data */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNewTimeBetweenData, float, NewTimeBetweenData);

/** Delegate to broadcast the new simulation time when it changes */ //TODO: do we need this delegate? left comment for now


/**
 * Template that is defined for the Time Dilation Subsystem so it can be used with Mass Entity systems 
 */
template<>
struct TMassExternalSubsystemTraits<UTimeDilationSubSystem> final
{
	enum
	{
		ThreadSafeRead = true,
		ThreadSafeWrite = false,
		GameThreadOnly = false,
	};
};

/**
 * This world sub system is used to store the time step information for the AI
 * We can store simulation total time, time between steps, simulation time dialation, etc.
 * This is a singleton class
 * By using a sub system we can have a global time step information that can be accessed
 * by all our classes
 */
UCLASS()
class MOBIUSCORE_API UTimeDilationSubSystem : public UTickableWorldSubsystem, public IProjectMobiusInterface
{
	GENERATED_BODY()
	
public:
#pragma region CONSTRUCTORS_DESTRUCTORS
	// Default Constructor
	explicit UTimeDilationSubSystem();

#pragma endregion CONSTRUCTORS_DESTRUCTORS

#pragma region PUBLIC_METHODS

#pragma region INHERITED_METHODS
	/**
	* Initialize the subsystem -- we can initialize other subsystems and delegates here
	* @param Collection The collection that this subsystem belongs to
	*/
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Deinitialize the subsystem */
	virtual void Deinitialize() override;

	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTimeDilationSubSystem, STATGROUP_Tickables); }

	/** Tick -- fires every frame */
	virtual void Tick(float DeltaTime) override;
#pragma endregion INHERITED_METHODS

#pragma region CUSTOM_METHODS
	/** Calculate current time step */
	void CalculateCurrentTimeStep(float SimCurrentTime);

	/** Get Updated Time Dilation -- This is bound to the interfaces broadcast to ensure updates happen */
	UFUNCTION()
	void GetUpdatedTimeDilation();

	/**
	 * Update the Max Time Steps
	 *
	 * @param[int32] NewMaxTimeSteps - The new max time steps
	 */
	void UpdateMaxTimeSteps(int32 NewMaxTimeSteps);

	/**
	 * Update the simulation total time
	 *
	 * @param[float] NewTotalTime - The new total time
	 */
	void UpdateTotalTime(float NewTotalTime);

	/**
	 * Offset the simulation time
	 * This is used to offset simulation time usually when a user drags the slider
	 *
	 * @param[float] NewSimulationTime - The new simulation time
	 * @param[uint8] PreviouslyPaused - To avoid the simulation from un pausing when dragging the slider and updating the time before the new value is set pass a value of 0
	 */
	UFUNCTION(BlueprintCallable, Category = "Time Dilation")
	void OverrideCurrentTime(float NewSimulationTime, uint8 PreviouslyPaused = 1);

	/**
	 * This method gives a 0-1 percentage of the current time step, this is used to interpolate between time steps
	 * this should limit the issue of agents jumping between time steps and give a smoother transition
	 *
	 */
	float GetCurrentTimeStepPercentage() const;

	/**
	 * As movement data can be changed by the user at any time, the simulation needs to be paused and reset to 0 time
	 */
	UFUNCTION()
	void FileChanging();

	/**
	 * Update the simulation timestep value
	 *
	 * @param[float] NewTimeBetweenData - The new time between data
	 */
	void UpdateTimeBetweenData(float NewTimeBetweenData);

#pragma endregion CUSTOM_METHODS


#pragma endregion PUBLIC_METHODS

#pragma region PUBLIC_PROPERTIES
	/** The total time of the simulation in seconds */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	float TotalTime;

	/** The time between steps in seconds */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	float TimeBetweenSteps;

	/** The time scale dialation of the simulation */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	float TimeDialation;

	/** The current simulation time */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	float CurrentSimulationTime = 0.0f;

	//TODO: @Peter -- This an assumption to be clarified but I am assuming all timesteps are calculated
	// at the same time, via a time step ie . 0.1 seconds, 0.2 seconds, etc. and not a per agent basis
	/** Current Time Step, this is also the starting time step */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep", meta = (ClampMin = 0))
	int32 CurrentTimeStep;

	/** Max number of time steps */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	int32 MaxTimeSteps;

	/** This value is used to calculate time step for hours and minutes */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	float SixtySecondTimeSteps;

	/** The hours of current simulation time */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	int32 CurrentSimHours;

	/** The minutes of current simulation time */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	int32 CurrentSimMinutes;

	/** The seconds of current simulation time */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	int32 CurrentSimSeconds;
	
	/** The milliseconds of current simulation time */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	int32 CurrentSimMilliseconds;

	/** The full string of current simulation time */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	FString CurrentSimTimeStr;

	/** The Amount of time a simulation has been paused for */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	float AmountOfTimePaused;

	/** A bool to configure pauses - i.e. when we still in the widget */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	bool bIsPaused = true;

	/** Project Mobius Interface - used to get variables */
	IProjectMobiusInterface* ProjectMobiusInterface;

#pragma endregion PUBLIC_PROPERTIES

#pragma region DELEGATES
	/** Delegate to broadcast max time changes */
	FOnNewMaxTime OnNewMaxTime;

	/** Delegate to broadcast current time */
	FOnNewCurrentTime OnNewCurrentTime;

	/** Delegate for broadcast time between data */
	FOnNewTimeBetweenData OnNewTimeBetweenData;
	
#pragma endregion DELEGATES

protected:
#pragma region PROTECTED_METHODS

#pragma endregion PROTECTED_METHODS
private:
#pragma region PRIVATE_METHODS
	/** Update our simulation time */
	void UpdateSimulationTime();

	/** Get the game elapsed time */
	UFUNCTION()// as we are binding to a delegate it must be a UFUNCTION
	float GetGameElapsedTime();
#pragma endregion PRIVATE_METHODS

public:
#pragma region PUBLIC_GETTERS_SETTERS
	/** Get the current simulation time */
	FORCEINLINE float GetCurrentSimTime() 
	{
		UE_MT_SCOPED_READ_ACCESS(AccessDetector);
		//UpdateSimulationTime(); // we only want to update the simulation time when we need it not every frame
		return CurrentSimulationTime;
	}

	/**
	 * Get the current simulation time as a string -- which will also update the simulation time
	 */
	FORCEINLINE FString GetCurrentSimTimeStr()
	{
		UE_MT_SCOPED_READ_ACCESS(AccessDetector);
		UpdateSimulationTime(); // we only want to update the simulation time when we need it not every frame
		return CurrentSimTimeStr;
	}

	/** Get the current time step */
	FORCEINLINE int32 GetCurrentTimeStep() const
	{
		UE_MT_SCOPED_READ_ACCESS(AccessDetector);
		return CurrentTimeStep;
	}
#pragma endregion PUBLIC_GETTERS_SETTERS

private:
	UE_MT_DECLARE_RW_ACCESS_DETECTOR(AccessDetector);
	
};


