// Fill out your copyright notice in the Description page of Project Settings.


#include "MeshImporters/WKT_TestActor.h"

#include "AsyncAssimpMeshLoader.h"
#include "BuildingGenerator/RuntimeMeshBuilder.h"
#include "Kismet/GameplayStatics.h"
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

	// now we can use the WKTParser to parse WKT data from a string
	FString OutFileData;
	FilePath = FPaths::ProjectDir() / TEXT("UnitTestSampleData\\WKT_Test\\double_bottleneck.wkt");

	if (WKTParser->LoadWKTFile(FilePath, OutFileData, OutErrorMessage))
	{
		// if the outfiledata contains geometry collections use correct parser
		TArray<TArray<FVector2D>> Geometries;
		if (WKTParser->ParseGeometryCollectionWkt(OutFileData, Geometries, OutErrorMessage))
		{
			for (const TArray<FVector2D>& Geometry : Geometries)
			{
				for (const FVector2D& P : Geometry)
				{
					UE_LOG(LogTemp, Log, TEXT("Parsed Geometry Point: X=%f, Y=%f"), P.X, P.Y);
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to parse geometry collection: %s"), *OutErrorMessage);
		}

		// loop through the parsed WKT data
		for (TArray<FVector2D>& Geometry : Geometries)
		{
			
			TArray<FVector> WorldPoints;
			for (const FVector2D& P : Geometry)
			{
				WorldPoints.Add(FVector(P.X, P.Y, 10.f)); // raise above first file data
			}

			// DEBUG draw points
			for (const FVector& P : WorldPoints)
			{
				DrawDebugPoint(GetWorld(), P, 10.f, FColor::Green, true);
			}

			// draw lines from 0 to 1, 1 to 2, etc.
			for (int32 i = 0; i < WorldPoints.Num() - 1; ++i)
			{
				DrawDebugLine(GetWorld(), WorldPoints[i], WorldPoints[i + 1], FColor::Blue, true);
			}

			// Test the assimp triangulation method
			TArray<FVector> TriVerts;
			TArray<FIntVector> Triangles = UAsyncAssimpMeshLoader::TriangulateWktPolygon(Geometry, TriVerts);

			// Visualize triangles
			for (const FIntVector& Tri : Triangles)
			{
				DrawDebugLine(GetWorld(), TriVerts[Tri.X] * 100.0f, TriVerts[Tri.Y] * 100.0f, FColor::Orange, true);
				DrawDebugLine(GetWorld(), TriVerts[Tri.Y] * 100.0f, TriVerts[Tri.Z] * 100.0f, FColor::Orange, true);
				DrawDebugLine(GetWorld(), TriVerts[Tri.Z] * 100.0f, TriVerts[Tri.X] * 100.0f, FColor::Orange, true);

			}
			// Generate OBJ with double-sided wall extrusion (1 meter = 100 units)
			FString OBJ = TEXT("o WKTPolygonWithWalls\n");

			int32 NumPoints = Geometry.Num();
			TArray<FVector2D> Scaled = Geometry;
			for (FVector2D& P : Scaled)
				P *= 100.0f;

			// Step 1: Add base (Z=0) and top (Z=100) vertices
			for (const FVector2D& P : Scaled)
			{
				OBJ += FString::Printf(TEXT("v %f %f 0.0\n"), P.X, P.Y);       // base ring
			}
			for (const FVector2D& P : Scaled)
			{
				OBJ += FString::Printf(TEXT("v %f %f 100.0\n"), P.X, P.Y);     // top ring
			}

			// Step 2: Add bottom face (original polygon winding)
			OBJ += TEXT("f");
			for (int32 i = 1; i <= NumPoints; ++i)
			{
				OBJ += FString::Printf(TEXT(" %d"), i);
			}
			OBJ += TEXT("\n");

			// Step 3: Add top face (reverse winding for top)
			OBJ += TEXT("f");
			for (int32 i = NumPoints; i > 0; --i)
			{
				OBJ += FString::Printf(TEXT(" %d"), i + NumPoints);
			}
			OBJ += TEXT("\n");

			// Step 4: Add side wall faces (double-sided)
			for (int32 i = 0; i < NumPoints; ++i)
			{
				int32 A = i + 1;
				int32 B = ((i + 1) % NumPoints) + 1;
				int32 ATop = A + NumPoints;
				int32 BTop = B + NumPoints;

				// Outer face (clockwise)
				OBJ += FString::Printf(TEXT("f %d %d %d\n"), A, B, BTop);
				OBJ += FString::Printf(TEXT("f %d %d %d\n"), A, BTop, ATop);

				// Inner face (reversed winding)
				OBJ += FString::Printf(TEXT("f %d %d %d\n"), BTop, B, A);
				OBJ += FString::Printf(TEXT("f %d %d %d\n"), ATop, BTop, A);
			}


			UAsyncAssimpMeshLoader* AssimpLoader = NewObject<UAsyncAssimpMeshLoader>();
			AssimpLoader->MeshLoaderRunnable = new FAssimpMeshLoaderRunnable(OBJ, true);
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

		
	}
	
	
}

// Called every frame
void AWKT_TestActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

