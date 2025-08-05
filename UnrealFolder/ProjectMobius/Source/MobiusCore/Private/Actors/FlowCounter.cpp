// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/FlowCounter.h"

#include "Components/BoxComponent.h"
#include "Subsystems/StatisticActorManagementSubsystem.h"
#include "Subsystems/StatisticSubsystem.h"


// Sets default values
AFlowCounter::AFlowCounter()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	/* Attach the meshes to the scene component root */
	// Create a default scene root component
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// Create pillar mesh components
	FlowCounterPillarMesh1 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlowCounterPillarMesh1"));
	FlowCounterPillarMesh1->SetupAttachment(RootComponent);
	FlowCounterPillarMesh1->SetVisibility(true);
	// As we use world space, we need to set the pillars to use world space
	
	

	FlowCounterPillarMesh2 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlowCounterPillarMesh2"));
	FlowCounterPillarMesh2->SetupAttachment(RootComponent);
	FlowCounterPillarMesh2->SetVisibility(true);
	

	// load mesh for the pillars
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultMesh(TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Cylinder.Cylinder'"));
	if (DefaultMesh.Succeeded())
	{
		FlowCounterPillarMesh1->SetStaticMesh(DefaultMesh.Object);
		FlowCounterPillarMesh1->SetWorldLocation(FVector(50.0f, 0.0f, 0.0f)); // Offset for ease of development
		FlowCounterPillarMesh1->SetWorldScale3D(FVector(0.1f,0.1f,1.0f)); // Scale down the pillar for better visibility
			
		FlowCounterPillarMesh2->SetStaticMesh(DefaultMesh.Object);
		FlowCounterPillarMesh2->SetWorldLocation(FVector(-50.0f, 0.0f, 0.0f)); // Offset for ease of development
		FlowCounterPillarMesh2->SetWorldScale3D(FVector(0.1f,0.1f,1.0f)); // Scale down the pillar for better visibility
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Default mesh not found!"));
	}

	// Set up the box component for flow counter trigger area
	FlowCounterTriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("FlowCounterTriggerBox"));
	//FlowCounterTriggerBox->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
	FlowCounterTriggerBox->SetupAttachment(RootComponent);
	
	UpdateFlowCounterTriggerBox();
	
	// As the trigger box is only used to check whether a location is within the flow counter area, we can set the collision to none
	FlowCounterTriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlowCounterTriggerBox->SetCollisionObjectType(ECC_WorldDynamic);
	FlowCounterTriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	//DEBUG - for now we will make the trigger box visible in the sim
	FlowCounterTriggerBox->SetVisibility(true);
	FlowCounterTriggerBox->SetHiddenInGame(false);
	
	

	RootComponent->UpdateChildTransforms();
}

AFlowCounter::~AFlowCounter()
{
	RemoveFlowCounterToSubsystem();
}

// Called when the game starts or when spawned
void AFlowCounter::BeginPlay()
{
	Super::BeginPlay();

	// Here we need to tell the statistic subsystem that this flow counter exists and is in the world
	AddFlowCounterToSubsystem();
}

// Called every frame
void AFlowCounter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AFlowCounter::MoveGatePillarMeshToLocation(int32 PillarIndex, const FVector& NewLocation)
{
	if (PillarIndex != 0 && PillarIndex != 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid Pillar Index! Must be 0 or 1."));
		return;
	}
	//TODO: likely need to communicate pillars location changing wait for changes to mass ai so we don't get data corruption/mismatches
	
	if (PillarIndex == 0 && FlowCounterPillarMesh1)
	{
		FlowCounterPillarMesh1->SetWorldLocation(NewLocation);
	}
	else if (PillarIndex == 1 && FlowCounterPillarMesh2)
	{
		FlowCounterPillarMesh2->SetWorldLocation(NewLocation);
	}

	// After moving the pillar mesh, we need to update the trigger box to match the new pillar locations
	UpdateFlowCounterTriggerBox();
	
	// When a pillar is moved, we also need to reset the previous tracked agent locations and reset the flow counter count to 0
	PreviousTrackedAgentLocations.Empty();
	FlowCounterCount.Exchange(0); // Reset the flow counter count to 0

	// TODO: depending on mass ai logic we may want to reset the agents passed through counter as well

	// Communicate to the statistic subsystem that the flow counter has been updated
	AddFlowCounterToSubsystem();
}

