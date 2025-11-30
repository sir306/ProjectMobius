// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.  
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,  
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL  
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR  
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING  
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS  
 * IN THE SOFTWARE.
 */

#include "BuildingGenerator/RuntimeMeshBuilder.h"
#include "ProceduralMeshComponent.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Kismet/KismetMathLibrary.h"
#include "AsyncAssimpMeshLoader.h"
#include "DatasmithRuntime.h"
#include "DatasmithRuntimeBlueprintLibrary.h"
#include "DatasmithSceneFactory.h"
#include "DirectLink/DatasmithSceneReceiver.h"
#include "Engine/StaticMeshActor.h"
#include "DatasmithAssetUserData.h"
#include "Actors/FlowCounter.h"
#include "Components/FlowCounterSpawnerComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceConstant.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Subsystems/LoadingSubsystem.h"
#include "Subsystems/MobiusStartupLoggerSubsystem.h"
#include "Engine/Engine.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"

namespace
{
	UMobiusStartupLoggerSubsystem* GetStartupLogger()
	{
		return GEngine ? GEngine->GetEngineSubsystem<UMobiusStartupLoggerSubsystem>() : nullptr;
	}
}

// Sets default values
ARuntimeMeshBuilder::ARuntimeMeshBuilder() :
        MobiusProceduralMeshComponent(nullptr),
        DatasmithMaterialsMap(TMap<TWeakObjectPtr<UStaticMeshComponent>, FDatasmithMaterials>()),
        MaterialCache(this)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// Create the ProceduralMeshComponent
        MobiusProceduralMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MobiusProceduralMeshComponent"));

        // Set the ProceduralMeshComponent as the RootComponent
        RootComponent = MobiusProceduralMeshComponent;

        MobiusProceduralMeshComponent->bRenderInMainPass = true;
        MobiusProceduralMeshComponent->bUseAsyncCooking = false;
        MobiusProceduralMeshComponent->bUseComplexAsSimpleCollision = false;
        MobiusProceduralMeshComponent->bSelectable = true;
        MobiusProceduralMeshComponent->Mobility = EComponentMobility::Movable;
        MobiusProceduralMeshComponent->SetSimulatePhysics(false);
        MobiusProceduralMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	

	FlowCounterSpawnerComponent = CreateDefaultSubobject<UFlowCounterSpawnerComponent>(TEXT("FlowCounterSpawnerComponent"));
	
}

// Called when the game starts or when spawned
void ARuntimeMeshBuilder::BeginPlay()
{
	const double BeginPlayStart = FPlatformTime::Seconds();
	UMobiusStartupLoggerSubsystem* StartupLogger = GetStartupLogger();
	if (StartupLogger)
	{
		StartupLogger->EnqueueLogMessage(TEXT("RuntimeMeshBuilder BeginPlay started"));
	}

	ON_SCOPE_EXIT
	{
		if (StartupLogger)
		{
			const double DurationMs = (FPlatformTime::Seconds() - BeginPlayStart) * 1000.0;
			StartupLogger->EnqueueTimedMessage(TEXT("RuntimeMeshBuilder::BeginPlay"), DurationMs);
		}
	};

	Super::BeginPlay();

	// As mesh generation needs to happen when the game starts and the world is required the delegate is bound here
	if(GetWorld())
	{
		UProjectMobiusGameInstance* GameInst = GetMobiusGameInstance(GetWorld());
		if(GameInst)
		{
			GameInst->OnMeshFileChanged.AddDynamic(this, &ARuntimeMeshBuilder::UpdateMeshFileName);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Game Instance is not valid, Mesh Generation will not work"));
		}
	}
	else
	{
		// TODO: Implement error to user that mesh generator will not work
		UE_LOG(LogTemp, Error, TEXT("World is not valid, Mesh Generation will not work"));
	}
	
	// Assign the Flow Counter class to auto spawn
	if (FlowCounterSpawnerComponent)
	{
		FlowCounterSpawnerComponent->FlowCounterClass = FlowCounterToAutoSpawn;
	}
}

void ARuntimeMeshBuilder::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clear any pending items
	PendingCollisionEnable.Reset();
	PendingDatasmithMeshes.Reset();
	
	Super::EndPlay(EndPlayReason);
}

// Called every frame
void ARuntimeMeshBuilder::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	// Don’t touch queues while we’re in the middle of a reset
	if (bIsResettingForNewLoad)
	{
		return;
	}
	
	ProcessPendingDatasmithMeshes(DeltaTime);
	ProcessPendingCollisionEnables(DeltaTime);
}

void ARuntimeMeshBuilder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// If in game mode, don't clear the mesh
	if(!GetWorld()->GetBegunPlay())
	{
		MobiusProceduralMeshComponent->ClearAllMeshSections();
	}
	
	
	// moving the mesh is causing headaches and memory issues
	
}

void ARuntimeMeshBuilder::GenerateMobiusMesh(TArray<FVector> InVertices, TArray<int32> InTriangles, TArray<FVector> InNormals)
{
        const bool bHasValidVertexCounts = InVertices.Num() > 0 && InNormals.Num() == InVertices.Num();
        const bool bHasValidTriangles   = InTriangles.Num() > 0 && InTriangles.Num() % 3 == 0;

        if (!bHasValidVertexCounts || !bHasValidTriangles)
        {
                UE_LOG(LogTemp, Error, TEXT("GenerateMobiusMesh received invalid mesh data (Vertices: %d, Normals: %d, Triangles: %d)"), InVertices.Num(), InNormals.Num(), InTriangles.Num());
                return;
        }

        ResetMeshCollisionAndPhysics();

        MobiusProceduralMeshComponent->CreateMeshSection(0, InVertices, InTriangles, InNormals, TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);
}

