// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "StatisticSubsystem.generated.h"

/** TODO: this will be used to pull logic from the heatmap subsystem and the floor stats widget so only one subsystem is used
 * and provide a way to break one of the circular dependencies
 * This subsystem will be used to perform calculations and statistics gathering for user interfaces
 */
UCLASS()
class MOBIUSCORE_API UStatisticSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
};