void AFlowCounter::ResizeFlowCounterTriggerBox(float& OutDistanceBetweenPillars, FVector& OutCenterLocation) const
{
	// if the pillars are not valid, return
	if (!FlowCounterPillarMesh1 || !FlowCounterPillarMesh2)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowCounter Pillar Meshes are not valid!"));
		return;
	}

	OutDistanceBetweenPillars = FVector::Dist(FlowCounterPillarMesh1->GetComponentLocation(), FlowCounterPillarMesh2->GetComponentLocation());

	if (OutDistanceBetweenPillars <= 0.0f)
	{
		OutDistanceBetweenPillars = FVector::Dist(FlowCounterPillarMesh1->GetRelativeLocation(), FlowCounterPillarMesh2->GetRelativeLocation());
	}

	// center location is the average of the two pillar locations
	OutCenterLocation = (FlowCounterPillarMesh1->GetComponentLocation() + FlowCounterPillarMesh2->GetComponentLocation()) / 2.0f;
}

void AFlowCounter::ResizeFlowCounterTriggerBoxExtent(const FVector& NewExtent)
{
}

void AFlowCounter::UpdateFlowCounterTriggerBoxLocation(const FVector& NewLocation)
{
}

void AFlowCounter::UpdateFlowCounterTriggerBox()
{
	if (FlowCounterTriggerBox == nullptr)
	{
		return;// Early exit if the trigger box is not valid
	}
	float DistanceBetweenPillars;
	FVector CenterLocation;
	ResizeFlowCounterTriggerBox(DistanceBetweenPillars, CenterLocation);

	// Center Location should also be the root components location, that way child objects that aren't dynamic can update with it
	RootComponent->SetWorldLocation(CenterLocation);
	
	FlowCounterTriggerBox->SetBoxExtent(FVector(DistanceBetweenPillars / 2.0f, 50.0f, 100.0f));

	// Offset Center location to be minus 10 - to ensure we capture all agents that pass through the flow counter line
	// this is due to the sensitivity of the line intersection check and the fact that agents may not be exactly on the lower z if offset by a few units
	CenterLocation.Z -= 10.0f;
	
	// We may want to offset the box location in Z 
	FlowCounterTriggerBox->SetWorldLocation(CenterLocation);

	// Update the Z search limits based on the trigger box location and Z box extents
	FlowCounterZSearchLimits.MinZBounds = CenterLocation.Z - (FlowCounterTriggerBox->GetScaledBoxExtent().Z );
	FlowCounterZSearchLimits.MaxZBounds = CenterLocation.Z + (FlowCounterTriggerBox->GetScaledBoxExtent().Z );

	// Update the FlowCounterLineStartLocation and FlowCounterLineEndLocation based on the pillar locations
	FlowCounterLineStartLocation = FlowCounterPillarMesh1->GetComponentLocation();
	FlowCounterLineEndLocation = FlowCounterPillarMesh2->GetComponentLocation();

	// Rotate the trigger box to align with the flow counter line
	FRotator BoxRotation = (FlowCounterLineEndLocation - FlowCounterLineStartLocation).Rotation();
	FlowCounterTriggerBox->SetWorldRotation(BoxRotation);

	// Root component should be updated to reflect the same orientation as the trigger box
	RootComponent->SetWorldRotation(BoxRotation);
}

