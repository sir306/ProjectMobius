// Fill out your copyright notice in the Description page of Project Settings.


#include "RuntimeMeshGeneration/RuntimeHeatmapBuilder.h"

#include "ProceduralMeshComponent.h"


// Sets default values
ARuntimeHeatmapBuilder::ARuntimeHeatmapBuilder()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Create the ProceduralMeshComponent
	RuntimeHeatmapMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RuntimeHeatmapMeshComponent"));

	// Set the ProceduralMeshComponent as the RootComponent
	RootComponent = RuntimeHeatmapMeshComponent;
}

// Called when the game starts or when spawned
void ARuntimeHeatmapBuilder::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ARuntimeHeatmapBuilder::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ARuntimeHeatmapBuilder::GenerateHeatmapMesh(TArray<FVector> InVertices)
{
}

bool ARuntimeHeatmapBuilder::EnsureMeshIsRectangular(TArray<FVector>& InVertices)
{
	return false;
}

