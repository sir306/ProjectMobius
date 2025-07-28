// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/FlowCounter.h"

#include "Components/BoxComponent.h"


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
	FlowCounterPillarMesh1->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	FlowCounterPillarMesh1->SetVisibility(true);

	FlowCounterPillarMesh2 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlowCounterPillarMesh2"));
	FlowCounterPillarMesh2->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	FlowCounterPillarMesh2->SetVisibility(true);
	

	// load mesh for the pillars
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultMesh(TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Cylinder.Cylinder'"));
	if (DefaultMesh.Succeeded())
	{
		FlowCounterPillarMesh1->SetStaticMesh(DefaultMesh.Object);
		FlowCounterPillarMesh1->SetRelativeLocation(FVector(50.0f, 0.0f, 0.0f)); // Offset for ease of development
		FlowCounterPillarMesh1->SetRelativeScale3D(FVector(0.1f,0.1f,1.0f)); // Scale down the pillar for better visibility
			
		FlowCounterPillarMesh2->SetStaticMesh(DefaultMesh.Object);
		FlowCounterPillarMesh2->SetRelativeLocation(FVector(-50.0f, 0.0f, 0.0f)); // Offset for ease of development
		FlowCounterPillarMesh2->SetRelativeScale3D(FVector(0.1f,0.1f,1.0f)); // Scale down the pillar for better visibility
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Default mesh not found!"));
	}

	// Set up the box component for flow counter trigger area
	FlowCounterTriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("FlowCounterTriggerBox"));
	FlowCounterTriggerBox->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	FlowCounterTriggerBox->SetBoxExtent(FVector(100.0f, 50.0f, 100.0f)); // Set the size of the box
	

	RootComponent->UpdateChildTransforms();
}

// Called when the game starts or when spawned
void AFlowCounter::BeginPlay()
{
	Super::BeginPlay();

}

// Called every frame
void AFlowCounter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