void ARuntimeMeshBuilder::GetMeshDataFromFile(const FRotator MeshRotationOffset)
{
	const double SyncLoadStart = FPlatformTime::Seconds();
	UMobiusStartupLoggerSubsystem* StartupLogger = GetStartupLogger();
	if (StartupLogger)
	{
		StartupLogger->EnqueueLogMessage(FString::Printf(TEXT("RuntimeMeshBuilder::GetMeshDataFromFile start -> %s"), *MeshFileName));
	}

	ON_SCOPE_EXIT
	{
		if (StartupLogger)
		{
			const double DurationMs = (FPlatformTime::Seconds() - SyncLoadStart) * 1000.0;
			StartupLogger->EnqueueTimedMessage(TEXT("RuntimeMeshBuilder::GetMeshDataFromFile"), DurationMs);
		}
	};

        if (MeshFileName.IsEmpty())
        {
                UE_LOG(LogTemp, Error, TEXT("MeshFileName is empty. Aborting mesh load."));
                return;
        }

	// TEST
	//FString FilePath = "C:\\Users\\User_VR4\\Desktop\\WORK\\ProjectMobius\\ProjectMobius\\TestData\\TechnicalSchool1000People\\Technical-School-For-Lab-3D.fbx";
	//FString FilePath = "C:\\Users\\User_VR4\\Desktop\\WORK\\ProjectMobius\\ProjectMobius\\TestData\\ISO-Test-8-r25-ifc2x3-to.obj";
	//FString FilePath = "C:\\Users\\User_VR4\\Desktop\\WORK\\ProjectMobius\\ProjectMobius\\TestData\\ISO-Test-8-r25.fbx";
	//FString FilePath = "D:\\1_Work\\Mobius\\ProjectMobius\\TestData\\ISO-Test-1-3DView.fbx";
	//FString FilePath = "D:\\1_Work\\Mobius\\ProjectMobius\\TestData\\ISO-Test-1-2x3.ifc";
	int32 SectionCount = 0;
	FString ErrorMessageCode;
	TArray<FVector> MVertices;
	TArray<int32> MFaces;
	TArray<FVector> MNormals;
	TArray<FVector2D> MUV;
	TArray<FVector> MTangents;
	
	//MobiusProceduralMeshComponent->ClearAllMeshSections(); see comments below
	// this could be the memory issue as it clears and will mark as dirty and will be updated in the next frame
	// the create mesh section also does the same thing and it could be causing the issue

        if(IAssimpInterface::OpenMeshFileGetWithAssimp(MeshFileName, SectionCount, ErrorMessageCode, MVertices, MFaces, MNormals, MUV, MTangents, MeshRotationOffset))
        {
                UE_LOG(LogTemp, Warning, TEXT("Successfully opened the mesh file"));
                UE_LOG(LogTemp, Warning, TEXT("Section Count: %d"), SectionCount);
                UE_LOG(LogTemp, Warning, TEXT("Vertices Count: %d"), MVertices.Num());
                UE_LOG(LogTemp, Warning, TEXT("Faces Count: %d"), MFaces.Num());
                UE_LOG(LogTemp, Warning, TEXT("Normals Count: %d"), MNormals.Num());
                UE_LOG(LogTemp, Warning, TEXT("Tangents Count: %d"), MTangents.Num());
                UE_LOG(LogTemp, Warning, TEXT("UV Count: %d"), MUV.Num());

                if (SectionCount == 0 || MVertices.Num() == 0 || MFaces.Num() == 0)
                {
                        UE_LOG(LogTemp, Error, TEXT("Mesh file returned no usable data (Sections: %d, Vertices: %d, Faces: %d)."), SectionCount, MVertices.Num(), MFaces.Num());
                        return;
                }

                // A mesh section should only be created if successful
                MobiusProceduralMeshComponent->CreateMeshSection_LinearColor(0, MVertices, MFaces, MNormals, MUV,
                                                                             TArray<FLinearColor>(),
		                                                             TArray<FProcMeshTangent>(),
		                                                             false);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to open the mesh file: %s"), *ErrorMessageCode);
	}
	// // Lambda function to rotate the vectors by 90 degrees
	// auto RotateVector = [](const FVector& InVector) -> FVector
	// {
	// 	return UKismetMathLibrary::LessLess_VectorRotator(InVector, FRotator(0.0f, 0.0f, -180.0f));
	// };
	// for(int32 Index = 0; Index < MVertices.Num(); Index++)
	// {
	// 	if(MVertices.IsValidIndex(Index))
	// 	{
	// 		MVertices[Index] = RotateVector(MVertices[Index]);
	// 	}
	// 	if(MNormals.IsValidIndex(Index))
	// 	{
	// 		MNormals[Index] = RotateVector(MNormals[Index]);
	// 	}
	// 	if(MTangents.IsValidIndex(Index))
	// 	{
	// 		MTangents[Index] = RotateVector(MTangents[Index]);
	// 	}
	// }
	
}

void ARuntimeMeshBuilder::ResetMeshCollisionAndPhysics()
{
        // 1. Turn off async cooking for deterministic behavior here (optional but recommended), ensures it will update
        MobiusProceduralMeshComponent->bUseAsyncCooking = false;

	// 2. Clear all generated geometry + convex collision
	MobiusProceduralMeshComponent->ClearAllMeshSections();          // Empties sections + UpdateCollision()
	MobiusProceduralMeshComponent->ClearCollisionConvexMeshes();    // Empties convex + UpdateCollision()

	// 3. Make sure the collision setup itself is empty and re-cooked
	if (UBodySetup* BodySetup = MobiusProceduralMeshComponent->GetBodySetup())
	{
		// Wipe any residual simple collision shapes
		BodySetup->AggGeom.EmptyElements();

		// Throw away any cooked data and rebuild with the now-empty geometry
		BodySetup->InvalidatePhysicsData();
		BodySetup->CreatePhysicsMeshes();
	}

	// 4. Force physics state to be recreated on the component
	MobiusProceduralMeshComponent->RecreatePhysicsState();

}

