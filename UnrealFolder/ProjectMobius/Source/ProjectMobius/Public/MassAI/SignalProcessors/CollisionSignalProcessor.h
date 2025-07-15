// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassSignalProcessorBase.h"
#include "CollisionSignalProcessor.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UCollisionSignalProcessor : public UMassSignalProcessorBase
{
	GENERATED_BODY()

public:
	UCollisionSignalProcessor();

	// Where to bind to extra signals etc
	virtual void Initialize(UObject& Owner) override;

protected:
	// Configure the queries for this processor
	virtual void ConfigureQueries() override;
	/**
	 * This method is called to signal entities in the current frame and perform our custom logic, in this case
	 * we will be adding a tag to the entities to allow them to have collisions enabled and update their locations
	 * 
	 * @param EntityManager Entity Manager to use for signaling
	 * @param Context Execution context for the current frame
	 * @param EntitySignals The signal name lookup for the entities
	 */
	virtual void SignalEntities(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
	                            FMassSignalNameLookup& EntitySignals) override;

	FMassEntityQuery EntityQuery;
};
