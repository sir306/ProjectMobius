// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassSignalProcessorBase.h"
#include "EnableCollisionSignalProcessor.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UEnableCollisionSignalProcessor : public UMassSignalProcessorBase
{
	GENERATED_BODY()

public:
	UEnableCollisionSignalProcessor();

	// Where to bind to extra signals etc
	virtual void Initialize(UObject& Owner) override;

protected:
	// Configure the queries for this processor
	virtual void ConfigureQueries() override;
	/**
	 * This method is called to signal entities in the current frame and perform our custom logic, in this case
	 * we will be adding a tag to the entities to allow them to have collisions enabled and enabling the collisions
	 * 
	 * @param EntityManager Entity Manager to use for signaling
	 * @param Context Execution context for the current frame
	 * @param EntitySignals The signal name lookup for the entities
	 */
	virtual void SignalEntities(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
	                            FMassSignalNameLookup& EntitySignals) override;

	FMassEntityQuery EntityQuery;
};


UCLASS()
class PROJECTMOBIUS_API UDisableCollisionSignalProcessor : public UMassSignalProcessorBase
{
	GENERATED_BODY()

public:
	UDisableCollisionSignalProcessor();

	// Where to bind to extra signals etc
	virtual void Initialize(UObject& Owner) override;

protected:
	// Configure the queries for this processor
	virtual void ConfigureQueries() override;
	/**
	 * This method is called to signal entities in the current frame and perform our custom logic, in this case
	 * we will be removing the enable collision tag from the entities and disabling their collisions and clearing any hovered capsule components
	 * 
	 * @param EntityManager Entity Manager to use for signaling
	 * @param Context Execution context for the current frame
	 * @param EntitySignals The signal name lookup for the entities
	 */
	virtual void SignalEntities(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
								FMassSignalNameLookup& EntitySignals) override;

	FMassEntityQuery EntityQuery;
};