void ARuntimeMeshBuilder::UpdateMeshFileName()
{
	const double UpdateStart = FPlatformTime::Seconds();
	UMobiusStartupLoggerSubsystem* StartupLogger = GetStartupLogger();
	ON_SCOPE_EXIT
	{
		if (StartupLogger)
		{
			const double DurationMs = (FPlatformTime::Seconds() - UpdateStart) * 1000.0;
			StartupLogger->EnqueueTimedMessage(TEXT("RuntimeMeshBuilder::UpdateMeshFileName"), DurationMs);
		}
	};

	// Mark that we're about to tear things down
	bIsResettingForNewLoad = true;

	// Drop any queued work that still references old components
	PendingCollisionEnable.Reset();
	PendingDatasmithMeshes.Reset();
    MaterialCache.Reset();
	
	// Get the game instance
	UProjectMobiusGameInstance* GameInst = GetMobiusGameInstance(GetWorld());

	// Assign the new file path to the mesh file name
	MeshFileName = GameInst->GetSimulationMeshFilePath();

	// get the clean file name
	FString LoadingFileName = GameInst->GetSimulationMeshFileName();

	if (StartupLogger)
	{
		StartupLogger->EnqueueLogMessage(FString::Printf(TEXT("RuntimeMeshBuilder::UpdateMeshFileName -> %s"), *MeshFileName));
	}

	// Get the loading subsystem
	auto LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>();

	if(LoadingSubsystem)
	{
		// Start the load widget
		LoadingSubsystem->SetLoadingUnknownDuration(true, TEXT("Loading Geometry: " + LoadingFileName));
	}

	// Make sure no residual data of the procedural mesh comp exists  
	ResetMeshCollisionAndPhysics();
	
    if (RuntimeDatasmithAnchor)
    {
    	auto ActorToDestroy = RuntimeDatasmithAnchor;
        ActorToDestroy->Destroy();
        RuntimeDatasmithAnchor = nullptr;
    }

    // Double-flush queues in case async work enqueued new items during teardown
    PendingCollisionEnable.Reset();
    PendingDatasmithMeshes.Reset();
	
	DatasmithMaterialsMap.Empty();
	bIsDatasmithAsset = false;

	// Remove any flow counters, as the mesh is changing
	FlowCounterSpawnerComponent->RemoveAllFlowCounters();

	// As we are now able to use Datasmith assets we need to check if the file is a .udatasmith file
	if(MeshFileName.Contains(".udatasmith") || MeshFileName.Contains(".ifc"))
	{
		// check world is valid
		if(!CheckStillInWorld())
		{
			UE_LOG(LogTemp, Error, TEXT("World is not valid"));
			return;
		}

		// set the flag to indicate this is a datasmith file
		bIsDatasmithAsset = true;
		
		// spawn a runtime datasmtih actor to load the mesh
		RuntimeDatasmithAnchor = GetWorld()->SpawnActor<ADatasmithRuntimeActor>();
		
		if(MeshFileName.Contains(".udatasmith"))
		{
			// is the runtime datasmith anchor valid
			if(RuntimeDatasmithAnchor == nullptr)
			{
				UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
				return;
			}
			// stack trace to show this is the datasmith import
			TRACE_CPUPROFILER_EVENT_SCOPE_STR("Datasmith Import");

			// import options
			FDatasmithRuntimeImportOptions ImportOptions;// TODO: set the import options

			// For thesis, I don't want collisions
			ImportOptions.BuildCollisions = ECollisionEnabled::Type::NoCollision;
			ImportOptions.CollisionType = ECollisionTraceFlag::CTF_UseSimpleAndComplex;
			ImportOptions.TessellationOptions.bUseCADKernel = true;
			ImportOptions.TessellationOptions.StitchingTechnique = EDatasmithCADStitchingTechnique::StitchingHeal;
			RuntimeDatasmithAnchor->ImportOptions = ImportOptions;
			
			// import the mesh data into the anchor
			RuntimeDatasmithAnchor->LoadFile(MeshFileName);

			// Async task to check if the scene is loaded
			Async(EAsyncExecution::ThreadPool, [this]()
			      {
				      FPlatformProcess::Sleep(5.0f);
				      // log building and receiving
				      UE_LOG(LogTemp, Warning, TEXT("1Building: %d, Receiving: %d"), RuntimeDatasmithAnchor->bBuilding, RuntimeDatasmithAnchor->IsReceiving());
				      while (RuntimeDatasmithAnchor->bBuilding || RuntimeDatasmithAnchor->IsReceiving())
				      {
					      // log building and receiving
					      UE_LOG(LogTemp, Warning, TEXT("2Building: %d, Receiving: %d"), RuntimeDatasmithAnchor->bBuilding, RuntimeDatasmithAnchor->IsReceiving());
					      // sleep for 0.05 seconds
					      FPlatformProcess::Sleep(0.05f);
				      }
				      // log building and receiving
				      UE_LOG(LogTemp, Warning, TEXT("3Building: %d, Receiving: %d"), RuntimeDatasmithAnchor->bBuilding, RuntimeDatasmithAnchor->IsReceiving());

			      }, [this]()
			      {
			      	
				      AsyncTask(ENamedThreads::GameThread,[this] {
					      CreateDatasmithMaterials();
					      // lights imported by datasmith can cause performance issues, so may need to disable cast shadows or reduce
					      // the size of point light radius and intensitys
				      	
				      	bIsResettingForNewLoad = false;
				      });
				
			      });

			
			

			// wait for the scene to be loaded for now delay for 5 seconds
			//FTimerHandle TimerHandle;
			//GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &ARuntimeMeshBuilder::TestDatasmithMaterialSetup, 15.0f, false);
		}
		else
		{
			// import options
			FDatasmithRuntimeImportOptions ImportOptions;// TODO: set the import options

			//ImportOptions.TessellationOptions.bUseCADKernel = true;
			
			RuntimeDatasmithAnchor->ImportOptions = ImportOptions;

			// // construct new object UDatasmithSceneElement
			// UDatasmithSceneElement* DatasmithSceneElement = NewObject<UDatasmithSceneElement>();
			//
			// UDatasmithSceneElement* ConstructedScene = DatasmithSceneElement->ConstructDatasmithSceneFromFile(MeshFileName);
			//
			// auto Imported = ConstructedScene->ImportScene("/Game/");
			//
			// auto CreatedScene = FDatasmithSceneFactory::CreateScene(*Imported.Scene.GetName());
			//
			// RuntimeDatasmithAnchor->SetScene(CreatedScene);
			// RuntimeDatasmithAnchor->ApplyNewScene();
			// RuntimeDatasmithAnchor->MarkComponentsRenderStateDirty();
		}
		
		
	}
	// not a datasmith file so we can load the mesh as normal
	else
	{		
		//GetMeshDataFromFile(FRotator(0.0f, 0.0f, 90.0f));
		AsyncUpdateMesh(MeshFileName);
		bIsResettingForNewLoad = false;
	}
	
	
}

void ARuntimeMeshBuilder::AsyncUpdateMesh(const FString PathToMesh)
{
	const double AsyncUpdateStart = FPlatformTime::Seconds();
	UMobiusStartupLoggerSubsystem* StartupLogger = GetStartupLogger();
	if (StartupLogger)
	{
		StartupLogger->EnqueueLogMessage(FString::Printf(TEXT("RuntimeMeshBuilder::AsyncUpdateMesh start -> %s"), *PathToMesh));
	}

	ON_SCOPE_EXIT
	{
		if (StartupLogger)
		{
			const double DurationMs = (FPlatformTime::Seconds() - AsyncUpdateStart) * 1000.0;
			StartupLogger->EnqueueTimedMessage(TEXT("RuntimeMeshBuilder::AsyncUpdateMesh"), DurationMs);
		}
	};

	// As this is game thread dependent we need to ensure this is called on the game thread and return if not
	if(!IsInGameThread())
	{
		UE_LOG(LogTemp, Error, TEXT("AsyncUpdateMesh must be called on the game thread and after the game has started"));
		return;
	}

	// set the mesh being loaded flag
	bMeshBeingBuilt = true;
	
	AsyncAssimpLoader = NewObject<UAsyncAssimpMeshLoader>();

	// check if runnable is null and if not then delete it
	if (auto* ExistingRunnable = AsyncAssimpLoader->MeshLoaderRunnable)
	{
		AsyncAssimpLoader->MeshLoaderRunnable = nullptr;

		// Stop the existing runnable
		ExistingRunnable->Stop();
		ExistingRunnable->Exit();

		AsyncTask(ENamedThreads::GameThread, [&ExistingRunnable]
		{
			// Delete the existing runnable on the game thread
			delete ExistingRunnable;
		});
	}
	
	// Create the runnable
	AsyncAssimpLoader->MeshLoaderRunnable = new FAssimpMeshLoaderRunnable(PathToMesh);
	AsyncAssimpLoader->MeshLoaderRunnable->OnLoadMeshDataComplete.AddDynamic(this, &ARuntimeMeshBuilder::GetTheAsyncMeshData);
}

