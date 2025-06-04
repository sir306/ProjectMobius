// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassObserverProcessor.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "PedestrianInitializeMOP.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UPedestrianInitializeMOP : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UPedestrianInitializeMOP();

protected:
	virtual void ConfigureQueries() override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash



private:

	void InitializeEntityInfoAgent(int32 InEntityID, FEntityInfoFragment& EntityInfoToAssign);

	void InitializeEntityInfoAgent(FEntityInfoFragment& EntityInfoToAssign, int32 InEntityID, FString InEntityName, FString InEntitySimTimeS, float InEntityMaxSpeed, FString InEntityM_Plane, int32 InEntityMap);

	//void SetEntitiesPedestrianMovement(FPedestrianMovementFragment& PedestrianMovementToAssign, FSimMovementSample InSharedMovementData);

	FMassEntityQuery EntityQuery;
};
