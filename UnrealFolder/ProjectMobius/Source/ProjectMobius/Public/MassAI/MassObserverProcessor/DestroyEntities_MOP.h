// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassObserverProcessor.h"
#include "DestroyEntities_MOP.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UDestroyEntities_MOP : public UMassObserverProcessor
{
	GENERATED_BODY()
	
public:
	UDestroyEntities_MOP();

protected:
	virtual void ConfigureQueries() override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash
private:
	FMassEntityQuery EntityQuery;
};
