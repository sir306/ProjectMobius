// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Actors/FlowCounter.h"
#include "FlowCounterSpawnerComponent.generated.h"

class UMobiusCustomLoggerSubsystem;

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

	// bypass frame-budget throttle — call after load spikes to finish remaining spawns immediately
	void FlushRemainingSpawns();

	// cancel in-progress spawning and discard all queued doors — call before a new load replaces the mesh
	void AbortSpawning();

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
	struct FPendingDoorEntry
	{
		TWeakObjectPtr<UStaticMeshComponent> DoorMesh;
		int32 DoorId = INDEX_NONE;
	};

	TArray<FPendingDoorEntry> PendingDoorMeshes;
	int32 NextDoorSpawnId = 1;
	bool bSpawning = false;

    void GenerateFlowCounterForDoor(UStaticMeshComponent* DoorMesh, int32 DoorId);
    UMobiusCustomLoggerSubsystem* GetCustomLogger() const;
    void LogToCustomLogger(const FString& Message) const;
	
	double LastSpawnTime = 0.0;

};