void ARuntimeMeshBuilder::GetTheAsyncMeshData()
{
	const double ReceiveStart = FPlatformTime::Seconds();
	UMobiusStartupLoggerSubsystem* StartupLogger = GetStartupLogger();
	if (StartupLogger)
	{
		StartupLogger->EnqueueLogMessage(TEXT("RuntimeMeshBuilder::GetTheAsyncMeshData received"));
	}

	ON_SCOPE_EXIT
	{
		if (StartupLogger)
		{
			const double DurationMs = (FPlatformTime::Seconds() - ReceiveStart) * 1000.0;
			StartupLogger->EnqueueTimedMessage(TEXT("RuntimeMeshBuilder::GetTheAsyncMeshData"), DurationMs);
		}
	};

	// TODO:check valid data
	//AsyncAssimpLoader->MeshLoaderRunnable

	if(AsyncAssimpLoader == nullptr || AsyncAssimpLoader->MeshLoaderRunnable == nullptr)// important to check if the runnable is valid
	{
		/** TODO: if we get here and the runnable is not valid or the async loader is not valid we need to handle this
			As this means a thread has started but the object has lost its reference and we need to clean up the thread
			as we know the name of the thread we can look for it this way and clean it up
		*/
		return;
	}
	// Enable collisions so we can perform click interactions for flow counters
	MobiusProceduralMeshComponent->bUseComplexAsSimpleCollision = true;
	MobiusProceduralMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MobiusProceduralMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	MobiusProceduralMeshComponent->SetSimulatePhysics(false);

	
	// A mesh section should only be created if successful
	MobiusProceduralMeshComponent->CreateMeshSection_LinearColor(0, AsyncAssimpLoader->MeshLoaderRunnable->Vertices,
	                                                             AsyncAssimpLoader->MeshLoaderRunnable->Faces,
	                                                             AsyncAssimpLoader->MeshLoaderRunnable->Normals,
	                                                             AsyncAssimpLoader->MeshLoaderRunnable->UV,
	                                                             TArray<FLinearColor>(),
	                                                             TArray<FProcMeshTangent>(),
	                                                             true/*set to true so we can use collisions - at a small cost of performance*/);

	// The loader is no longer needed so we can stop the thread
	AsyncAssimpLoader->MeshLoaderRunnable->Stop();
	
	// nullptr the runnable to free up memory
	AsyncAssimpLoader->MeshLoaderRunnable = nullptr;

	// if the material property is set then we want to apply our material to the mesh
	if(MobiusMaterialInstanceDynamic != nullptr)
	{
		MobiusProceduralMeshComponent->SetMaterial(0, MobiusMaterialInstanceDynamic);
	}

	// Mesh has been built so we can set the flag to false
	bMeshBeingBuilt = false;

	// The origin we want to broadcast is the smallest location of the mesh bounds as the mesh generator for the heatmap
	// works from left to right and bottom to top
	FVector HeatmapOrigin = MobiusProceduralMeshComponent->Bounds.Origin - MobiusProceduralMeshComponent->Bounds.BoxExtent;

	// Broadcast that the mesh has been built
	OnMeshBuilt.Broadcast(HeatmapOrigin, MobiusProceduralMeshComponent->Bounds.BoxExtent);

	// check if runnable is null and if not then delete it
	if (auto* ExistingRunnable = AsyncAssimpLoader->MeshLoaderRunnable)
	{
		AsyncAssimpLoader->MeshLoaderRunnable = nullptr;

		// Stop the existing runnable
		ExistingRunnable->Stop();
		ExistingRunnable->Exit();

		AsyncTask(ENamedThreads::GameThread, [&ExistingRunnable]
		{
			// Delete the existing runnable on the game thread
			delete ExistingRunnable;
		});
	}
	EndLoadingWidget();
}

void ARuntimeMeshBuilder::UpdateMeshMaterial(UMaterialInstanceDynamic* InMaterial)
{
	// Check input is valid
	if(InMaterial == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Material is not valid"));
		return;
	}
	MobiusMaterialInstanceDynamic = InMaterial;

	
	// Mesh no longer being built so we can set the material
	SetMaterialOnMesh();
}

void ARuntimeMeshBuilder::SetDatasmithMeshToUseNonModifiedMaterials(bool bUseNonModifiedMaterials)
{
	// log called
	UE_LOG(LogTemp, Warning, TEXT("SetDatasmithMeshToUseNonModifiedMaterials Called"));
	
	// check if the datasmith anchor is valid and that the map is not empty
	if(RuntimeDatasmithAnchor == nullptr || DatasmithMaterialsMap.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("Datasmith Anchor is not valid or the Datasmith Materials Map is empty"));
		return;
	}

	// loop over the map and set the materials to use the non modified materials
	for(auto& DatasmithMaterial : DatasmithMaterialsMap)
	{
		// loop over the materials and set the material
		for(auto Material : DatasmithMaterial.Value.MeshMaterials)
		{
			// set the material to use the non modified material
			Material->SetScalarParameterValue(FName("Use Modified Material"), bUseNonModifiedMaterials ? 0.0f : 1.0f);
		}

		// get num materials on the mesh component and set the material to use the non modified material
		for (int32 Index = 0; Index < DatasmithMaterial.Key->GetNumMaterials(); Index++)
		{
			
			UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(DatasmithMaterial.Key->GetMaterial(Index));

			if (Mat != nullptr)
			{
				Mat->SetScalarParameterValue(FName("Use Modified Material"), bUseNonModifiedMaterials ? 0.0f : 1.0f);
			}
		}
		
	}
	UE_LOG(LogTemp, Warning, TEXT("SetDatasmithMeshToUseNonModifiedMaterials Finished"));
}

