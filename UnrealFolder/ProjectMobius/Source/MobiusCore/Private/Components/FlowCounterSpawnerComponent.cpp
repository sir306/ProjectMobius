// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/FlowCounterSpawnerComponent.h"

#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Subsystems/LoadingSubsystem.h"
#include "Subsystems/MobiusCustomLoggerSubsystem.h"


// Sets default values for this component's properties
UFlowCounterSpawnerComponent::UFlowCounterSpawnerComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
	LogToCustomLogger(TEXT("FlowCounterSpawnerComponent constructed"));
}

void UFlowCounterSpawnerComponent::QueueDoorForFlowCounter(UStaticMeshComponent* DoorMesh)
{
	LogToCustomLogger(TEXT("QueueDoorForFlowCounter called"));

	// We only care about valid, non-destroying meshes.
	if (DoorMesh == nullptr || DoorMesh->IsBeingDestroyed())
	{
		LogToCustomLogger(TEXT("QueueDoorForFlowCounter skipped due to invalid or destroying DoorMesh"));
		return;
	}

	const int32 AssignedDoorId = NextDoorSpawnId++;
	PendingDoorMeshes.Add({DoorMesh, AssignedDoorId});

	LogToCustomLogger(FString::Printf(TEXT("Queued door %d (%s) for flow counter spawn. Pending: %d"),
		AssignedDoorId, *DoorMesh->GetName(), PendingDoorMeshes.Num()));
}

void UFlowCounterSpawnerComponent::RemoveAllFlowCounters()
{
	LogToCustomLogger(TEXT("RemoveAllFlowCounters called"));

	if (GetWorld() == nullptr) return;

	// get all flow counters
	TArray<AActor*> FlowCounters;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFlowCounter::StaticClass(), FlowCounters);

	int32 DestroyedCount = 0;
	for (AActor* FlowCounterActor : FlowCounters)
	{
		// we don't need to cast and check, so we can check its not null or pending kill
		if (FlowCounterActor != nullptr && !FlowCounterActor->IsPendingKillPending())
		{
			FlowCounterActor->Destroy();
			DestroyedCount++;
		}
	}
	LogToCustomLogger(FString::Printf(TEXT("RemoveAllFlowCounters destroyed %d actors"), DestroyedCount));
	OnAllFlowCountersRemoved.Broadcast();
}

void UFlowCounterSpawnerComponent::BeginSpawning()
{
	LogToCustomLogger(FString::Printf(TEXT("BeginSpawning called. Pending doors: %d"), PendingDoorMeshes.Num()));

	// If nothing to do, early out.
	if (PendingDoorMeshes.Num() == 0)
	{
		LogToCustomLogger(TEXT("BeginSpawning exiting early - no pending doors"));
		return;
	}

	// Mark that we should start consuming the queue during Tick().
	bSpawning = true;
	LogToCustomLogger(TEXT("Spawning flagged to begin on Tick"));
	
	// Get the loading subsystem
	auto LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>();

	if(LoadingSubsystem)
	{
		// Start the load widget
		LoadingSubsystem->SetLoadingUnknownDuration(true, FString::Printf(TEXT("Remaining Flow Counters to create for doors: %d"), PendingDoorMeshes.Num()));
	}

	UE_LOG(LogTemp, Log, TEXT("Deferred FlowCounter spawning started. Pending doors: %d"), PendingDoorMeshes.Num());
}

void UFlowCounterSpawnerComponent::FlushRemainingSpawns()
{
	if (!bSpawning || PendingDoorMeshes.Num() == 0) return;

	LogToCustomLogger(FString::Printf(TEXT("FlushRemainingSpawns: spawning %d remaining doors immediately"), PendingDoorMeshes.Num()));

	while (PendingDoorMeshes.Num() > 0)
	{
		const FPendingDoorEntry PendingDoor = PendingDoorMeshes[0];
		PendingDoorMeshes.RemoveAtSwap(0);

		if (PendingDoor.DoorMesh.IsValid())
		{
			GenerateFlowCounterForDoor(PendingDoor.DoorMesh.Get(), PendingDoor.DoorId);
		}
		else
		{
			LogToCustomLogger(FString::Printf(TEXT("FlushRemainingSpawns: skipped door %d — mesh no longer valid"), PendingDoor.DoorId));
		}
	}

	bSpawning = false;
	LogToCustomLogger(TEXT("FlushRemainingSpawns complete"));

	if (auto LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>())
	{
		LoadingSubsystem->SetLoadingUnknownDuration(false, TEXT(""));
	}
}

