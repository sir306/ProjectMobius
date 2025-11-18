// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/FlowCounterSpawnerComponent.h"

#include "Kismet/GameplayStatics.h"
#include "Subsystems/LoadingSubsystem.h"


// Sets default values for this component's properties
UFlowCounterSpawnerComponent::UFlowCounterSpawnerComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}

void UFlowCounterSpawnerComponent::QueueDoorForFlowCounter(UStaticMeshComponent* DoorMesh)
{
	// We only care about valid, non-destroying meshes.
	if (DoorMesh == nullptr || DoorMesh->IsBeingDestroyed())
	{
		return;
	}

	PendingDoorMeshes.Add(DoorMesh);
}

void UFlowCounterSpawnerComponent::RemoveAllFlowCounters()
{
	if (GetWorld() == nullptr) return;

	// get all flow counters
	TArray<AActor*> FlowCounters;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFlowCounter::StaticClass(), FlowCounters);

	for (AActor* FlowCounterActor : FlowCounters)
	{
		// we don't need to cast and check, so we can check its not null or pending kill
		if (FlowCounterActor != nullptr && !FlowCounterActor->IsPendingKillPending())
		{
			FlowCounterActor->Destroy();
		}
	}
	OnAllFlowCountersRemoved.Broadcast();
}

void UFlowCounterSpawnerComponent::BeginSpawning()
{
	// If nothing to do, early out.
	if (PendingDoorMeshes.Num() == 0)
	{
		return;
	}

	// Mark that we should start consuming the queue during Tick().
	bSpawning = true;
	
	// Get the loading subsystem
	auto LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>();

	if(LoadingSubsystem)
	{
		// Start the load widget
		LoadingSubsystem->SetLoadingUnknownDuration(true, FString::Printf(TEXT("Generating %d Flow Counters for Doors"), PendingDoorMeshes.Num()));
	}

	UE_LOG(LogTemp, Log, TEXT("Deferred FlowCounter spawning started. Pending doors: %d"), PendingDoorMeshes.Num());
}

// Called every frame
void UFlowCounterSpawnerComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                                 FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if (!bSpawning || PendingDoorMeshes.Num() == 0)
		return;
	
	// Check frame time throttle to avoid spawning when the game is struggling
	float FrameTime = DeltaTime * 1000.0f; // ms
	if (FrameTime > 25.0f)
	{
		// Skip this batch – system under load
		return;
	}

	// Check TIME throttle (not frame throttle)
	const double Now = FPlatformTime::Seconds();
	if (Now - LastSpawnTime < SpawnInterval)
		return;

	LastSpawnTime = Now;

	int32 SpawnedThisBatch = 0;

	while (PendingDoorMeshes.Num() > 0 && SpawnedThisBatch < MaxPerBatch)
	{
		TWeakObjectPtr<UStaticMeshComponent> Door = PendingDoorMeshes[0];
		PendingDoorMeshes.RemoveAtSwap(0);

		if (Door.IsValid())
		{
			GenerateFlowCounterForDoor(Door.Get());
		}

		SpawnedThisBatch++;
	}

	if (PendingDoorMeshes.Num() == 0)
	{
		bSpawning = false;
		
		// End the load widget
		auto LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>();

		if(LoadingSubsystem)
		{
			LoadingSubsystem->SetLoadingUnknownDuration(false, TEXT(""));
		}
	}
}

void UFlowCounterSpawnerComponent::GenerateFlowCounterForDoor(UStaticMeshComponent* DoorMesh)
{
	// We can't spawn something that doesn't exist or gives valid data to use
	if (DoorMesh == nullptr || FlowCounterClass == nullptr) return;

	// Spawn location
	FTransform DoorTransform = DoorMesh->GetComponentTransform();

	// Spawn the flow counter at the world transform
	auto SpawnedFlowCounter = GetWorld()->SpawnActor<AFlowCounter>(FlowCounterClass, DoorTransform);

	SpawnedFlowCounter->SetActorLocation(DoorTransform.GetLocation());

	OnFlowCounterAutoSpawned.Broadcast(SpawnedFlowCounter);

	FVector MinBounds, MaxBounds;
	
	DoorMesh->GetLocalBounds(MinBounds, MaxBounds);

	MaxBounds.Z = 0;
	//MaxBounds.Y = MaxBounds.X;
	//MaxBounds.X = 0;

	// if x is bigger than y then set y to 0 else set x to 0
	if (FMath::Abs(MaxBounds.X) > FMath::Abs(MaxBounds.Y))
	{
		MaxBounds.Y = 0;
	}
	else
	{
		MaxBounds.X = 0;
	}

	MaxBounds = DoorTransform.GetRotation().Rotator().UnrotateVector(MaxBounds);
	
	SpawnedFlowCounter->MoveGatePillarMeshToLocation(0, ((DoorTransform.GetLocation() - MaxBounds) + FVector(0,0,100)));
	SpawnedFlowCounter->MoveGatePillarMeshToLocation(1, ((DoorTransform.GetLocation() + MaxBounds) + FVector(0,0,100)));

	//SpawnedFlowCounter->SetActorRotation(DoorTransform.GetRotation());

	// TODO: Manipulate the rotation, size etc to fit door
}