void ARuntimeMeshBuilder::SetDatasmithMeshToTranslucentMaterials()
{
	UE_LOG(LogTemp, Error, TEXT("SetDatasmithMeshToTranslucentMaterials Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		// get num materials on the mesh component and set the material to use the non modified material
		for (int32 Index = 0; Index < DatasmithMaterialMap.Key->GetNumMaterials(); Index++)
		{
			// set the material to use the translucent material
			DatasmithMaterialMap.Key->SetMaterial(Index,DatasmithMaterialMap.Value.MeshMaterials[Index * 2 + 1]);
		}
	}
	UE_LOG(LogTemp, Error, TEXT("SetDatasmithMeshToTranslucentMaterials Finished"));
}

void ARuntimeMeshBuilder::SetDatasmithMeshToSolidMaterials()
{
	UE_LOG(LogTemp, Error, TEXT("SetDatasmithMeshToSolidMaterials Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		// get num materials on the mesh component and set the material to use the non modified material
		for (int32 Index = 0; Index < DatasmithMaterialMap.Key->GetNumMaterials(); Index++)
		{
			// set the material to use the translucent material
			DatasmithMaterialMap.Key->SetMaterial(Index,DatasmithMaterialMap.Value.MeshMaterials[Index * 2]);
		}
	}
}

void ARuntimeMeshBuilder::UpdateDatasmithMeshOpacity(float Opacity)
{
	UE_LOG(LogTemp, Warning, TEXT("UpdateDatasmithMeshOpacity Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the opacity amount
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		for (auto Material : DatasmithMaterialMap.Value.MeshMaterials)
		{
			// set the opacity amount
			Material->SetScalarParameterValue(FName("OpacityAmount"), Opacity);
		}
	}
}

void ARuntimeMeshBuilder::BoxDissolveDatasmithMesh(bool bDissolve)
{
	UE_LOG(LogTemp, Warning, TEXT("BoxDissolveDatasmithMesh Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		for (auto Material : DatasmithMaterialMap.Value.MeshMaterials)
		{
			// set the material to use the translucent material
			Material->SetScalarParameterValue(FName("Box Dissolve?"), bDissolve ? 1.0f : 0.0f);
		}
	}
}

void ARuntimeMeshBuilder::SetDatasmithToUseModifiedColour(bool bUseModifiedColour, FLinearColor NewColour)
{
	UE_LOG(LogTemp, Warning, TEXT("SetDatasmithToUseModifiedColour Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		for (auto Material : DatasmithMaterialMap.Value.MeshMaterials)
		{
			// set the material shading to use the modified colour material or not
			Material->SetScalarParameterValue(FName("Use Modified Colour"), bUseModifiedColour ? 1.0f : 0.0f);

			// set the modified colour on the material
			Material->SetVectorParameterValue(FName("NewColour"), NewColour);
		}
	}
}

void ARuntimeMeshBuilder::SetDatasmithMeshToUseClearCoatMaterials(bool bUseClearCoatMaterials)
{
	UE_LOG(LogTemp, Warning, TEXT("SetDatasmithMeshToUseClearCoatMaterials Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		for (auto Material : DatasmithMaterialMap.Value.MeshMaterials)
		{
			// set the material shading to use the clear coat material or not
			Material->SetScalarParameterValue(FName("Default Lit Shading"), bUseClearCoatMaterials ? 0.0f : 1.0f);
		}
	}
}

void ARuntimeMeshBuilder::SetDatasmithDissolveMeshSizeAndOrigin(FVector Origin, FVector Extents)
{
	UE_LOG(LogTemp, Warning, TEXT("SetDatasmithDissolveMeshSizeAndOrigin Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	FLinearColor BoxLocation = FLinearColor(Origin.X, Origin.Y, Origin.Z, 1.0f);
	FLinearColor BoxBounds = FLinearColor(Extents.X, Extents.Y, Extents.Z, 1.0f);

	// loop over the map and set the box dissolve parameters
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		for (auto Material : DatasmithMaterialMap.Value.MeshMaterials)
		{
			// set the material shading to use the clear coat material or not
			Material->SetVectorParameterValue(FName("Box Dissolve Location"), BoxLocation);
			Material->SetVectorParameterValue(FName("Box Dissolve Bounds"), BoxBounds);
		}
	}
}

void ARuntimeMeshBuilder::SetDatasmithToOriginalMatStyle()
{
	UE_LOG(LogTemp, Warning, TEXT("SetDatasmithToOriginalMatStyle Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{

		for (int32 Index = 0; Index < DatasmithMaterialMap.Key->GetNumMaterials(); Index++)
		{
			// set the material shading to use the modified colour material or not
			DatasmithMaterialMap.Value.MeshMaterials[Index * 2]->SetScalarParameterValue(FName("Use Modified Colour"), 1.0f);
			DatasmithMaterialMap.Value.MeshMaterials[Index * 2]->SetScalarParameterValue(FName("Use Modified Material"), 0.0f);

			DatasmithMaterialMap.Value.MeshMaterials[Index * 2 + 1]->SetScalarParameterValue(FName("Use Modified Colour"), 1.0f);
			DatasmithMaterialMap.Value.MeshMaterials[Index * 2 + 1]->SetScalarParameterValue(FName("Use Modified Material"), 0.0f);
			
			// if is opaque then set the material to use the opaque material
			if(DatasmithMaterialMap.Value.bIsOpaque[Index])
			{
				// set the material to use the opaque material
				DatasmithMaterialMap.Key->SetMaterial(Index,DatasmithMaterialMap.Value.MeshMaterials[Index * 2]);

				// set the material shading to use default Lit Shading
				DatasmithMaterialMap.Value.MeshMaterials[Index * 2]->SetScalarParameterValue(FName("Default Lit Shading"), 1.0f);
			}
			else
			{
				// set the material to use the translucent material
				DatasmithMaterialMap.Key->SetMaterial(Index,DatasmithMaterialMap.Value.MeshMaterials[Index * 2 + 1]);

				// set the material shading to use the clear coat material
				DatasmithMaterialMap.Value.MeshMaterials[Index * 2 + 1]->SetScalarParameterValue(FName("Default Lit Shading"), 0.0f);
			}
		}
	}
}

void ARuntimeMeshBuilder::SetMaterialOnMesh()
{
	// As the mesh may not exist we check if it does
	if(MobiusProceduralMeshComponent != nullptr)
	{
		MobiusProceduralMeshComponent->SetMaterial(0, MobiusMaterialInstanceDynamic);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Procedural Mesh Component is not valid"));
	}
}

void ARuntimeMeshBuilder::EndLoadingWidget()
{
	// End the load widget
	auto LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>();

	if(LoadingSubsystem)
	{
		LoadingSubsystem->SetLoadingUnknownDuration(false, TEXT(""));
	}
}

void ARuntimeMeshBuilder::CreateDatasmithMaterials()
{
	const double DatasmithStart = FPlatformTime::Seconds();
	UMobiusStartupLoggerSubsystem* StartupLogger = GetStartupLogger();
	if (StartupLogger)
	{
		StartupLogger->EnqueueLogMessage(TEXT("RuntimeMeshBuilder::CreateDatasmithMaterials start"));
	}

	ON_SCOPE_EXIT
	{
		if (StartupLogger)
		{
			const double DurationMs = (FPlatformTime::Seconds() - DatasmithStart) * 1000.0;
			StartupLogger->EnqueueTimedMessage(TEXT("RuntimeMeshBuilder::CreateDatasmithMaterials"), DurationMs);
		}
	};

	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Datasmith Import Completed, Performing Material Setup");

    if (RuntimeDatasmithAnchor == nullptr)
    {
        UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
        return;
    }

    auto DataComps = RuntimeDatasmithAnchor->GetComponents();
    UE_LOG(LogTemp, Warning, TEXT("Number of Data Components: %d"), DataComps.Num());

    PendingDatasmithMeshes.Reset();
    bDatasmithMaterialSetupInProgress = false;

    if (DataComps.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Data Components are not valid"));
        EndLoadingWidget();
        return;
    }

    // Global bounds for all Datasmith meshes
    FBox GlobalBounds(EForceInit::ForceInit);

    for (auto DataComp : DataComps)
    {
        if (USceneComponent* SceneComp = Cast<USceneComponent>(DataComp))
        {
            TArray<USceneComponent*> ChildrenComps;
            SceneComp->GetChildrenComponents(true, ChildrenComps);

            for (USceneComponent* ChildComp : ChildrenComps)
            {
                UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(ChildComp);
                if (!MeshComp || MeshComp->IsBeingDestroyed())
                {
                    continue;
                }

                // Door metadata + flow counter queuing (unchanged)
                if (UDatasmithAssetUserData* MetaData = MeshComp->GetAssetUserData<UDatasmithAssetUserData>())
                {
                    for (auto& Data : MetaData->MetaData)
                    {
                        if (Data.Key == TEXT("Element*Category") && Data.Value == TEXT("Doors"))
                        {
                            FlowCounterSpawnerComponent->QueueDoorForFlowCounter(MeshComp);
                        }
                    }
                }

                // Accumulate world-space bounds now (independent of materials)
                const FBoxSphereBounds CompBounds = MeshComp->Bounds;
                GlobalBounds += CompBounds.GetBox();

                // Queue this mesh for later material processing
                FPendingDatasmithMesh& Pending = PendingDatasmithMeshes.AddDefaulted_GetRef();
                Pending.Mesh = MeshComp;
            }
        }
    }

    if (!GlobalBounds.IsValid || PendingDatasmithMeshes.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No valid Datasmith meshes found"));
        EndLoadingWidget();
        return;
    }

    // Compute origin/extents for the rest of the system (heatmap etc.)
    const FVector BoundsCenter = GlobalBounds.GetCenter();
    const FVector BoundsExtent = GlobalBounds.GetExtent();

    UE_LOG(LogTemp, Warning, TEXT("Datasmith GlobalBounds Center: %s Extent: %s"),
           *BoundsCenter.ToString(), *BoundsExtent.ToString());

    const FVector HeatmapOrigin = BoundsCenter - BoundsExtent;

    OnMeshBuilt.Broadcast(HeatmapOrigin, BoundsExtent);

    // We now have a queue of meshes to process over multiple frames.
    bDatasmithMaterialSetupInProgress = true;

    // Do NOT call EndLoadingWidget or BeginSpawning here; we’ll do that when the queue is empty.
}

TArray<TObjectPtr<UMaterialInstanceDynamic>> ARuntimeMeshBuilder::CreateMaterialInstances(UMaterialInterface* InMaterial, const FString& MaterialPath)
{
	TArray<TObjectPtr<UMaterialInstanceDynamic>> MaterialInstances;

	TArray<FMaterialParameterInfo> ScalarParams;
	TArray<FMaterialParameterInfo> VectorParams;
	TArray<FMaterialParameterInfo> TextureParams;
	TArray<FGuid> ScalarGuids;
	TArray<FGuid> VectorGuids;
	TArray<FGuid> TextureGuids;

	InMaterial->GetAllScalarParameterInfo(ScalarParams, ScalarGuids);
	InMaterial->GetAllVectorParameterInfo(VectorParams, VectorGuids);
	InMaterial->GetAllTextureParameterInfo(TextureParams, TextureGuids);

	UMaterialInstanceDynamic* DynamicMaterial = nullptr;
	auto* LoadedMaterial = LoadObject<UMaterialInstanceConstant>(nullptr, *MaterialPath);

	if (LoadedMaterial)
	{
		DynamicMaterial = UMaterialInstanceDynamic::Create(LoadedMaterial, this);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load the material: %s"), *MaterialPath);
		return MaterialInstances;
	}

	for (const auto& Param : ScalarParams)
	{
		float Value = 0.0f;
		if (InMaterial->GetScalarParameterValue(Param.Name, Value))
		{
			DynamicMaterial->SetScalarParameterValue(Param.Name, Value);
		}
	}

	for (const auto& Param : VectorParams)
	{
		FLinearColor Value;
		if (InMaterial->GetVectorParameterValue(Param.Name, Value))
		{
			DynamicMaterial->SetVectorParameterValue(Param.Name, Value);
		}
	}

	for (const auto& Param : TextureParams)
	{
		UTexture* Value = nullptr;
		if (InMaterial->GetTextureParameterValue(Param.Name, Value))
		{
			DynamicMaterial->SetTextureParameterValue(Param.Name, Value);
		}
	}

	MaterialInstances.Add(DynamicMaterial);
	return MaterialInstances;
}

TArray<TObjectPtr<UMaterialInstanceDynamic>> ARuntimeMeshBuilder::CreateOpaqueMaterials(UMaterialInterface* InMaterial)
{
        static const FString OpaqueMaterialPath = TEXT("MaterialInstanceConstant'/Game/01_Dev/RuntimeMeshGenerator/DatasmithMasterMaterials/MI_DatasmithOpaqueMasked.MI_DatasmithOpaqueMasked'");
        return MaterialCache.CreateMaterialInstancesUsingCache(InMaterial, OpaqueMaterialPath, true);
}

TArray<TObjectPtr<UMaterialInstanceDynamic>> ARuntimeMeshBuilder::CreateTranslucentMaterials(UMaterialInterface* InMaterial, bool bIsOpaque)
{
        const FString TranslucentMaterialPath = bIsOpaque
                                                        ? TEXT("MaterialInstanceConstant'/Game/01_Dev/RuntimeMeshGenerator/DatasmithMasterMaterials/MI_DatasmithTranslucent.MI_DatasmithTranslucent'")
                                                        : TEXT("MaterialInstanceConstant'/Game/01_Dev/RuntimeMeshGenerator/DatasmithMasterMaterials/WindowsGlass/MI_DatasmithTranslucent.MI_DatasmithTranslucent'");

        return MaterialCache.CreateMaterialInstancesUsingCache(InMaterial, TranslucentMaterialPath, false);
}

TArray<TObjectPtr<UMaterialInstanceDynamic>> ARuntimeMeshBuilder::CreateRuntimeOpaqueMaterials(UMaterialInterface* InMaterial)
{
        static const FString OpaqueMaterialPath = TEXT("MaterialInstanceConstant'/Game/01_Dev/RuntimeMeshGenerator/RuntimeDatasmithOverrides/MI_Opaque.MI_Opaque'");
        return MaterialCache.CreateMaterialInstancesUsingCache(InMaterial, OpaqueMaterialPath, true);
}

TArray<TObjectPtr<UMaterialInstanceDynamic>> ARuntimeMeshBuilder::CreateRuntimeTranslucentMaterials(UMaterialInterface* InMaterial, bool bIsOpaque)
{
        const FString TranslucentMaterialPath = bIsOpaque
                                                        ? TEXT("MaterialInstanceConstant'/Game/01_Dev/RuntimeMeshGenerator/RuntimeDatasmithOverrides/MI_Transparent.MI_Transparent'")
                                                        : TEXT("MaterialInstanceConstant'/Game/01_Dev/RuntimeMeshGenerator/RuntimeDatasmithOverrides/MI_Transparent.MI_Transparent'");

        //return CreateMaterialInstances(InMaterial, TranslucentMaterialPath); - old way without cache
        return MaterialCache.CreateMaterialInstancesUsingCache(InMaterial, TranslucentMaterialPath, false);
}

void ARuntimeMeshBuilder::EnqueueCollisionEnable(UStaticMeshComponent* Mesh)
{
	if (!IsValid(Mesh))
	{
		return;
	}

	// Avoid duplicates
	if (!PendingCollisionEnable.Contains(Mesh))
	{
		PendingCollisionEnable.Add(Mesh);
	}
}

void ARuntimeMeshBuilder::ProcessPendingCollisionEnables(float DeltaSeconds)
{
	// If we are resetting for a new load or being torn down, drop the queue and bail
    if (bIsResettingForNewLoad || IsActorBeingDestroyed() || !GetWorld())
    {
        PendingCollisionEnable.Reset();
        return;
    }

    if (PendingCollisionEnable.Num() == 0 || MaxCollisionEnablesPerFrame <= 0)
    {
        return;
    }

    const int32 MaxPerFrame = FMath::Max(1, MaxCollisionEnablesPerFrame);
    int32 ProcessedThisFrame = 0;

    // Walk from the end so removals do not shift earlier indices
    for (int32 Index = PendingCollisionEnable.Num() - 1;
         Index >= 0 && ProcessedThisFrame < MaxPerFrame;
         --Index)
    {
        TWeakObjectPtr<UStaticMeshComponent>& WeakMesh = PendingCollisionEnable[Index];

        if (!WeakMesh.IsValid())
        {
            WeakMesh.Reset();
            PendingCollisionEnable.RemoveAt(Index, 1, /*bAllowShrinking=*/false);
            continue;
        }

        UStaticMeshComponent* Mesh = WeakMesh.Get();

        // Drop anything that is clearly unsafe to touch
        if (!Mesh
            || !IsValid(Mesh)
            || Mesh->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
            || !Mesh->IsRenderStateCreated()
            || !Mesh->IsRegistered()
            || !Mesh->GetWorld()
            || Mesh->GetWorld() != GetWorld())
        {
            WeakMesh.Reset();
            PendingCollisionEnable.RemoveAt(Index, 1, /*bAllowShrinking=*/false);
            continue;
        }

        if (AActor* MeshOwner = Mesh->GetOwner())
        {
            if (MeshOwner->IsActorBeingDestroyed())
            {
                WeakMesh.Reset();
                PendingCollisionEnable.RemoveAt(Index, 1, /*bAllowShrinking=*/false);
                continue;
            }
        }
    	if (UStaticMesh* StaticMesh = Mesh->GetStaticMesh())
    	{
            if (StaticMesh->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
            {
                WeakMesh.Reset();
                PendingCollisionEnable.RemoveAt(Index, 1, /*bAllowShrinking=*/false);
                continue;
            }

    		if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
    		{
                if (BodySetup->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
                {
                    WeakMesh.Reset();
                    PendingCollisionEnable.RemoveAt(Index, 1, /*bAllowShrinking=*/false);
                    continue;
                }

                // If the body instance is not valid, avoid touching collision settings
                FBodyInstance* BodyInstance = Mesh->GetBodyInstance();
                if (!BodyInstance || !BodyInstance->IsValidBodyInstance())
                {
                    WeakMesh.Reset();
                    PendingCollisionEnable.RemoveAt(Index, 1, /*bAllowShrinking=*/false);
                    continue;
                }

    			// You can add debug logs here if you want to see which door is which
    			// UE_LOG(LogTemp, Warning, TEXT("Enabling collision on %s (%s)"),
    			//        *MeshComp->GetName(),
    			//        StaticMesh->GetPathName());

    			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    			Mesh->SetCollisionObjectType(ECC_WorldDynamic);
    			Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    			Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    		}
            else
            {
                WeakMesh.Reset();
                PendingCollisionEnable.RemoveAt(Index, 1, /*bAllowShrinking=*/false);
                continue;
            }
    	}
        else
        {
            WeakMesh.Reset();
            PendingCollisionEnable.RemoveAt(Index, 1, /*bAllowShrinking=*/false);
            continue;
        }
        WeakMesh.Reset();
    	PendingCollisionEnable.RemoveAt(Index, 1, /*bAllowShrinking=*/false);
    	++ProcessedThisFrame;
        
        // Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        // Mesh->SetCollisionObjectType(ECC_WorldDynamic);
        // Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
        // Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

       
    }
}

void ARuntimeMeshBuilder::ProcessPendingDatasmithMeshes(float DeltaSeconds)
{
	if (!bDatasmithMaterialSetupInProgress || PendingDatasmithMeshes.Num() == 0 || bIsResettingForNewLoad || IsActorBeingDestroyed())
	{
        PendingDatasmithMeshes.Reset();
		return;
	}

    if (!RuntimeDatasmithAnchor || RuntimeDatasmithAnchor->IsActorBeingDestroyed())
    {
        PendingDatasmithMeshes.Reset();
        bDatasmithMaterialSetupInProgress = false;
        return;
    }

	const int32 MaxPerFrame = FMath::Max(1, MaxDatasmithMeshesPerFrame);
	int32 Processed = 0;

	// Walk from the end so RemoveAtSwap is cheap
	for (int32 Index = PendingDatasmithMeshes.Num() - 1;
		 Index >= 0 && Processed < MaxPerFrame;
		 --Index)
	{
		UStaticMeshComponent* Mesh = PendingDatasmithMeshes[Index].Mesh.Get();
		PendingDatasmithMeshes.RemoveAtSwap(Index, 1, /*bAllowShrinking=*/false);

		if (Mesh)
		{
			BuildDatasmithMaterialsForMesh(Mesh);
		}

		++Processed;
	}

	if (PendingDatasmithMeshes.Num() == 0)
	{
		// We’re done – clear the flag and finish up the “import finished” flow.
		bDatasmithMaterialSetupInProgress = false;

		EndLoadingWidget();
		FlowCounterSpawnerComponent->BeginSpawning();
	}
}

void ARuntimeMeshBuilder::BuildDatasmithMaterialsForMesh(UStaticMeshComponent* MeshComp)
{
    if (!MeshComp
        || !IsValid(MeshComp)
        || MeshComp->IsBeingDestroyed()
        || MeshComp->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
        || !MeshComp->IsRegistered()
        || !MeshComp->IsRenderStateCreated()
        || !MeshComp->GetWorld()
        || MeshComp->GetWorld() != GetWorld()
        || (RuntimeDatasmithAnchor && RuntimeDatasmithAnchor->IsActorBeingDestroyed()))
    {
        return;
    }

    // Cache to avoid repeated string comparisons on master materials
    static TMap<UMaterial*, EDatasmithMasterType> MasterTypeCache;

    FDatasmithMaterials DatasmithMaterials;

    const int32 NumMats = MeshComp->GetNumMaterials();

	// Number of materials must be > 0
	if (NumMats <= 0)
    {
        DatasmithMaterialsMap.Remove(MeshComp);
        return;
    }
	
    DatasmithMaterials.MeshMaterials.Reserve(NumMats * 2); // opaque+translucent pair per slot
    DatasmithMaterials.bIsOpaque.Reserve(NumMats);

    MeshComp->CastShadow = false;

    TArray<TObjectPtr<UMaterialInstanceDynamic>> MaterialInstances;
    MaterialInstances.Reserve(2);

    for (int32 Index = 0; Index < MeshComp->GetNumMaterials(); ++Index)
    {
        if (!IsValid(MeshComp)
            || MeshComp->IsBeingDestroyed()
            || MeshComp->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
            || MeshComp->GetNumMaterials() <= Index)
        {
            break;
        }

        UMaterialInterface* Material = MeshComp->GetMaterial(Index);
        if (!Material)
        {
            continue;
        }

        UMaterial* ParentMaterial = Material->GetMaterial();
        if (!ParentMaterial)
        {
            continue;
        }

        // Classify master material using cached FName → enum mapping
        EDatasmithMasterType MasterType = EDatasmithMasterType::Unknown;

        if (EDatasmithMasterType* CachedType = MasterTypeCache.Find(ParentMaterial))
        {
            MasterType = *CachedType;
        }
        else
        {
            const FName MatName = ParentMaterial->GetFName();
            EDatasmithMasterType NewType = EDatasmithMasterType::Unknown;

            if (MatName == TEXT("M_TMStdOpaque"))
            {
                NewType = EDatasmithMasterType::TMStdOpaque;
            }
            else if (MatName == TEXT("M_TMStdTranslucentNEW"))
            {
                NewType = EDatasmithMasterType::TMStdTranslucent;
            }
            else if (MatName == TEXT("M_Opaque"))
            {
                NewType = EDatasmithMasterType::RuntimeOpaque;
            }
            else if (MatName == TEXT("M_Transparent"))
            {
                NewType = EDatasmithMasterType::RuntimeTranslucent;
            }

            MasterTypeCache.Add(ParentMaterial, NewType);
            MasterType = NewType;
        }

        MaterialInstances.Reset();

        switch (MasterType)
        {
        case EDatasmithMasterType::TMStdOpaque:
            MaterialInstances.Append(CreateOpaqueMaterials(Material));
            MaterialInstances.Append(CreateTranslucentMaterials(Material, /*bIsOpaque=*/true));
            DatasmithMaterials.bIsOpaque.Add(true);
            break;

        case EDatasmithMasterType::TMStdTranslucent:
            MaterialInstances.Append(CreateOpaqueMaterials(Material));
            MaterialInstances.Append(CreateTranslucentMaterials(Material, /*bIsOpaque=*/false));
            DatasmithMaterials.bIsOpaque.Add(false);
            break;

        case EDatasmithMasterType::RuntimeOpaque:
            MaterialInstances.Append(CreateRuntimeOpaqueMaterials(Material));
            MaterialInstances.Append(CreateRuntimeTranslucentMaterials(Material, /*bIsOpaque=*/true));
            DatasmithMaterials.bIsOpaque.Add(true);
            break;

        case EDatasmithMasterType::RuntimeTranslucent:
            MaterialInstances.Append(CreateRuntimeOpaqueMaterials(Material));
            MaterialInstances.Append(CreateRuntimeTranslucentMaterials(Material, /*bIsOpaque=*/false));
            DatasmithMaterials.bIsOpaque.Add(false);
            break;

        default:
            continue;
        }

        DatasmithMaterials.MeshMaterials.Append(MaterialInstances);

        if (MeshComp->GetNumMaterials() <= Index)
        {
            break;
        }

        UMaterialInterface* const OpaqueMat      = DatasmithMaterials.MeshMaterials.IsValidIndex(Index * 2) ? DatasmithMaterials.MeshMaterials[Index * 2] : nullptr;
        UMaterialInterface* const TranslucentMat = DatasmithMaterials.MeshMaterials.IsValidIndex(Index * 2 + 1) ? DatasmithMaterials.MeshMaterials[Index * 2 + 1] : nullptr;

        if (DatasmithMaterials.bIsOpaque[Index] && OpaqueMat)
        {
            MeshComp->SetMaterial(Index, OpaqueMat);
        }
        else if (!DatasmithMaterials.bIsOpaque[Index] && TranslucentMat)
        {
            MeshComp->SetMaterial(Index, TranslucentMat);
        }
    }

    // Cache for later toggling
    DatasmithMaterialsMap.Add(MeshComp, MoveTemp(DatasmithMaterials));

    // Enable collision for this mesh (still batched separately)
    EnqueueCollisionEnable(MeshComp);
}