void UFlowCounterSpawnerComponent::AbortSpawning()
{
	const int32 Discarded = PendingDoorMeshes.Num();
	PendingDoorMeshes.Empty();
	NextDoorSpawnId = 1;
	bSpawning = false;

	LogToCustomLogger(FString::Printf(TEXT("AbortSpawning: discarded %d queued doors"), Discarded));

	if (auto LoadingSubsystem = GetWorld() ? GetWorld()->GetSubsystem<ULoadingSubsystem>() : nullptr)
	{
		LoadingSubsystem->SetLoadingUnknownDuration(false, TEXT(""));
	}
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
		// Skip this batch - system under load
		LogToCustomLogger(FString::Printf(TEXT("TickComponent skipped spawning due to frame time %.2fms; pending doors: %d"),
			FrameTime, PendingDoorMeshes.Num()));
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
		const FPendingDoorEntry PendingDoor = PendingDoorMeshes[0];
		PendingDoorMeshes.RemoveAtSwap(0);

		if (PendingDoor.DoorMesh.IsValid())
		{
			LogToCustomLogger(FString::Printf(TEXT("Spawning flow counter for queued door %d (%s). Pending after pop: %d"),
				PendingDoor.DoorId, *PendingDoor.DoorMesh->GetName(), PendingDoorMeshes.Num()));
			GenerateFlowCounterForDoor(PendingDoor.DoorMesh.Get(), PendingDoor.DoorId);
		}
		else
		{
			LogToCustomLogger(FString::Printf(TEXT("Skipped spawn for queued door %d; DoorMesh no longer valid. Pending remaining: %d"),
				PendingDoor.DoorId, PendingDoorMeshes.Num()));
		}

		SpawnedThisBatch++;
	}
	// Get the loading subsystem
	auto LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>();
	
	if (PendingDoorMeshes.Num() == 0)
	{
		bSpawning = false;
		LogToCustomLogger(TEXT("All queued flow counters spawned; stopping spawn loop"));
		
		// End the load widget
		

		if(LoadingSubsystem)
		{
			LoadingSubsystem->SetLoadingUnknownDuration(false, TEXT(""));
		}
	}
	else
	{
		if(LoadingSubsystem)
		{
			// Start the load widget
			LoadingSubsystem->SetLoadingUnknownDuration(true, FString::Printf(TEXT("Remaining Flow Counters to create for doors: %d"), PendingDoorMeshes.Num()));
		}
	}
}

void UFlowCounterSpawnerComponent::GenerateFlowCounterForDoor(UStaticMeshComponent* DoorMesh, int32 DoorId)
{
	LogToCustomLogger(FString::Printf(TEXT("GenerateFlowCounterForDoor called for door %d (%s)"),
		DoorId, DoorMesh ? *DoorMesh->GetName() : TEXT("Invalid")));

	// We can't spawn something that doesn't exist or gives valid data to use
	if (DoorMesh == nullptr || FlowCounterClass == nullptr) return;

	// Spawn location
	FTransform DoorTransform = DoorMesh->GetComponentTransform();

	// Spawn the flow counter at the world transform
	auto SpawnedFlowCounter = GetWorld()->SpawnActor<AFlowCounter>(FlowCounterClass, DoorTransform);

	if (!SpawnedFlowCounter)
	{
		LogToCustomLogger(FString::Printf(TEXT("Flow counter spawn failed for door %d"), DoorId));
		return;
	}

	SpawnedFlowCounter->SetActorLocation(DoorTransform.GetLocation());

	OnFlowCounterAutoSpawned.Broadcast(SpawnedFlowCounter);
	LogToCustomLogger(FString::Printf(TEXT("Flow counter spawned for door %d at %s"),
		DoorId, *DoorTransform.GetLocation().ToCompactString()));

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

UMobiusCustomLoggerSubsystem* UFlowCounterSpawnerComponent::GetCustomLogger() const
{
	return GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
}

void UFlowCounterSpawnerComponent::LogToCustomLogger(const FString& Message) const
{
	if (UMobiusCustomLoggerSubsystem* Logger = GetCustomLogger())
	{
		Logger->EnqueueLogMessage(Message);
	}
}

