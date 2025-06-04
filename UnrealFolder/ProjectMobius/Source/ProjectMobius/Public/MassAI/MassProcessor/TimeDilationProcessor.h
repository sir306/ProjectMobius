// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "TimeDilationProcessor.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UTimeDilationProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UTimeDilationProcessor();

protected:
	virtual void ConfigureQueries() override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash



private:

	FMassEntityQuery EntityQuery;
};
