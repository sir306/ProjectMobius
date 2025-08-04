// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/FlowCounter.h"
#include "Subsystems/WorldSubsystem.h"
#include "StatisticActorManagementSubsystem.generated.h"


// Delegate
/** * Delegate to notify when FlowCounters changes +/- */
DECLARE_DYNAMIC_DELEGATE(FOnFlowCountersChanged);

/**
 * 
 */
UCLASS()
class MOBIUSCORE_API UStatisticActorManagementSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	UStatisticActorManagementSubsystem();

	UFUNCTION(BlueprintCallable)
	void AddFlowCounter(AFlowCounter* FlowCounter);

	UFUNCTION(BlueprintCallable)
	void RemoveFlowCounter(AFlowCounter* FlowCounter);

	UPROPERTY(EditAnywhere)
	FOnFlowCountersChanged OnFlowCountersChanged;

	/** Reference to the FlowCounter actors */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TObjectPtr<AFlowCounter>> FlowCounters = TArray<TObjectPtr<AFlowCounter>>();
};
