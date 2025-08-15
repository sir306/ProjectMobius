// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassSignalProcessorBase.h"
#include "FlowCounterSignalProcessor.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UEnableFlowCounterSignalProcessor : public UMassSignalProcessorBase
{
	GENERATED_BODY()

public:
	UEnableFlowCounterSignalProcessor();
	virtual void Initialize(UObject& Owner) override;

protected:
	virtual void ConfigureQueries() override;
	virtual void SignalEntities(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
		FMassSignalNameLookup& EntitySignals) override;

	UPROPERTY(EditAnywhere)
	FMassEntityQuery EntityQuery;
};


/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UDisableFlowCounterSignalProcessor : public UMassSignalProcessorBase
{
	GENERATED_BODY()
public:
	UDisableFlowCounterSignalProcessor();
	virtual void Initialize(UObject& Owner) override;

protected:
	virtual void ConfigureQueries() override;
	virtual void SignalEntities(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
		FMassSignalNameLookup& EntitySignals) override;

	UPROPERTY(EditAnywhere)
	FMassEntityQuery EntityQuery;
};
