// Fill out your copyright notice in the Description page of Project Settings.


#include "MeshImporters/WKT_TestActor.h"

#include "MeshImporters/WKT_FileParser.h"


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

	// Example usage of WKT_FileParser
	UWKT_FileParser* WKTParser = NewObject<UWKT_FileParser>();

	TArray<FString> WktList;
	FString OutErrorMessage;
	FString FilePath = FPaths::ProjectDir() / TEXT("UnitTestSampleData\\WKT_Test\\test.json");
	// load example file
	if (WKTParser->LoadJsonWktFile(FilePath, WktList, OutErrorMessage))
	{
		for (FString line : WktList)
		{
			TArray<FVector2D> Points = WKTParser->ParseWKTData(line,OutErrorMessage);
         
			for (const FVector2D& P : Points)
			{
				UE_LOG(LogTemp, Log, TEXT("Parsed Point: X=%f, Y=%f"), P.X, P.Y);
			}
         
			// Optional: Convert to 3D FVector if needed
			TArray<FVector> WorldPoints;
			for (const FVector2D& P : Points)
			{
				WorldPoints.Add(FVector(P.X, P.Y, 0.f));
			}

			// DEBUG draw points
			for (const FVector& P : WorldPoints)
			{
				DrawDebugPoint(GetWorld(), P, 10.f, FColor::Red, true);
			}
		}
		
	}
	
	
}

// Called every frame
void AWKT_TestActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