void AFlowCounter::NewAgentData(UE::TConsumeAllMpmcQueue<FFlowCounterData>& NewAgentData)
{
	/* We want to process the new agent data and check if the agent is within the flow counter trigger box
	 * - the data passed here is expected to be within the Z bounds of the flow counter trigger box
	 * (this is due to the potential amount of agent data passed and multiple flow counters may be present).
	 */

	

	TWeakObjectPtr<AFlowCounter> WeakThis(this);
	
	// dequeue the new agent data
	NewAgentData.ConsumeAllFifo([WeakThis](const FFlowCounterData& Data)
	{
		if (!WeakThis.IsValid()) return;

		AFlowCounter* FlowCounter = WeakThis.Get();

		// Get the flow counter trigger box so we can check if the agent is within the box
		UE::Math::TBox FlowCounterBox = FlowCounter->FlowCounterTriggerBox->Bounds.GetBox();
		// check if new agent is already been added to the completed agent set - if so we can skip it
		if (!FlowCounter->AgentsPassedThroughCounter.Contains(Data.AgentID))
		{
			// Agent has already been processed, skip it
			return;
		}

		// check if the agent is within the flow counter trigger box
		if (FMath::PointBoxIntersection(Data.Location, FlowCounterBox))
		{
			// check if the agent is already tracked
			if (FlowCounter->PreviousTrackedAgentLocations.Contains(Data.AgentID))
			{
				FVector* PreviousLocation = FlowCounter->PreviousTrackedAgentLocations.Find(Data.AgentID);
				// perform line intersection check to see if the agent has crossed the flow counter line
				FVector IntersectionLocation;
				FVector CurrentLocation = Data.Location;

				bool bAgentCrossed = FMath::SegmentIntersection2D(*PreviousLocation, CurrentLocation,
				                                                  FlowCounter->FlowCounterLineStartLocation, FlowCounter->FlowCounterLineEndLocation, IntersectionLocation);

				// if we intersect, then add it to the completed agent set and increment the flow counter
				if (bAgentCrossed)
				{
					FlowCounter->AgentsPassedThroughCounter.Add(Data.AgentID);
					FlowCounter->FlowCounterCount.AddExchange(1);
				}
				else
				{
					// Agent has not crossed the line, update the previous tracked agent location with the new location
					FlowCounter->PreviousTrackedAgentLocations[Data.AgentID] = Data.Location;
				}
			}
			else
			{
				// Agent is not tracked, add it 
				FlowCounter->PreviousTrackedAgentLocations.Add(Data.AgentID, Data.Location);
			}
		}
		// check that we weren't already tracking the agent in case movement extends pass the trigger box
		else if (FlowCounter->PreviousTrackedAgentLocations.Contains(Data.AgentID)) // TODO: check if we actually want to check if intersected or disregard 
		{
			FVector* PreviousLocation = FlowCounter->PreviousTrackedAgentLocations.Find(Data.AgentID);
			// perform line intersection check to see if the agent has crossed the flow counter line
			FVector IntersectionLocation;
			FVector CurrentLocation = Data.Location;

			bool bAgentCrossed = FMath::SegmentIntersection2D(*PreviousLocation, CurrentLocation,
			                                                  FlowCounter->FlowCounterLineStartLocation, FlowCounter->FlowCounterLineEndLocation, IntersectionLocation);

			// if we intersect, then add it to the completed agent set and increment the flow counter
			if (bAgentCrossed)
			{
				FlowCounter->AgentsPassedThroughCounter.Add(Data.AgentID);
				FlowCounter->FlowCounterCount.AddExchange(1);
			}

			// if we intersect or not we want to remove it from the previous tracked agent locations as we are no longer tracking it and likely moving away from the flow counter
			FlowCounter->PreviousTrackedAgentLocations.Remove(Data.AgentID);
		}
		else
		{
			return; // Agent is not within the flow counter trigger box, skip it
		}
		
	});

	// DEBUG
	//UE_LOG(LogTemp, Warning, TEXT("Flow Counter Count: %d"), FlowCounterCount.Load());//just log output till we get a UI to display the count
}

void AFlowCounter::AddFlowCounterToSubsystem()
{
	if (GetWorld() == nullptr){return;}  
	// We want to notify the statistic subsystem of the flow counter update passing through the upper and lower limits and a ptr to this
	
	auto* StatActorManagerSubsystem = GetWorld()->GetSubsystem<UStatisticActorManagementSubsystem>();
	if (StatActorManagerSubsystem != nullptr)
	{
		StatActorManagerSubsystem->AddFlowCounter(this);		
	}
}

void AFlowCounter::RemoveFlowCounterToSubsystem()
{
	if (GetWorld() == nullptr){return;}  
	// We want to notify the statistic subsystem of the flow counter update passing through the upper and lower limits and a ptr to this
	
	auto* StatActorManagerSubsystem = GetWorld()->GetSubsystem<UStatisticActorManagementSubsystem>();
	if (StatActorManagerSubsystem != nullptr)
	{
		StatActorManagerSubsystem->RemoveFlowCounter(this);		
	}
}
