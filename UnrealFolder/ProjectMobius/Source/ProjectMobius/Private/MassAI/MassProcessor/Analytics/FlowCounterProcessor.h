// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "FlowCounterProcessor.generated.h"

class UStatisticSubsystem;
/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UFlowCounterProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UFlowCounterProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	UPROPERTY(EditAnywhere)
	FMassEntityQuery EntityQuery;

	UPROPERTY()
	TObjectPtr<UStatisticSubsystem> StatisticSubsystem;
};
