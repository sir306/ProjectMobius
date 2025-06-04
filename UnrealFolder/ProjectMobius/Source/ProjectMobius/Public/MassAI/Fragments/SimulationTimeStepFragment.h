// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h" 
#include "SimulationTimeStepFragment.generated.h"


/**
 * 
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FSimulationTimeStepFragment : public FMassSharedFragment
{
	GENERATED_BODY()
public:
	explicit FSimulationTimeStepFragment();

	FSimulationTimeStepFragment(int32 StartingTimeStep);
	
	~FSimulationTimeStepFragment();

#pragma region PUBLIC_VARIABLES
	// Current time step
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TimeStep")
	int32 CurrentTimeStep;

#pragma endregion PUBLIC_VARIABLES
};
