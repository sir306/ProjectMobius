// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Actors/FlowCounter.h"
#include "FlowCounterSpawnerComponent.generated.h"

/** Delegate for broadcasting auto flow counter spawns */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFlowCounterSpawned, AFlowCounter*, NewFlowCounter);

/** Delegate for broadcasting all flow counters removed */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAllFlowCounterRemoved);


UCLASS(ClassGroup=(Mobius), meta=(BlueprintSpawnableComponent))
class MOBIUSCORE_API UFlowCounterSpawnerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFlowCounterSpawnerComponent();
	
	UPROPERTY(EditAnywhere, Category="Flow Counters")
	TSubclassOf<AFlowCounter> FlowCounterClass;

	UPROPERTY(EditAnywhere, Category="Flow Counters")
	int32 MaxPerTick = 5;

	void QueueDoorForFlowCounter(UStaticMeshComponent* DoorMesh);
	void RemoveAllFlowCounters();

	// called by owner when all doors discovered
	void BeginSpawning();

	UPROPERTY(EditAnywhere)
	int32 MaxPerBatch = 5;

	UPROPERTY(EditAnywhere)
	float SpawnInterval = 0.25f;
	
	UPROPERTY(EditAnywhere, BlueprintAssignable, Category = "MeshGenerator|Delegates")
	FOnFlowCounterSpawned OnFlowCounterAutoSpawned;
	
	UPROPERTY(EditAnywhere, BlueprintAssignable, Category = "MeshGenerator|Delegates")
	FOnAllFlowCounterRemoved OnAllFlowCountersRemoved;
	
protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction* ThisTickFunction) override;

private:
	TArray<TWeakObjectPtr<UStaticMeshComponent>> PendingDoorMeshes;
	bool bSpawning = false;

	void GenerateFlowCounterForDoor(UStaticMeshComponent* DoorMesh);
	
	double LastSpawnTime = 0.0;

};
