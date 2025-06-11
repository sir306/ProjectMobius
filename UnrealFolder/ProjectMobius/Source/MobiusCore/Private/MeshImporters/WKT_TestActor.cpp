// Fill out your copyright notice in the Description page of Project Settings.


#include "MeshImporters/WKT_TestActor.h"

#include "AsyncAssimpMeshLoader.h"
#include "BuildingGenerator/RuntimeMeshBuilder.h"
#include "Kismet/GameplayStatics.h"


// Sets default values
AWKT_TestActor::AWKT_TestActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AWKT_TestActor::BeginPlay()
{
	Super::BeginPlay();
	

	TArray<FString> WktList;
	FString OutErrorMessage;

	// now we can use the WKTParser to parse WKT data from a string
	FString OutFileData;
	FString FilePath = FPaths::ProjectDir() / TEXT("UnitTestSampleData\\WKT_Test\\double_bottleneck.wkt");

	
	UAsyncAssimpMeshLoader* AssimpLoader = NewObject<UAsyncAssimpMeshLoader>();
	AssimpLoader->MeshLoaderRunnable = new FAssimpMeshLoaderRunnable(FilePath);
	// get the runtime mesh builder that is already in world
	ARuntimeMeshBuilder* RuntimeMeshBuilder = Cast<ARuntimeMeshBuilder>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ARuntimeMeshBuilder::StaticClass()));
	if (!RuntimeMeshBuilder)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to spawn RuntimeMeshBuilder actor"));
		return;
	}
	RuntimeMeshBuilder->AsyncAssimpLoader = AssimpLoader;
	AssimpLoader->MeshLoaderRunnable->OnLoadMeshDataComplete.AddDynamic(RuntimeMeshBuilder, &ARuntimeMeshBuilder::GetTheAsyncMeshData);
	
	
}

// Called every frame
void AWKT_TestActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

