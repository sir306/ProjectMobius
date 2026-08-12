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
#include "MaterialDomain.h"
#include "Actors/FlowCounter.h"
#include "Components/FlowCounterSpawnerComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/BodySetup.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Subsystems/PerformanceUtilSubsystem.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Subsystems/LoadingSubsystem.h"
#include "Subsystems/MobiusCustomLoggerSubsystem.h"
#include "Engine/Engine.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMemory.h"
#include "Misc/ScopeExit.h"
#include "Engine/StaticMesh.h"
#include "RenderingThread.h"

TMap<UMaterial*, EDatasmithMasterType> ARuntimeMeshBuilder::MasterTypeCache;

namespace
{
	/**
	 * Index of refraction on the RuntimeDatasmithOverrides translucent master.
	 *
	 * The Twinmotion translucent master names its equivalent differently, so its MIDs fail the
	 * lookup and are left alone — deliberately, since that content cannot ship anyway.
	 */
	const FName RefractionIndexParamName(TEXT("RefractionIndex"));

	/** IOR 1.0 bends nothing, so the surface contributes no distortion. */
	constexpr float NoRefractionIndex = 1.0f;
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
	// Async cooking spreads N-section collision cooks across worker threads instead of blocking the
	// game thread. First clicks after load may briefly miss until the last section cook completes —
	// acceptable: the loading widget covers the window.
	MobiusProceduralMeshComponent->bUseAsyncCooking = true;
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
	UMobiusCustomLoggerSubsystem* StartupLogger = GetStartupLogger();
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

	// Warn if generated Datasmith override materials are missing
	{
		static const FString RuntimeOverridePath = TEXT("/Game/01_Dev/RuntimeMeshGenerator/RuntimeDatasmithOverrides/MI_Opaque");
		if (!FPackageName::DoesPackageExist(RuntimeOverridePath))
		{
			UE_LOG(LogTemp, Error, TEXT("Missing generated Datasmith materials. Run Scripts/GenerateDatasmithMaterials.bat"));
		}
	}

	// As mesh generation needs to happen when the game starts and the world is required the delegate is bound here
	if(GetWorld())
	{
		RuntimeDatasmithAnchor = GetWorld()->SpawnActor<ADatasmithRuntimeActor>();

		UProjectMobiusGameInstance* GameInst = GetMobiusGameInstance(GetWorld());
		if(GameInst)
		{
			GameInst->OnMeshFileChanged.AddDynamic(this, &ARuntimeMeshBuilder::UpdateMeshFileName);
		}
		else
		{
			ReportError(
				this,
				FString("Mesh Generation Error"),
				FString("Game instance unavailable"),
				FString("Mesh generation will not work without a valid game instance."),
				FString("RuntimeMeshBuilder"));

			UE_LOG(LogTemp, Error, TEXT("Game Instance is not valid, Mesh Generation will not work"));
		}
	}
	else
	{
		ReportError(
			this,
			FString("Mesh Generation Error"),
			FString("World context unavailable"),
			FString("Mesh generation will not work without a valid world."),
			FString("RuntimeMeshBuilder"));

		UE_LOG(LogTemp, Error, TEXT("World is not valid, Mesh Generation will not work"));
	}

	// Assign the Flow Counter class to auto spawn
	if (FlowCounterSpawnerComponent)
	{
		FlowCounterSpawnerComponent->FlowCounterClass = FlowCounterToAutoSpawn;
	}
}

void ARuntimeMeshBuilder::ReleaseDatasmithSceneResources()
{
	if (!RuntimeDatasmithAnchor || RuntimeDatasmithAnchor->IsActorBeingDestroyed())
	{
		return;
	}

	// Render resources must be released from the game thread with rendering
	// commands already flushed, otherwise the render proxy may reference freed
	// vertex/index buffers for one more frame.
	FlushRenderingCommands();

	TSet<UStaticMesh*> ReleasedMeshes;

	const TSet<UActorComponent*>& DataComps = RuntimeDatasmithAnchor->GetComponents();
	for (UActorComponent* DataComp : DataComps)
	{
		USceneComponent* SceneComp = Cast<USceneComponent>(DataComp);
		if (!SceneComp)
		{
			continue;
		}

		TArray<USceneComponent*> ChildrenComps;
		SceneComp->GetChildrenComponents(true, ChildrenComps);

		for (USceneComponent* ChildComp : ChildrenComps)
		{
			UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(ChildComp);
			if (!MeshComp || MeshComp->IsBeingDestroyed())
			{
				continue;
			}

			// Drop our custom MID override refs on the component so the MIDs
			// can be GC'd.
			MeshComp->EmptyOverrideMaterials();

			UStaticMesh* Mesh = MeshComp->GetStaticMesh();
			if (!Mesh)
			{
				continue;
			}

			// Detach from component first so the component's detach path
			// doesn't try to read the render proxy after we free it.
			MeshComp->SetStaticMesh(nullptr);

			// Each RuntimeMesh may be referenced by multiple components; only
			// release its resources once.
			bool bAlreadyReleased = false;
			ReleasedMeshes.Add(Mesh, &bAlreadyReleased);
			if (bAlreadyReleased)
			{
				continue;
			}

			if (UBodySetup* BS = Mesh->GetBodySetup())
			{
				BS->ClearPhysicsMeshes();
				BS->InvalidatePhysicsData();
			}

			Mesh->ReleaseResources();
		}
	}

	// Wait for the render thread to finish releasing the resources before
	// EndPlay continues and the UObject shells get handed to GC.
	for (UStaticMesh* Mesh : ReleasedMeshes)
	{
		if (Mesh)
		{
			Mesh->ReleaseResourcesFence.Wait();
		}
	}
}

void ARuntimeMeshBuilder::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Kill the staggered emit pump before the actor goes away — the ticker holds a UObject
	// binding to `this` and would fire again post-teardown otherwise.
	if (ChunkEmitTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ChunkEmitTickerHandle);
		ChunkEmitTickerHandle.Reset();
	}
	PendingMeshChunks.Empty();
	PendingChunkEmitIndex = 0;

	// Clear any pending items
	PendingCollisionEnable.Reset();
	PendingDatasmithMeshes.Reset();
	bHeatmapBroadcastPending = false;

	// Drop our custom MID refs held per-mesh so the MIDs can be GC'd once the
	// components detach below.
	DatasmithMaterialsMap.Empty();
	TranslucentViewRefractionSnapshot.Empty();
	bTranslucentViewActive = false;

	// Free the DatasmithRuntime scene's heavy render/collision data. The
	// plugin's static FAssetRegistry::RegistrationMap keeps the UObject shells
	// alive across PIE stop, but the ~500MB of vertex/index/collision buffers
	// those shells own can be released here.
	if (RuntimeDatasmithAnchor && !RuntimeDatasmithAnchor->IsActorBeingDestroyed())
	{
		ReleaseDatasmithSceneResources();

		RuntimeDatasmithAnchor->Destroy();
		RuntimeDatasmithAnchor = nullptr;
	}

	// MasterTypeCache holds raw UMaterial* that become stale after PIE stop.
	// Empty it so the next PIE session starts with a clean classification cache
	// and doesn't dereference freed pointers.
	MasterTypeCache.Empty();

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
		ReportError(this,
		            FString("Mesh Generation Error"),
		            FString("Invalid mesh data"),
		            FString("Vertices, normals, or triangle indices are invalid."),
		            FString("RuntimeMeshBuilder"));

		UE_LOG(LogTemp, Error, TEXT("GenerateMobiusMesh received invalid mesh data (Vertices: %d, Normals: %d, Triangles: %d)"), InVertices.Num(), InNormals.Num(), InTriangles.Num());
		return;
	}

	bIsResettingForNewLoad = true;
	PendingCollisionEnable.Reset();
	PendingDatasmithMeshes.Reset();
	bHeatmapBroadcastPending = false;
	MaterialCache.Reset();

	if (ChunkEmitTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ChunkEmitTickerHandle);
		ChunkEmitTickerHandle.Reset();
	}
	PendingMeshChunks.Empty();
	PendingChunkEmitIndex = 0;

	if (FlowCounterSpawnerComponent)
	{
		FlowCounterSpawnerComponent->AbortSpawning();
		FlowCounterSpawnerComponent->RemoveAllFlowCounters();
	}

	if (RuntimeDatasmithAnchor && !RuntimeDatasmithAnchor->IsActorBeingDestroyed())
	{
		ReleaseDatasmithSceneResources();
		RuntimeDatasmithAnchor->Reset();
	}

	DatasmithMaterialsMap.Empty();
	TranslucentViewRefractionSnapshot.Empty();
	bTranslucentViewActive = false;
	bIsDatasmithAsset = false;
	ResetMeshCollisionAndPhysics();

	const FBox MeshBounds(InVertices);

	FAssimpSubmeshBuffers Input;
	Input.Vertices = MoveTemp(InVertices);
	Input.Faces    = MoveTemp(InTriangles);
	Input.Normals  = MoveTemp(InNormals);

	TArray<FAssimpSubmeshBuffers> Chunks;
	SplitSubmeshByTriCap(Input, MaxTrisPerSection, Chunks);

	static const TArray<FVector2D>        EmptyUVs;
	static const TArray<FColor>           EmptyColors;
	static const TArray<FProcMeshTangent> EmptyTangents;
	for (int32 ChunkIdx = 0; ChunkIdx < Chunks.Num(); ++ChunkIdx)
	{
		const FAssimpSubmeshBuffers& Chunk = Chunks[ChunkIdx];
		MobiusProceduralMeshComponent->CreateMeshSection(
			ChunkIdx,
			Chunk.Vertices,
			Chunk.Faces,
			Chunk.Normals,
			EmptyUVs,
			EmptyColors,
			EmptyTangents,
			true);
	}

	if (MobiusMaterialInstanceDynamic != nullptr)
	{
		const int32 NumSections = MobiusProceduralMeshComponent->GetNumSections();
		for (int32 SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
		{
			MobiusProceduralMeshComponent->SetMaterial(SectionIdx, MobiusMaterialInstanceDynamic);
		}
	}

	MobiusProceduralMeshComponent->bUseComplexAsSimpleCollision = true;
	MobiusProceduralMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MobiusProceduralMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	MobiusProceduralMeshComponent->SetSimulatePhysics(false);

	bMeshBeingBuilt = false;
	bIsResettingForNewLoad = false;

	if (MeshBounds.IsValid)
	{
		const FVector BoundsCenter = MeshBounds.GetCenter();
		const FVector BoundsExtent = MeshBounds.GetExtent();
		OnMeshBuilt.Broadcast(BoundsCenter - BoundsExtent, BoundsExtent);
	}
}

void ARuntimeMeshBuilder::GetMeshDataFromFile(const FRotator MeshRotationOffset)
{
	const double SyncLoadStart = FPlatformTime::Seconds();
	UMobiusCustomLoggerSubsystem* StartupLogger = GetStartupLogger();
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
		ReportError(this,
		            FString("Mesh Load Error"),
		            FString("Mesh file path missing"),
		            FString("Mesh file name is empty. Aborting mesh load."),
		            FString("RuntimeMeshBuilder"));

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
			ReportError(this,
			            FString("Mesh Load Error"),
			            FString("Mesh file empty"),
			            FString("Mesh file returned no usable data."),
			            FString("RuntimeMeshBuilder"));

			UE_LOG(LogTemp, Error, TEXT("Mesh file returned no usable data (Sections: %d, Vertices: %d, Faces: %d)."), SectionCount, MVertices.Num(), MFaces.Num());
			return;
		}

		// A mesh section should only be created if successful
		MobiusProceduralMeshComponent->CreateMeshSection_LinearColor(0, MVertices, MFaces, MNormals, MUV,
		                                                             TArray<FLinearColor>(),
		                                                             TArray<FProcMeshTangent>(),
		                                                             true);
	}
	else
	{
		ReportError(this,
		            FString("Mesh Load Error"),
		            FString("Failed to open mesh file"),
		            FString(ErrorMessageCode),
		            FString("RuntimeMeshBuilder"));

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
	// 1. Keep async cooking on — see constructor comment. Disabling here would force a blocking cook
	//    against the empty body setup we're about to hand off, wasting the async worker path.
	MobiusProceduralMeshComponent->bUseAsyncCooking = true;

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

void ARuntimeMeshBuilder::ClearMobiusProceduralMesh()
{
	// Guard: ResetMeshCollisionAndPhysics dereferences the component directly.
	if (!MobiusProceduralMeshComponent)
	{
		return;
	}

	// Reuse the procedural-only teardown. This empties mesh sections + collision and leaves
	// any imported Datasmith building (a separate component tree) completely untouched.
	ResetMeshCollisionAndPhysics();
}

void ARuntimeMeshBuilder::UpdateMeshFileName()
{
	const double UpdateStart = FPlatformTime::Seconds();
	UMobiusCustomLoggerSubsystem* StartupLogger = GetStartupLogger();
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
	bHeatmapBroadcastPending = false;
	MaterialCache.Reset();

	// Cancel any in-flight FBX staggered emit — the ticker runs independently of Tick() and
	// does not check bIsResettingForNewLoad, so it must be explicitly removed here. Without this,
	// EmitNextChunkSection continues calling CreateMeshSection_LinearColor after ClearAllMeshSections,
	// re-adding the old FBX geometry on top of the new Datasmith load.
	if (ChunkEmitTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ChunkEmitTickerHandle);
		ChunkEmitTickerHandle.Reset();
	}
	PendingMeshChunks.Empty();
	PendingChunkEmitIndex = 0;

	// Stop any in-flight FBX async runnable so GetTheAsyncMeshData cannot fire late and
	// set up a new emit ticker during the incoming Datasmith load.
	if (AsyncAssimpLoader && AsyncAssimpLoader->MeshLoaderRunnable)
	{
		AsyncAssimpLoader->MeshLoaderRunnable->OnLoadMeshDataComplete.RemoveAll(this);
		AsyncAssimpLoader->MeshLoaderRunnable->Stop();
		AsyncAssimpLoader->MeshLoaderRunnable->Vertices.Empty();
		AsyncAssimpLoader->MeshLoaderRunnable->Faces.Empty();
		AsyncAssimpLoader->MeshLoaderRunnable->Normals.Empty();
		AsyncAssimpLoader->MeshLoaderRunnable->UV.Empty();
		AsyncAssimpLoader->MeshLoaderRunnable->Tangents.Empty();
		FAssimpMeshLoaderRunnable* StaleRunnable = AsyncAssimpLoader->MeshLoaderRunnable;
		AsyncAssimpLoader->MeshLoaderRunnable = nullptr;
		AsyncTask(ENamedThreads::GameThread, [StaleRunnable] { delete StaleRunnable; });
	}

	// Discard any queued-but-unspawned door entries from the previous load. The spawner's
	// TickComponent is independent and would otherwise continue draining the stale queue
	// after the new mesh replaces the old one.
	FlowCounterSpawnerComponent->AbortSpawning();

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

	// Double-flush queues in case async work enqueued new items during teardown
	PendingCollisionEnable.Reset();
	PendingDatasmithMeshes.Reset();

	DatasmithMaterialsMap.Empty();
	TranslucentViewRefractionSnapshot.Empty();
	bTranslucentViewActive = false;
	bIsDatasmithAsset = false;

	// Remove any flow counters, as the mesh is changing
	FlowCounterSpawnerComponent->RemoveAllFlowCounters();

	// Cancel any previous deferred continuation so rapid switches don't stack.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredLoadTimerHandle);
	}

	// If we have a live DatasmithRuntime anchor from a prior load, queue its
	// SceneImporter to purge AssetDataList / AssetElementMapping on the next
	// tick. The plugin's Reset() only sets TasksToComplete=ResetScene; the
	// actual DeleteData() + AssetRegistry::CleanUp() + internal CollectGarbage
	// run inside FSceneImporter::Tick. We must not call LoadFile (or spin up
	// the fbx pipeline) until that tick completes, otherwise StartImport
	// overwrites TasksToComplete and the prior scene's ~837 RuntimeMesh +
	// ~840 BodySetup UObjects survive. Applies regardless of the new file's
	// extension — switching datasmith -> fbx also needs the anchor purged.
	if (RuntimeDatasmithAnchor)
	{
		RuntimeDatasmithAnchor->Reset();

		if (UWorld* World = GetWorld())
		{
			// Two-tick defer: within a single frame FTimerManager fires before
			// the actor's Tick, so a single next-tick delay lets our LoadFile
			// run (and overwrite TasksToComplete=CollectSceneData) before the
			// SceneImporter's ResetScene branch ever executes. Chaining two
			// next-tick timers guarantees at least one full ADatasmithRuntime
			// Tick runs between Reset() and LoadFile, so DeleteData() +
			// AssetRegistry::CleanUp() + the plugin's internal CollectGarbage
			// finish before the next scene populates AssetDataList.
			TWeakObjectPtr<ARuntimeMeshBuilder> WeakThis(this);
			World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateLambda([WeakThis]()
				{
					ARuntimeMeshBuilder* Self = WeakThis.Get();
					if (!Self) return;
					UWorld* InnerWorld = Self->GetWorld();
					if (!InnerWorld) return;
					InnerWorld->GetTimerManager().SetTimerForNextTick(
						FTimerDelegate::CreateLambda([WeakThis]()
						{
							if (ARuntimeMeshBuilder* Inner = WeakThis.Get())
							{
								Inner->ContinueLoadAfterPurge();
							}
						}));
				}));
			return;
		}
	}

	// No anchor (first load, or fbx->fbx) — nothing to purge, run immediately.
	ContinueLoadAfterPurge();
}

void ARuntimeMeshBuilder::ContinueLoadAfterPurge()
{
	// The 2-tick defer let the plugin's ResetScene tick unreference all prior-
	// scene RuntimeMesh/BodySetup objects, but they're still PendingKill in the
	// UObject array. Sweep them now so the next LoadFile doesn't double the
	// resident set before engine GC runs.
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
	// As we are now able to use Datasmith assets we need to check if the file is a .udatasmith file.
	//
	// .ifc USED TO BE ROUTED HERE and silently did nothing: it fell into the else below, built a
	// FDatasmithRuntimeImportOptions, assigned it to the anchor and returned without ever calling
	// LoadFile — no geometry, no error, no log, while seven other places in the codebase advertised
	// .ifc support. It is not routed here any more, and must not be re-routed here: DatasmithRuntime
	// cannot translate IFC in a packaged build at all, because its CAD backend (HOOPS/TechSoft) lives
	// in Engine\Restricted\NotForLicensees and is absent from a licensee engine install. .ifc now
	// falls through to AsyncUpdateMesh, where FAssimpMeshLoaderRunnable routes it to
	// FMobiusIfcMeshLoader (IFC++ via our MobiusIfcBridge C shim). See
	// _CurrentHandoff\HANDOFF_IFC_2026-08-11.md sections 1, 3 and 7.3.
	if(MeshFileName.EndsWith(TEXT(".udatasmith"), ESearchCase::IgnoreCase))
	{
		// check world is valid
		if(!CheckStillInWorld())
		{
			ReportError(this,
			            FString("Mesh Load Error"),
			            FString("World context unavailable"),
			            FString("Cannot load Datasmith mesh without a valid world."),
			            FString("RuntimeMeshBuilder"));

			UE_LOG(LogTemp, Error, TEXT("World is not valid"));
			return;
		}

		// set the flag to indicate this is a datasmith file
		bIsDatasmithAsset = true;

		// Spawn only on first load; subsequent loads reuse the same actor so the
		// plugin's SceneImporter teardown runs before the next scene populates.
		if (RuntimeDatasmithAnchor == nullptr)
		{
			RuntimeDatasmithAnchor = GetWorld()->SpawnActor<ADatasmithRuntimeActor>();
		}

		// Inner extension re-check removed: the enclosing branch is now .udatasmith-only, so the
		// second test was always true and its else was the dead .ifc no-op described above.
		{
			// is the runtime datasmith anchor valid
			if(RuntimeDatasmithAnchor == nullptr)
			{
				ReportError(this,
				            FString("Mesh Load Error"),
				            FString("Datasmith anchor unavailable"),
				            FString("Cannot load Datasmith mesh without a valid Runtime Datasmith Anchor."),
				            FString("RuntimeMeshBuilder")
				);
				UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
				return;
			}
			// stack trace to show this is the datasmith import
			TRACE_CPUPROFILER_EVENT_SCOPE_STR("Datasmith Import");

			// import options
			FDatasmithRuntimeImportOptions ImportOptions;// TODO: set the import options

			// For thesis, I don't want collisions
			ImportOptions.BuildCollisions = ECollisionEnabled::Type::QueryOnly;
			ImportOptions.CollisionType = ECollisionTraceFlag::CTF_UseSimpleAndComplex;
			ImportOptions.TessellationOptions.bUseCADKernel = true;
			ImportOptions.TessellationOptions.StitchingTechnique = EDatasmithCADStitchingTechnique::StitchingHeal;
			RuntimeDatasmithAnchor->ImportOptions = ImportOptions;

			// import the mesh data into the anchor
			RuntimeDatasmithAnchor->LoadFile(MeshFileName);

			// Async task to check if the scene is loaded.
			// Poll on thread pool, then marshal material setup back to GT — use
			// TWeakObjectPtr so the actor being torn down during the poll doesn't
			// leave the worker / GT lambdas dereferencing a destroyed this.
			TWeakObjectPtr<ARuntimeMeshBuilder> WeakThis(this);
			Async(EAsyncExecution::ThreadPool, [WeakThis]()
			      {
				      FPlatformProcess::Sleep(5.0f);
				      ARuntimeMeshBuilder* Self = WeakThis.Get();
				      if (!Self || !Self->RuntimeDatasmithAnchor) return;
				      UE_LOG(LogTemp, Warning, TEXT("1Building: %d, Receiving: %d"), Self->RuntimeDatasmithAnchor->bBuilding, Self->RuntimeDatasmithAnchor->IsReceiving());
				      while (Self->RuntimeDatasmithAnchor->bBuilding || Self->RuntimeDatasmithAnchor->IsReceiving())
				      {
					      UE_LOG(LogTemp, Warning, TEXT("2Building: %d, Receiving: %d"), Self->RuntimeDatasmithAnchor->bBuilding, Self->RuntimeDatasmithAnchor->IsReceiving());
					      FPlatformProcess::Sleep(0.05f);
					      Self = WeakThis.Get();
					      if (!Self || !Self->RuntimeDatasmithAnchor) return;
				      }
				      UE_LOG(LogTemp, Warning, TEXT("3Building: %d, Receiving: %d"), Self->RuntimeDatasmithAnchor->bBuilding, Self->RuntimeDatasmithAnchor->IsReceiving());

			      }, [WeakThis]()
			      {
				      AsyncTask(ENamedThreads::GameThread, [WeakThis] {
					      if (ARuntimeMeshBuilder* Self = WeakThis.Get())
					      {
						      Self->CreateDatasmithMaterials();
						      // lights imported by datasmith can cause performance issues, so may need to disable cast shadows or reduce
						      // the size of point light radius and intensitys
						      Self->bIsResettingForNewLoad = false;
					      }
				      });

			      });
		}
	}
	// not a datasmith file so we can load the mesh as normal — fbx/obj/wkt/h5 via Assimp, and .ifc
	// via FMobiusIfcMeshLoader, both inside FAssimpMeshLoaderRunnable. Anchor (if any) has had its
	// prior scene purged by the ResetScene tick and stays idle.
	else
	{
		AsyncUpdateMesh(MeshFileName);
		bIsResettingForNewLoad = false;
	}
}

void ARuntimeMeshBuilder::AsyncUpdateMesh(const FString PathToMesh)
{
	const double AsyncUpdateStart = FPlatformTime::Seconds();
	UMobiusCustomLoggerSubsystem* StartupLogger = GetStartupLogger();
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
		ReportError(this,
		            FString("Mesh Load Error"),
		            FString("AsyncUpdateMesh called off-thread"),
		            FString("AsyncUpdateMesh must be called on the game thread."),
		            FString("RuntimeMeshBuilder"));

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

		// Drop the broadcast binding first so a late completion from the old runnable
		// can't invoke GetTheAsyncMeshData on already-stale state.
		ExistingRunnable->OnLoadMeshDataComplete.RemoveAll(this);

		// Signal stop; destructor (via the deferred GT delete) WaitForCompletion-joins
		// the thread, which lets UE call Exit() once cleanly after Run() returns.
		ExistingRunnable->Stop();

		// Free the CPU-side mesh buffers immediately. Without this, the runnable
		// sits in the deferred GT delete lambda holding hundreds of MB until the
		// game thread services the queue.
		ExistingRunnable->Vertices.Empty();
		ExistingRunnable->Faces.Empty();
		ExistingRunnable->Normals.Empty();
		ExistingRunnable->UV.Empty();
		ExistingRunnable->Tangents.Empty();

		// Capture by value — ExistingRunnable is a stack-local pointer and goes
		// out of scope when this function returns, before the GT task runs.
		AsyncTask(ENamedThreads::GameThread, [ExistingRunnable]
		{
			delete ExistingRunnable;
		});
	}

	// Create the runnable
	AsyncAssimpLoader->MeshLoaderRunnable = new FAssimpMeshLoaderRunnable(PathToMesh, this);
	AsyncAssimpLoader->MeshLoaderRunnable->OnLoadMeshDataComplete.AddDynamic(this, &ARuntimeMeshBuilder::GetTheAsyncMeshData);
}

void ARuntimeMeshBuilder::GetTheAsyncMeshData()
{
	// Guard against late completion callbacks fired after a new load already started.
	// UpdateMeshFileName stops the runnable and removes this binding, but a callback
	// already dispatched to the game thread queue can still arrive here.
	if (bIsResettingForNewLoad) { return; }

	const double ReceiveStart = FPlatformTime::Seconds();
	UMobiusCustomLoggerSubsystem* StartupLogger = GetStartupLogger();
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

	// Move the per-submesh buffers out of the runnable so we can tear down the loader before the
	// (potentially slow) emit loop. Runnable retains empty TArrays — cheap to delete.
	TArray<FAssimpSubmeshBuffers> LocalSubmeshes = MoveTemp(AsyncAssimpLoader->MeshLoaderRunnable->Submeshes);

	// IFC diagnostics travel with the buffers. Moved out before the runnable is torn down below;
	// default-constructed (empty SourceSchema) for every non-IFC format.
	LastIfcLoadStats = MoveTemp(AsyncAssimpLoader->MeshLoaderRunnable->IfcLoadStats);
	IfcSectionInfo.Reset();

	// Fresh load: re-probe whether the (Blueprint-supplied) building material can express source
	// colours, since the material may have been swapped since the last load.
	bSourceColourParamProbeDone = false;
	bSourceColourParamsAvailable = false;
	SourceColourProbedParent = nullptr;
	SourceMaterialSectionsApplied = 0;
	// Per-section colours belong to the mesh that is being replaced, not to the next one. Same for the
	// section MIDs -- section 3 of the next building is not section 3 of this one.
	SectionSourceMaterials.Reset();
	SectionColourMIDs.Reset();
	SectionColourMIDParent = nullptr;

	// The loader is no longer needed. Properly stop, drop the CPU-side mesh
	// buffers, and delete the runnable. The previous code path nulled
	// MeshLoaderRunnable without deleting it, leaking the runnable plus
	// hundreds of MB of Vertices/Faces/Normals/UV/Tangents per load.
	if (auto* ExistingRunnable = AsyncAssimpLoader->MeshLoaderRunnable)
	{
		AsyncAssimpLoader->MeshLoaderRunnable = nullptr;

		// Drop the completion binding before stopping so any late broadcast is a no-op.
		ExistingRunnable->OnLoadMeshDataComplete.RemoveAll(this);

		// Destructor WaitForCompletion-joins the thread and UE calls Exit() once cleanly
		// after Run() returns — no manual Exit() from the game thread.
		ExistingRunnable->Stop();

		// Per-submesh buffers already moved out above. Clear the transitional flat mirrors so the
		// runnable drops the last references before deletion.
		ExistingRunnable->Vertices.Empty();
		ExistingRunnable->Faces.Empty();
		ExistingRunnable->Normals.Empty();
		ExistingRunnable->UV.Empty();
		ExistingRunnable->Tangents.Empty();

		AsyncTask(ENamedThreads::GameThread, [ExistingRunnable]
		{
			delete ExistingRunnable;
		});
	}

	// Partition each submesh into triangle-capped chunks. Small submeshes pass through unchanged.
	// Rollback: when bEnableMultiSectionBatching is false, concatenate everything into a single chunk
	// that reproduces the legacy single-section-0 behaviour (with index remap for vertex offsets).
	TArray<FAssimpSubmeshBuffers> Chunks;
	if (bEnableMultiSectionBatching)
	{
		Chunks.Reserve(LocalSubmeshes.Num());
		for (const FAssimpSubmeshBuffers& Sub : LocalSubmeshes)
		{
			SplitSubmeshByTriCap(Sub, MaxTrisPerSection, Chunks);
		}
	}
	else
	{
		int32 TotalVerts = 0;
		int32 TotalFaces = 0;
		for (const FAssimpSubmeshBuffers& Sub : LocalSubmeshes)
		{
			TotalVerts += Sub.Vertices.Num();
			TotalFaces += Sub.Faces.Num();
		}
		FAssimpSubmeshBuffers Flat;
		Flat.Vertices.Reserve(TotalVerts);
		Flat.Faces.Reserve(TotalFaces);
		Flat.Normals.Reserve(TotalVerts);
		Flat.UV.Reserve(TotalVerts);
		for (const FAssimpSubmeshBuffers& Sub : LocalSubmeshes)
		{
			const int32 VertexBase = Flat.Vertices.Num();
			Flat.Vertices.Append(Sub.Vertices);
			Flat.Normals.Append(Sub.Normals);
			Flat.UV.Append(Sub.UV);
			Flat.Faces.Reserve(Flat.Faces.Num() + Sub.Faces.Num());
			for (int32 Index : Sub.Faces)
			{
				Flat.Faces.Add(Index + VertexBase);
			}
		}
		Chunks.Add(MoveTemp(Flat));
	}

	if (StartupLogger)
	{
		int32 TotalVerts = 0;
		int32 TotalTris  = 0;
		for (const FAssimpSubmeshBuffers& Chunk : Chunks)
		{
			TotalVerts += Chunk.Vertices.Num();
			TotalTris  += Chunk.Faces.Num() / 3;
		}
		StartupLogger->EnqueueLogMessage(FString::Printf(
			TEXT("RuntimeMeshBuilder::GetTheAsyncMeshData sections=%d verts=%d tris=%d"),
			Chunks.Num(), TotalVerts, TotalTris));
	}

	// IFC only: record the per-section entity map, then emit the render-filter summary exactly ONCE.
	// The allowlist itself never logs (per-product logging is a project rule violation); this is the
	// single line that makes a dropped class visible, and it is deliberately here — after the load,
	// outside every per-product and per-section loop.
	if (!LastIfcLoadStats.SourceSchema.IsEmpty())
	{
		IfcSectionInfo.Reserve(Chunks.Num());
		for (const FAssimpSubmeshBuffers& Chunk : Chunks)
		{
			FMobiusIfcSectionInfo& Info = IfcSectionInfo.AddDefaulted_GetRef();
			Info.Guid = Chunk.SourceGuid;
			Info.IfcClass = Chunk.SourceIfcClass;
			Info.MaterialName = Chunk.SourceMaterialName;
			Info.Material = Chunk.Material;
		}

		int32 SectionsWithSourceMaterial = 0;
		for (const FMobiusIfcSectionInfo& Info : IfcSectionInfo)
		{
			if (Info.Material.bHasMaterial)
			{
				++SectionsWithSourceMaterial;
			}
		}

		const FString IfcSummary = FString::Printf(
			TEXT("IFC load: schema=%s products_with_geometry=%d products_without_geometry=%d rendered_products=%d ")
			TEXT("rendered_tris=%d file_tris=%d malformed=%d rooms(IfcSpace)=%d sections=%d ")
			TEXT("styled_sections=%d materials(products)=%d | %s"),
			*LastIfcLoadStats.SourceSchema,
			LastIfcLoadStats.ProductsWithGeometry,
			LastIfcLoadStats.ProductsWithoutGeometry,
			LastIfcLoadStats.RenderedProducts,
			LastIfcLoadStats.RenderedTriangles,
			LastIfcLoadStats.TotalTriangles,
			LastIfcLoadStats.MalformedProducts,
			LastIfcLoadStats.RoomVolumes.Num(),
			IfcSectionInfo.Num(),
			SectionsWithSourceMaterial,
			LastIfcLoadStats.ProductMaterials.Num(),
			*LastIfcLoadStats.FilterSummary);

		if (StartupLogger)
		{
			StartupLogger->EnqueueLogMessage(IfcSummary);
		}
		UE_LOG(LogTemp, Log, TEXT("%s"), *IfcSummary);
	}

	// Hand the chunks off to the staggered emit pump. Emitting all sections in one tick spikes
	// FScene_AddPrimitive on the game thread (~300ms for 8 sections on the test asset); spreading
	// them across frames keeps the per-frame cost bounded. Finalize (broadcast + EndLoadingWidget
	// + bMeshBeingBuilt=false) runs after the last chunk is pushed, so listeners see a fully
	// populated bounds. Each section is cooked WITH collision (bCreateCollision=true) in the pump; the
	// component itself is switched from its ctor NoCollision to QueryOnly once in FinalizeMeshEmit, so
	// the cooked geometry becomes queryable for cursor ray-traces.
	PendingMeshChunks = MoveTemp(Chunks);
	PendingChunkEmitIndex = 0;
	ChunkEmitStartTime = FPlatformTime::Seconds();

	if (ChunkEmitTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ChunkEmitTickerHandle);
		ChunkEmitTickerHandle.Reset();
	}

	if (PendingMeshChunks.Num() == 0)
	{
		// Nothing to emit — still run finalize so bMeshBeingBuilt/loading widget clear.
		FinalizeMeshEmit();
		return;
	}

	ChunkEmitTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ARuntimeMeshBuilder::EmitNextChunkSection),
		0.0f);
}

bool ARuntimeMeshBuilder::EmitNextChunkSection(float /*DeltaTime*/)
{
	if (!IsValid(MobiusProceduralMeshComponent))
	{
		PendingMeshChunks.Empty();
		PendingChunkEmitIndex = 0;
		ChunkEmitTickerHandle.Reset();
		return false;
	}

	static const TArray<FLinearColor>     EmptyColors;
	static const TArray<FProcMeshTangent> EmptyTangents;

	UMobiusCustomLoggerSubsystem* StartupLogger = GetStartupLogger();
	const int32 ChunksThisTick = FMath::Max(1, SectionsEmittedPerTick);
	for (int32 Pushed = 0; Pushed < ChunksThisTick && PendingChunkEmitIndex < PendingMeshChunks.Num(); ++Pushed)
	{
		const int32 SectionIdx = PendingChunkEmitIndex++;
		FAssimpSubmeshBuffers& Chunk = PendingMeshChunks[SectionIdx];

		const int32 ChunkVerts = Chunk.Vertices.Num();
		const int32 ChunkTris  = Chunk.Faces.Num() / 3;
		const double PushStart = FPlatformTime::Seconds();

		MobiusProceduralMeshComponent->CreateMeshSection_LinearColor(
			SectionIdx,
			Chunk.Vertices,
			Chunk.Faces,
			Chunk.Normals,
			Chunk.UV,
			EmptyColors,
			EmptyTangents,
			/*bCreateCollision*/ true);

		// Remember this section's source material BEFORE anything is applied, index-parallel to the
		// ProcMesh sections and for every format. SetBuildingMaterialStyle re-applies these onto a
		// different parent, so without this cache switching to translucent and back would permanently
		// lose every imported colour.
		if (SectionSourceMaterials.Num() <= SectionIdx)
		{
			SectionSourceMaterials.SetNum(SectionIdx + 1);
		}
		SectionSourceMaterials[SectionIdx] = Chunk.Material;

		// Per-section source material, when the import gave this section one AND the project's building
		// material can express it. See ApplySourceMaterialToSection for why this is a probe rather than
		// an assumption.
		if (!ApplySourceMaterialToSection(SectionIdx, Chunk.Material))
		{
			if (MobiusMaterialInstanceDynamic)
			{
				MobiusProceduralMeshComponent->SetMaterial(SectionIdx, MobiusMaterialInstanceDynamic);
			}
		}

		const double PushDurationMs = (FPlatformTime::Seconds() - PushStart) * 1000.0;
		if (StartupLogger)
		{
			StartupLogger->EnqueueLogMessage(FString::Printf(
				TEXT("[Building %s] CreateMeshSection section=%d verts=%d tris=%d in %.2f ms"),
				*GetName(), SectionIdx, ChunkVerts, ChunkTris, PushDurationMs));
		}
		UE_LOG(LogTemp, Log,
			TEXT("[Building %s] CreateMeshSection section=%d verts=%d tris=%d in %.2f ms"),
			*GetName(), SectionIdx, ChunkVerts, ChunkTris, PushDurationMs);

		// Free the CPU buffers for this chunk now that the component owns the data.
		Chunk.Vertices.Empty();
		Chunk.Faces.Empty();
		Chunk.Normals.Empty();
		Chunk.UV.Empty();
	}

	if (PendingChunkEmitIndex >= PendingMeshChunks.Num())
	{
		FinalizeMeshEmit();
		return false;
	}

	return true;
}

UMaterialInterface* ARuntimeMeshBuilder::ResolveStyleParentMaterial(EMobiusBuildingMaterialStyle Style) const
{
	// The four instances already in the project, one per style. Hardcoded paths follow the existing
	// precedent in this file (CreateRuntimeOpaqueMaterials hardcodes MI_Opaque the same way).
	//
	// Style -> instance mapping, per the owner's specification:
	//   OriginalColours            -> Opaque
	//   OriginalColoursCutOut      -> Masked        (masked blend IS the cut-out)
	//   TransparentWhite           -> Translucent
	//   OriginalColoursTransparent -> TranslucentClearcoat
	static const TCHAR* Base = TEXT("/Game/01_Dev/RuntimeMeshGenerator/GeneratedMeshMasterMaterials/");
	const TCHAR* AssetName = TEXT("MI_RuntimeMeshBuilderOpaque");
	switch (Style)
	{
	case EMobiusBuildingMaterialStyle::OriginalColoursCutOut:      AssetName = TEXT("MI_RuntimeMeshBuilderMasked"); break;
	case EMobiusBuildingMaterialStyle::TransparentWhite:           AssetName = TEXT("MI_RuntimeMeshBuilderTranslucent"); break;
	case EMobiusBuildingMaterialStyle::OriginalColoursTransparent: AssetName = TEXT("MI_RuntimeMeshBuilderTranslucentClearcoat"); break;
	case EMobiusBuildingMaterialStyle::OriginalColours:
	default:                                                       AssetName = TEXT("MI_RuntimeMeshBuilderOpaque"); break;
	}

	const FString Path = FString::Printf(TEXT("%s%s.%s"), Base, AssetName, AssetName);
	UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(nullptr, *Path);
	if (!Loaded)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Building %s] building material style asset not found: %s. Sections keep their current material."),
			*GetName(), *Path);
	}
	return Loaded;
}

void ARuntimeMeshBuilder::SetBuildingMaterialStyle(EMobiusBuildingMaterialStyle Style)
{
	CurrentBuildingMaterialStyle = Style;
	bBuildingMaterialStyleChosen = true;

	// ---- Datasmith buildings: delegate to the existing implementations ----------------------------
	// Behaviour for a .udatasmith building is unchanged by construction -- this only routes one widget
	// control to the calls that widget was already making.
	if (bIsDatasmithAsset)
	{
		switch (Style)
		{
		case EMobiusBuildingMaterialStyle::OriginalColours:
			SetDatasmithToOriginalMatStyle();
			SetDatasmithMeshToSolidMaterials();
			SetDatasmithMeshToUseClearCoatMaterials(false);
			break;

		case EMobiusBuildingMaterialStyle::OriginalColoursCutOut:
			SetDatasmithToOriginalMatStyle();
			SetDatasmithMeshToSolidMaterials();
			// The Datasmith equivalent of "cut out" is the box dissolve, which needs its bounds set by
			// the caller (SetDatasmithDissolveMeshSizeAndOrigin) -- enabling it here without bounds
			// would dissolve nothing, so the dissolve toggle is left to the existing control.
			BoxDissolveDatasmithMesh(true);
			break;

		case EMobiusBuildingMaterialStyle::TransparentWhite:
			SetDatasmithMeshToTranslucentMaterials();
			SetDatasmithToUseModifiedColour(true, FLinearColor::White);
			break;

		case EMobiusBuildingMaterialStyle::OriginalColoursTransparent:
			SetDatasmithMeshToTranslucentMaterials();
			SetDatasmithToUseModifiedColour(false); // keep the imported colours
			SetDatasmithMeshToUseClearCoatMaterials(true);
			break;
		}
		return;
	}

	// ---- Procedural buildings (IFC / fbx / obj / wkt) ---------------------------------------------
	if (!IsValid(MobiusProceduralMeshComponent))
	{
		return;
	}

	UMaterialInterface* StyleParent = ResolveStyleParentMaterial(Style);
	if (!StyleParent)
	{
		return; // ResolveStyleParentMaterial already logged
	}

	const bool bForceWhite = (Style == EMobiusBuildingMaterialStyle::TransparentWhite);
	const int32 NumSections = MobiusProceduralMeshComponent->GetNumSections();
	int32 Coloured = 0;

	for (int32 SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
	{
		// Reused per section; StyleParent is an asset instance, so this never nests MIDs.
		UMaterialInstanceDynamic* SectionMaterial = GetOrCreateSectionMID(SectionIdx, StyleParent);
		if (!SectionMaterial)
		{
			continue;
		}

		// Reuse is safe against stale parameters here because each of the four styles resolves to a
		// DIFFERENT MI_RuntimeMeshBuilder* asset, so a style switch changes the parent and rebuilds the
		// MIDs. Reuse only happens on a repeat call with the same style, where every value written below
		// is the same value. If a future style ever shares a parent with another, this branch must start
		// writing every parameter unconditionally.

		// Source colour for this section, when the import gave it one AND this style wants it.
		// Everything else falls through to Use Modified Colour = 0, which is the master's own plain
		// white chain -- so "no colours" degrades to plain white / plain white cut out / plain white
		// transparent / plain white clear coat, one per style, rather than to black.
		const FMobiusMeshMaterial* Source = SectionSourceMaterials.IsValidIndex(SectionIdx)
			                                    ? &SectionSourceMaterials[SectionIdx]
			                                    : nullptr;

		if (bForceWhite)
		{
			SectionMaterial->SetScalarParameterValue(FName("Use Modified Colour"), 1.0f);
			SectionMaterial->SetVectorParameterValue(FName("NewColour"), FLinearColor::White);
		}
		else if (Source && Source->bHasMaterial)
		{
			SectionMaterial->SetScalarParameterValue(FName("Use Modified Colour"), 1.0f);
			SectionMaterial->SetVectorParameterValue(FName("NewColour"), Source->BaseColour);
			++Coloured;
		}
		else
		{
			SectionMaterial->SetScalarParameterValue(FName("Use Modified Colour"), 0.0f);
		}

		// Source opacity only where the source actually asked for transparency; the style's own blend
		// mode governs everything else. Overwriting OpacityAmount on an opaque style would fight the
		// project's own translucent-view feature for no reason.
		const float SourceAlpha = (Source && Source->bHasMaterial) ? Source->BaseColour.A : 1.0f;
		if (SourceAlpha < 1.0f)
		{
			SectionMaterial->SetScalarParameterValue(FName("OpacityAmount"), SourceAlpha);
		}

		MobiusProceduralMeshComponent->SetMaterial(SectionIdx, SectionMaterial);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Building %s] building material style -> %d (procedural): %d sections, %d took a source colour"),
		*GetName(), static_cast<int32>(Style), NumSections, Coloured);
}

UMaterialInterface* ARuntimeMeshBuilder::ResolveNonDynamicParent(UMaterialInterface* InMaterial)
{
	// See the header for why a MID must never parent a MID here. Bounded walk: a legitimate chain is one
	// or two links, and a cycle (which SetParentInternal should already have refused) must not hang us.
	UMaterialInterface* Candidate = InMaterial;
	for (int32 Depth = 0; Depth < 16 && Candidate != nullptr; ++Depth)
	{
		UMaterialInstanceDynamic* AsDynamic = Cast<UMaterialInstanceDynamic>(Candidate);
		if (AsDynamic == nullptr)
		{
			return Candidate; // asset material instance, or a UMaterial -- either is a stable parent
		}
		Candidate = AsDynamic->Parent;
	}
	return nullptr;
}

UMaterialInstanceDynamic* ARuntimeMeshBuilder::GetOrCreateSectionMID(int32 SectionIdx, UMaterialInterface* Parent)
{
	if (Parent == nullptr || SectionIdx < 0)
	{
		return nullptr;
	}

	// A different parent means a different look; the cached MIDs are no longer the right objects.
	if (SectionColourMIDParent != Parent)
	{
		SectionColourMIDParent = Parent;
		SectionColourMIDs.Reset();
	}

	if (SectionColourMIDs.Num() <= SectionIdx)
	{
		SectionColourMIDs.SetNum(SectionIdx + 1);
	}

	UMaterialInstanceDynamic* Existing = SectionColourMIDs[SectionIdx];
	if (IsValid(Existing) && Existing->Parent == Parent)
	{
		return Existing;
	}

	UMaterialInstanceDynamic* Created = UMaterialInstanceDynamic::Create(Parent, this);
	if (Created != nullptr && Created->Parent == nullptr)
	{
		// SetParentInternal refused the parent (cycle or self). Better to report and fall back to the
		// shared material than to hand the mesh a parentless MID, which renders as nothing meaningful.
		UE_LOG(LogTemp, Error,
			TEXT("[Building %s] section %d: MID creation left a NULL parent for '%s'. Section keeps the ")
			TEXT("shared material."),
			*GetName(), SectionIdx, *Parent->GetName());
		return nullptr;
	}

	SectionColourMIDs[SectionIdx] = Created;
	return Created;
}

bool ARuntimeMeshBuilder::ApplySourceMaterialToSection(int32 SectionIdx, const FMobiusMeshMaterial& SourceMaterial)
{
	// ---------------------------------------------------------------------------------------------
	// Applies a source-authored colour (IFC IfcSurfaceStyle, or aiMaterial for fbx/obj) to one section
	// by creating a MID from the building material's own parent and setting the colour parameters the
	// Datasmith path already uses.
	//
	// THIS IS A PROBE, NOT AN ASSUMPTION, and that is the whole design of this function.
	// MobiusMaterialInstanceDynamic is handed to this actor from Blueprint (UpdateMeshMaterial is
	// BlueprintCallable), so C++ cannot know which master material it is. "NewColour",
	// "Use Modified Colour" and "OpacityAmount" are known to exist on the Datasmith master family; if
	// the procedurally-built building happens to use a different parent, blindly calling
	// SetVectorParameterValue would be a silent no-op -- geometry would render in the default colour and
	// look like the importer had dropped the material. So: check the parameter exists, and if it does
	// not, return false so the caller keeps the shared material and the ONE summary log line says so.
	//
	// Returns true only if a per-section material was actually created AND parameterised.
	// ---------------------------------------------------------------------------------------------
	if (!SourceMaterial.bHasMaterial || !IsValid(MobiusProceduralMeshComponent))
	{
		return false;
	}

	// NOT MobiusMaterialInstanceDynamic itself -- that is a MID, and Blueprint derives it from whatever is
	// currently on the section, so using it as a parent stacks MIDs and eventually nulls the parent.
	// ResolveNonDynamicParent walks to the asset behind it; see its header comment.
	UMaterialInterface* Parent = ResolveNonDynamicParent(MobiusMaterialInstanceDynamic);
	if (!Parent)
	{
		// The walk found no asset, which means Blueprint handed us a MID that UE had ALREADY refused to
		// parent -- the "MID_MID_*" case, logged by the engine as "Only Materials and
		// MaterialInstanceConstants are valid parents for a material instance". Such a MID has Parent ==
		// null and carries no material chain at all, so it can neither be used as a parent nor be applied
		// to the mesh. Fall back to the asset backing the current style.
		Parent = ResolveStyleParentMaterial(CurrentBuildingMaterialStyle);
		if (!Parent)
		{
			return false;
		}
	}

	// One probe per parent, cached: the answer cannot change between sections sharing a parent, and
	// FMaterialParameterInfo lookups are not free. Keyed on the parent rather than only on the load,
	// because Blueprint can swap the building material mid-load.
	if (!bSourceColourParamProbeDone || SourceColourProbedParent != Parent)
	{
		bSourceColourParamProbeDone = true;
		SourceColourProbedParent = Parent;

		FLinearColor Unused;
		bSourceColourParamsAvailable =
			Parent->GetVectorParameterValue(FMaterialParameterInfo(TEXT("NewColour")), Unused);

		if (!bSourceColourParamsAvailable)
		{
			// Deliberately a Warning, not silence: "the importer read the colours and the material
			// cannot show them" is a different problem from "the file had no colours", and the two must
			// not look the same in a log.
			UE_LOG(LogTemp, Warning,
				TEXT("[Building %s] Source materials were imported but the building material '%s' has no ")
				TEXT("'NewColour' parameter, so per-section colours cannot be applied. Sections keep the ")
				TEXT("shared material. Fix = give the procedural building material the same colour ")
				TEXT("parameters the Datasmith master uses ('NewColour', 'Use Modified Colour', 'OpacityAmount')."),
				*GetName(), *Parent->GetName());
		}
	}

	if (!bSourceColourParamsAvailable)
	{
		return false;
	}

	UMaterialInstanceDynamic* SectionMaterial = GetOrCreateSectionMID(SectionIdx, Parent);
	if (!SectionMaterial)
	{
		return false;
	}

	SectionMaterial->SetVectorParameterValue(FName("NewColour"), SourceMaterial.BaseColour);
	SectionMaterial->SetScalarParameterValue(FName("Use Modified Colour"), 1.0f);
	// Alpha carries the source's opacity. Only pushed when the parameter exists, and only when the
	// source actually asked for transparency -- overwriting the project's own opacity handling on an
	// opaque material would fight the translucent-view feature for no reason.
	if (SourceMaterial.BaseColour.A < 1.0f)
	{
		float UnusedScalar = 0.0f;
		if (Parent->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OpacityAmount")), UnusedScalar))
		{
			SectionMaterial->SetScalarParameterValue(FName("OpacityAmount"), SourceMaterial.BaseColour.A);
		}
	}

	MobiusProceduralMeshComponent->SetMaterial(SectionIdx, SectionMaterial);
	++SourceMaterialSectionsApplied;
	return true;
}

void ARuntimeMeshBuilder::FinalizeMeshEmit()
{
	const int32 EmittedSections = PendingMeshChunks.Num();

	PendingMeshChunks.Empty();
	PendingChunkEmitIndex = 0;
	ChunkEmitTickerHandle.Reset();

	bMeshBeingBuilt = false;

	if (UMobiusCustomLoggerSubsystem* StartupLogger = GetStartupLogger())
	{
		const double DurationMs = (FPlatformTime::Seconds() - ChunkEmitStartTime) * 1000.0;
		StartupLogger->EnqueueLogMessage(FString::Printf(
			TEXT("RuntimeMeshBuilder::StaggeredEmit completed sections=%d in %.2f ms"),
			EmittedSections, DurationMs));
	}

	// A style chosen before this load finished (the widget can be driven at any time) applies to the
	// sections that have just been emitted, not to whatever was on screen when the button was pressed.
	if (bBuildingMaterialStyleChosen && EmittedSections > 0 && !bIsDatasmithAsset)
	{
		SetBuildingMaterialStyle(CurrentBuildingMaterialStyle);
	}

	// Source-material outcome, reported HERE rather than in the load summary because sections are
	// pushed across frames by the emit pump -- at load time the applied count is always 0. Logged
	// whenever the emit ran, so "the file had no colours", "the material cannot show them" and "they
	// were applied" are three distinguishable lines rather than one silence.
	if (EmittedSections > 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Building %s] source materials: %d of %d sections got a per-section colour ")
			TEXT("(colour params on building material: %s)"),
			*GetName(), SourceMaterialSectionsApplied, EmittedSections,
			bSourceColourParamProbeDone ? (bSourceColourParamsAvailable ? TEXT("available") : TEXT("MISSING"))
			                            : TEXT("never probed - no section carried a source material"));
	}

	if (!IsValid(MobiusProceduralMeshComponent))
	{
		EndLoadingWidget();
		return;
	}

	// Enable query collision now that every section is present. The staggered pump cooks per-section
	// collision (CreateMeshSection_LinearColor bCreateCollision=true), but the component is left at
	// NoCollision by the ctor and — unlike the legacy single-shot build paths — the staggered path
	// never re-enabled it, so the geometry exists yet is invisible to line traces. Cursor ray-traces
	// (e.g. flow-counter pillar placement: a bTraceComplex LineTraceByChannel against the building)
	// then hit nothing. QueryOnly + ComplexAsSimple + Block-all matches the legacy build paths and the
	// bTraceComplex trace, without enabling physics simulation.
	MobiusProceduralMeshComponent->bUseComplexAsSimpleCollision = true;
	MobiusProceduralMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MobiusProceduralMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	MobiusProceduralMeshComponent->SetSimulatePhysics(false);

	// The origin we want to broadcast is the smallest location of the mesh bounds as the mesh generator
	// for the heatmap works from left to right and bottom to top.
	const FVector HeatmapOrigin = MobiusProceduralMeshComponent->Bounds.Origin - MobiusProceduralMeshComponent->Bounds.BoxExtent;
	OnMeshBuilt.Broadcast(HeatmapOrigin, MobiusProceduralMeshComponent->Bounds.BoxExtent);

	EndLoadingWidget();
}

void ARuntimeMeshBuilder::UpdateMeshMaterial(UMaterialInstanceDynamic* InMaterial)
{
	// Check input is valid
	if(InMaterial == nullptr)
	{
		ReportError(this,
		            FString("Mesh Material Error"),
		            FString("Material is not valid"),
		            FString("Cannot update mesh material with a null instance."),
		            FString("RuntimeMeshBuilder"));

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
		ReportError(this,
		            FString("Mesh Material Error"),
		            FString("Datasmith materials unavailable"),
		            FString("Datasmith anchor is invalid or materials map is empty."),
		            FString("RuntimeMeshBuilder"));

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
		ReportError(this,
		            FString("Mesh Material Error"),
		            FString("Datasmith anchor unavailable"),
		            FString("Cannot set Datasmith mesh to translucent materials without a valid Runtime Datasmith Anchor."),
		            FString("RuntimeMeshBuilder")
		);
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}

	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		// get num materials on the mesh component and set the material to use the non modified material
		for (int32 Index = 0; Index < DatasmithMaterialMap.Key->GetNumMaterials(); Index++)
		{
			if (!DatasmithMaterialMap.Value.MeshMaterials.IsValidIndex(Index * 2 + 1)) continue;
			// set the material to use the translucent material
			DatasmithMaterialMap.Key->SetMaterial(Index,DatasmithMaterialMap.Value.MeshMaterials[Index * 2 + 1]);
		}
	}

	ApplyRefractionForCurrentView();

	UE_LOG(LogTemp, Error, TEXT("SetDatasmithMeshToTranslucentMaterials Finished"));
}

void ARuntimeMeshBuilder::SetDatasmithMeshToSolidMaterials()
{
	UE_LOG(LogTemp, Error, TEXT("SetDatasmithMeshToSolidMaterials Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		ReportError(this,
		            FString("Mesh Material Error"),
		            FString("Datasmith anchor unavailable"),
		            FString("Cannot set Datasmith mesh to solid materials without a valid Runtime Datasmith Anchor."),
		            FString("RuntimeMeshBuilder")
		);
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}

	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		// get num materials on the mesh component and set the material to use the non modified material
		for (int32 Index = 0; Index < DatasmithMaterialMap.Key->GetNumMaterials(); Index++)
		{
			if (!DatasmithMaterialMap.Value.MeshMaterials.IsValidIndex(Index * 2)) continue;
			// set the material to use the translucent material
			DatasmithMaterialMap.Key->SetMaterial(Index,DatasmithMaterialMap.Value.MeshMaterials[Index * 2]);
		}
	}

	ApplyRefractionForCurrentView();
}

void ARuntimeMeshBuilder::RecordOriginalRefraction(UMaterialInstanceDynamic* TranslucentMaterial)
{
	if (!TranslucentMaterial)
	{
		return;
	}

	// One shared MID serves many slots, so this is reached repeatedly. Only the first sighting can
	// be trusted as the original — and only a MID recorded here ever gets flattened or restored.
	if (TranslucentViewRefractionSnapshot.Contains(TranslucentMaterial))
	{
		return;
	}

	float OriginalRefractionIndex = 0.0f;
	if (!TranslucentMaterial->GetScalarParameterValue(RefractionIndexParamName, OriginalRefractionIndex))
	{
		// Master does not expose the parameter — nothing to flatten, nothing to restore.
		return;
	}

	TranslucentViewRefractionSnapshot.Add(TranslucentMaterial, OriginalRefractionIndex);

	// Datasmith meshes finish building across several ticks, so a mesh can arrive after the
	// translucent view is already on. Bring it straight into the current view rather than leaving
	// one late chunk refracting while the rest of the building does not.
	if (bTranslucentViewActive)
	{
		TranslucentMaterial->SetScalarParameterValue(RefractionIndexParamName, NoRefractionIndex);
	}
}

bool ARuntimeMeshBuilder::IsTranslucentViewActive() const
{
	for (const TPair<TWeakObjectPtr<UStaticMeshComponent>, FDatasmithMaterials>& Entry : DatasmithMaterialsMap)
	{
		const UStaticMeshComponent* Mesh = Entry.Key.Get();
		if (!Mesh)
		{
			continue;
		}

		for (int32 Index = 0; Index < Mesh->GetNumMaterials(); Index++)
		{
			// Only an opaque slot answers the question. A slot Datasmith classified as translucent
			// renders its translucent MID in both views, so it can never tell them apart.
			if (!Entry.Value.bIsOpaque.IsValidIndex(Index) || !Entry.Value.bIsOpaque[Index])
			{
				continue;
			}

			if (!Entry.Value.MeshMaterials.IsValidIndex(Index * 2 + 1))
			{
				continue;
			}

			if (Mesh->GetMaterial(Index) == Entry.Value.MeshMaterials[Index * 2 + 1])
			{
				return true;
			}
		}
	}

	return false;
}

void ARuntimeMeshBuilder::ApplyRefractionForCurrentView()
{
	// Read the view off the materials that are actually on the meshes rather than trusting which
	// function ran last. The UI drives these entry points from Blueprint and a single mode change
	// calls several of them, so "the last one called wins" is not something this can rely on.
	bTranslucentViewActive = IsTranslucentViewActive();

	for (const TPair<TWeakObjectPtr<UMaterialInstanceDynamic>, float>& Recorded : TranslucentViewRefractionSnapshot)
	{
		if (UMaterialInstanceDynamic* TranslucentMaterial = Recorded.Key.Get())
		{
			TranslucentMaterial->SetScalarParameterValue(
				RefractionIndexParamName,
				bTranslucentViewActive ? NoRefractionIndex : Recorded.Value);
		}
	}

	// The record is never consumed here. It belongs to the MIDs and is dropped with them when the
	// Datasmith scene is torn down, so the view can be entered and left any number of times.
}

void ARuntimeMeshBuilder::UpdateDatasmithMeshOpacity(float Opacity)
{
	UE_LOG(LogTemp, Warning, TEXT("UpdateDatasmithMeshOpacity Called"));

	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		ReportError(this,
					FString("Mesh Material Error"),
					FString("Datasmith anchor unavailable"),
					FString("Cannot update Datasmith mesh opacity without a valid Runtime Datasmith Anchor."),
					FString("RuntimeMeshBuilder::UpdateDatasmithMeshOpacity")
		);
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
		ReportError(this,
		            FString("Mesh Material Error"),
		            FString("Datasmith anchor unavailable"),
		            FString("Cannot set Datasmith mesh box dissolve without a valid Runtime Datasmith Anchor."),
		            FString("RuntimeMeshBuilder::BoxDissolveDatasmithMesh")
		);
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
		ReportError(this,
		            FString("Mesh Material Error"),
		            FString("Datasmith anchor unavailable"),
		            FString("Cannot set Datasmith mesh to use modified colour without a valid Runtime Datasmith Anchor."),
		            FString("RuntimeMeshBuilder::SetDatasmithToUseModifiedColour"));
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
		ReportError(this,
		            FString("Mesh Material Error"),
		            FString("Datasmith anchor unavailable"),
		            FString("Cannot set Datasmith mesh to use clear coat materials without a valid Runtime Datasmith Anchor."),
		            FString("RuntimeMeshBuilder::SetDatasmithMeshToUseClearCoatMaterials"));
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
		ReportError(this,
		            FString("Mesh Material Error"),
		            FString("Datasmith anchor unavailable"),
		            FString("Cannot set Datasmith mesh dissolve size and origin without a valid Runtime Datasmith Anchor."),
		            FString("RuntimeMeshBuilder::SetDatasmithDissolveMeshSizeAndOrigin"));
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
		ReportError(this,
		            FString("Mesh Material Error"),
		            FString("Datasmith anchor unavailable"),
		            FString("Cannot set Datasmith mesh to original material style without a valid Runtime Datasmith Anchor."),
		            FString("RuntimeMeshBuilder::SetDatasmithToOriginalMatStyle"));

		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}

	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{

		for (int32 Index = 0; Index < DatasmithMaterialMap.Key->GetNumMaterials(); Index++)
		{
			if (!DatasmithMaterialMap.Value.MeshMaterials.IsValidIndex(Index * 2 + 1)) continue;

			// set the material shading to not use the modified colour material or and to use the modified material so we are able to override opacity with the slider
			DatasmithMaterialMap.Value.MeshMaterials[Index * 2]->SetScalarParameterValue(FName("Use Modified Colour"), 0.0f);
			DatasmithMaterialMap.Value.MeshMaterials[Index * 2]->SetScalarParameterValue(FName("Use Modified Material"), 1.0f);

			DatasmithMaterialMap.Value.MeshMaterials[Index * 2 + 1]->SetScalarParameterValue(FName("Use Modified Colour"), 0.0f);
			DatasmithMaterialMap.Value.MeshMaterials[Index * 2 + 1]->SetScalarParameterValue(FName("Use Modified Material"), 1.0f);

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

	// Translucent slots go back to rendering the same MID the translucent view flattened, so the
	// refraction it took away has to come back here. This is the route the UI actually uses to
	// leave the translucent view — SetDatasmithMeshToSolidMaterials never runs in practice.
	ApplyRefractionForCurrentView();
}

void ARuntimeMeshBuilder::SetMaterialOnMesh()
{
	// As the mesh may not exist we check if it does
	if(MobiusProceduralMeshComponent != nullptr)
	{
		// ---------------------------------------------------------------------------------------------
		// MUST NOT stamp one shared material over every section unconditionally.
		//
		// This function used to do exactly that, and it silently undid every imported source colour.
		// The sequence, measured 2026-08-12: FinalizeMeshEmit applies per-section colours, THEN
		// broadcasts OnMeshBuilt; WBP_SetBuildingMat is bound to OnMeshBuilt and responds by calling
		// UpdateMeshMaterial, which lands here and overwrote all N sections with the single
		// MobiusMaterialInstanceDynamic. The building came out one flat colour with the window frames
		// indistinguishable from the walls -- which looks like the importer failing to read materials
		// and is not that at all.
		//
		// So: honour the chosen style if there is one, otherwise re-apply each section's own source
		// colour on top of the material being set, and only fall back to the shared material for
		// sections that genuinely have no source colour.
		// ---------------------------------------------------------------------------------------------
		if (bBuildingMaterialStyleChosen && !bIsDatasmithAsset)
		{
			SetBuildingMaterialStyle(CurrentBuildingMaterialStyle);
			return;
		}

		const int32 NumSections = MobiusProceduralMeshComponent->GetNumSections();
		for (int32 SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
		{
			const FMobiusMeshMaterial* Source = SectionSourceMaterials.IsValidIndex(SectionIdx)
				                                    ? &SectionSourceMaterials[SectionIdx]
				                                    : nullptr;

			// ApplySourceMaterialToSection parents its MID on MobiusMaterialInstanceDynamic, i.e. on
			// whatever material was just supplied -- so the caller's choice of material is respected and
			// the source colour is layered on top of it, rather than one overriding the other.
			if (!Source || !ApplySourceMaterialToSection(SectionIdx, *Source))
			{
				// Never hand the mesh a MID whose parent UE refused (the MID_MID case) -- it has no
				// material chain and renders as nothing. Substitute the asset behind the current style.
				UMaterialInterface* Fallback = MobiusMaterialInstanceDynamic;
				if (MobiusMaterialInstanceDynamic != nullptr && MobiusMaterialInstanceDynamic->Parent == nullptr)
				{
					Fallback = ResolveStyleParentMaterial(CurrentBuildingMaterialStyle);
				}
				if (Fallback != nullptr)
				{
					MobiusProceduralMeshComponent->SetMaterial(SectionIdx, Fallback);
				}
			}
		}
	}
	else
	{
		ReportError(this,
		            FString("Mesh Material Error"),
		            FString("Procedural mesh missing"),
		            FString("Procedural mesh component is not valid."),
		            FString("RuntimeMeshBuilder::SetMaterialOnMesh"));

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

void ARuntimeMeshBuilder::DecideAndExecuteSpawnStrategy()
{
	// PerformanceUtilSubsystem owns all FPS tracking — read from there rather than duplicating logic
	UPerformanceUtilSubsystem* PerfUtil = GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>();
	const float InstantFPS = PerfUtil ? PerfUtil->GetCurrentFPS()  : 9999.0f;
	const float AverageFPS = PerfUtil ? PerfUtil->GetSmoothedFPS() : 9999.0f;

	// Lightweight memory heuristic — single struct read, no allocations, safe on game thread
	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	const float MemFreeRatio = MemStats.TotalPhysical > 0
		? (float)MemStats.AvailablePhysical / (float)MemStats.TotalPhysical
		: 1.0f;
	const bool bMemoryConstrained = MemFreeRatio < MemFreeThreshold;

	// Flush immediately only if avg FPS is above threshold, instant FPS is not
	// borderline (within ±10 of threshold), and memory is not constrained
	const bool bAvgTooLow  = AverageFPS  < SpawnFPSThreshold;
	const bool bInstantLow = InstantFPS  <= (SpawnFPSThreshold + 10.0f);
	const bool bUseBatched = bAvgTooLow || bInstantLow || bMemoryConstrained;

	if (!bUseBatched)
	{
		FlowCounterSpawnerComponent->FlushRemainingSpawns();
		return;
	}

	// Batched fallback — tick queue handles spawning naturally, build reason string for popup
	TArray<FString> Reasons;
	if (bMemoryConstrained)
	{
		Reasons.Add(FString::Printf(TEXT("available memory is low (%.0f%% free)"),
			MemFreeRatio * 100.0f));
	}
	if (bAvgTooLow)
	{
		Reasons.Add(FString::Printf(TEXT("average FPS (%.0f) is below target (%.0f)"),
			AverageFPS, SpawnFPSThreshold));
	}
	else if (bInstantLow)
	{
		Reasons.Add(FString::Printf(TEXT("current FPS (%.0f) is near target (%.0f)"),
			InstantFPS, SpawnFPSThreshold));
	}

	const FString ReasonStr = Reasons.Num() > 0
		? FString::Join(Reasons, TEXT(" and "))
		: TEXT("performance conditions are not met");

	const FString Body = FString::Printf(
		TEXT("Door flow counters are using batched spawn because %s. ")
		TEXT("Doors will appear progressively over the next several seconds.\n\n")
		TEXT("Note: auto door spawning will be configurable in a future update. ")
		TEXT("If instability persists, consider reducing or removing tagged doors in the source model."),
		*ReasonStr);

	if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
	{
		Feedback->ReportError(
			FText::FromString(TEXT("Performance Notice")),
			FText::FromString(TEXT("Door Spawning: Batched Mode Active")),
			FText::FromString(Body),
			FText::GetEmpty(),
			EMobiusErrorSeverity::Warning,
			/*bShowPrompt=*/true);
	}

	if (UMobiusCustomLoggerSubsystem* Logger = GetStartupLogger())
	{
		Logger->EnqueueLogMessage(FString::Printf(
			TEXT("FlowCounter spawn strategy: batched (avg=%.1f inst=%.1f threshold=%.1f memFree=%.1f%%)"),
			AverageFPS, InstantFPS, SpawnFPSThreshold, MemFreeRatio * 100.0f));
	}
}

UMaterialInterface* ARuntimeMeshBuilder::GetUnsupportedMaterial()
{
	if (UnsupportedMaterialCache)
	{
		return UnsupportedMaterialCache;
	}

	// Vivid "render error" purple, shown when a Twinmotion-sourced slot can't be satisfied in a
	// packaged build (its master was excluded from cook under Epic's Twinmotion EULA). This asset
	// is project-owned (not Twinmotion-derived) and lives in the always-cooked RuntimeMeshGenerator
	// folder, so it is always available in the package.
	static const TCHAR* UnsupportedPath = TEXT("/Game/01_Dev/RuntimeMeshGenerator/M_MobiusUnsupported.M_MobiusUnsupported");
	UnsupportedMaterialCache = LoadObject<UMaterialInterface>(nullptr, UnsupportedPath);

	if (!UnsupportedMaterialCache)
	{
		// Last-resort fallback so the slot is at least visibly wrong rather than empty.
		UnsupportedMaterialCache = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	return UnsupportedMaterialCache;
}

void ARuntimeMeshBuilder::CreateDatasmithMaterials()
{
	const double DatasmithStart = FPlatformTime::Seconds();
	UMobiusCustomLoggerSubsystem* StartupLogger = GetStartupLogger();
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
		ReportError(this,
		            FString("Mesh Material Error"),
		            FString("Datasmith anchor unavailable"),
		            FString("Cannot create Datasmith materials without a valid Runtime Datasmith Anchor."),
		            FString("RuntimeMeshBuilder::CreateDatasmithMaterials"));
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}

	auto DataComps = RuntimeDatasmithAnchor->GetComponents();
	UE_LOG(LogTemp, Warning, TEXT("Number of Data Components: %d"), DataComps.Num());

	PendingDatasmithMeshes.Reset();
	bDatasmithMaterialSetupInProgress = false;
	bHeatmapBroadcastPending = false;
	bTwinmotionRefusedThisLoad = false;

	if (DataComps.Num() == 0)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Datasmith Error"),
				FText::FromString("Datasmith components missing"),
				FText::FromString("Datasmith anchor returned no components."),
				FText::FromString("RuntimeMeshBuilder"));
		}
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

	// Defer OnMeshBuilt until PendingDatasmithMeshes drains (see ProcessPendingDatasmithMeshes).
	// DatasmithRuntime registers components over multiple frames: at this point some
	// UStaticMeshComponents may be queued but not yet IsRegistered/IsRenderStateCreated,
	// which means their Bounds are zero and their render data is empty. The heatmap consumer
	// walks DataComps on the broadcast, so firing now produces partial heatmaps. Bounds are
	// recomputed at drain time from a fresh walk.
	UE_LOG(LogTemp, Warning, TEXT("Datasmith initial GlobalBounds Center: %s Extent: %s (deferring broadcast)"),
	       *GlobalBounds.GetCenter().ToString(), *GlobalBounds.GetExtent().ToString());

	bHeatmapBroadcastPending = true;

	// We now have a queue of meshes to process over multiple frames.
	bDatasmithMaterialSetupInProgress = true;

	// Do NOT call EndLoadingWidget or BeginSpawning here; we'll do that when the queue is empty.
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
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Material Load Error"),
				FText::FromString("Failed to load material"),
				FText::FromString(FString::Printf(TEXT("Failed to load the material: %s"), *MaterialPath)),
				FText::FromString("RuntimeMeshBuilder"));
		}
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
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Created =
		MaterialCache.CreateMaterialInstancesUsingCache(InMaterial, TranslucentMaterialPath, false);

	// Capture the untouched refraction here, at generation, not on the first switch to the
	// translucent view. MaterialCache hands back a shared MID and the view can be entered and left
	// any number of times in any order, so a snapshot taken on a toggle is only ever as good as the
	// toggle ordering — and the ordering is decided in Blueprint.
	for (UMaterialInstanceDynamic* Material : Created)
	{
		RecordOriginalRefraction(Material);
	}

	return Created;
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
			PendingCollisionEnable.RemoveAt(Index, 1, EAllowShrinking::No);
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
			PendingCollisionEnable.RemoveAt(Index, 1, EAllowShrinking::No);
			continue;
		}

		if (AActor* MeshOwner = Mesh->GetOwner())
		{
			if (MeshOwner->IsActorBeingDestroyed())
			{
				WeakMesh.Reset();
				PendingCollisionEnable.RemoveAt(Index, 1, EAllowShrinking::No);
				continue;
			}
		}
		if (UStaticMesh* StaticMesh = Mesh->GetStaticMesh())
		{
			if (StaticMesh->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
			{
				WeakMesh.Reset();
				PendingCollisionEnable.RemoveAt(Index, 1, EAllowShrinking::No);
				continue;
			}

			if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
			{
				if (BodySetup->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
				{
					WeakMesh.Reset();
					PendingCollisionEnable.RemoveAt(Index, 1, EAllowShrinking::No);
					continue;
				}

				// If the body instance is not valid, avoid touching collision settings
				FBodyInstance* BodyInstance = Mesh->GetBodyInstance();
				if (!BodyInstance || !BodyInstance->IsValidBodyInstance())
				{
					WeakMesh.Reset();
					PendingCollisionEnable.RemoveAt(Index, 1, EAllowShrinking::No);
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
				PendingCollisionEnable.RemoveAt(Index, 1, EAllowShrinking::No);
				continue;
			}
		}
		else
		{
			WeakMesh.Reset();
			PendingCollisionEnable.RemoveAt(Index, 1, EAllowShrinking::No);
			continue;
		}
		WeakMesh.Reset();
		PendingCollisionEnable.RemoveAt(Index, 1, EAllowShrinking::No);
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
		PendingDatasmithMeshes.RemoveAtSwap(Index, 1, EAllowShrinking::No);

		if (Mesh)
		{
			BuildDatasmithMaterialsForMesh(Mesh);
		}

		++Processed;
	}

	if (PendingDatasmithMeshes.Num() == 0)
	{
		// We're done – clear the flag and finish up the "import finished" flow.
		bDatasmithMaterialSetupInProgress = false;

		// All meshes have now been visited by BuildDatasmithMaterialsForMesh, which means each
		// was IsRegistered + IsRenderStateCreated when touched. Safe window to (a) recompute the
		// aggregate bounds from fully-resolved components and (b) hand off to the heatmap layer.
		if (bHeatmapBroadcastPending)
		{
			FBox FinalBounds(ForceInit);
			if (RuntimeDatasmithAnchor && !RuntimeDatasmithAnchor->IsActorBeingDestroyed())
			{
				auto DataComps = RuntimeDatasmithAnchor->GetComponents();
				for (auto DataComp : DataComps)
				{
					USceneComponent* SceneComp = Cast<USceneComponent>(DataComp);
					if (!SceneComp) continue;

					TArray<USceneComponent*> FinalChildren;
					SceneComp->GetChildrenComponents(true, FinalChildren);
					for (USceneComponent* Child : FinalChildren)
					{
						UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(Child);
						if (!MeshComp || MeshComp->IsBeingDestroyed() || !MeshComp->IsRegistered())
						{
							continue;
						}
						FinalBounds += MeshComp->Bounds.GetBox();
					}
				}
			}

			bHeatmapBroadcastPending = false;

			if (FinalBounds.IsValid)
			{
				const FVector BoundsCenter = FinalBounds.GetCenter();
				const FVector BoundsExtent = FinalBounds.GetExtent();
				const FVector HeatmapOrigin = BoundsCenter - BoundsExtent;

				UE_LOG(LogTemp, Warning, TEXT("Datasmith final GlobalBounds Center: %s Extent: %s (broadcast)"),
				       *BoundsCenter.ToString(), *BoundsExtent.ToString());

				if (UMobiusCustomLoggerSubsystem* StartupLogger = GetStartupLogger())
				{
					StartupLogger->EnqueueLogMessage(FString::Printf(
						TEXT("RuntimeMeshBuilder::Datasmith OnMeshBuilt broadcast origin=%s extent=%s"),
						*HeatmapOrigin.ToString(), *BoundsExtent.ToString()));
				}

				OnMeshBuilt.Broadcast(HeatmapOrigin, BoundsExtent);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Datasmith drain completed but no valid bounds; skipping OnMeshBuilt"));
			}
		}

#if MOBIUS_TWINMOTION_PACKAGED_DISABLED
		// One-shot notice per load: at least one slot was a Twinmotion-sourced material that can't
		// be shipped in a packaged build (Epic Twinmotion EULA). Geometry was drawn with the purple
		// M_MobiusUnsupported placeholder instead of the excluded Twinmotion masters.
		if (bTwinmotionRefusedThisLoad)
		{
			if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
			{
				Feedback->ReportError(
					FText::FromString(TEXT("Twinmotion Materials Not Supported")),
					FText::FromString(TEXT("Model exported from Twinmotion")),
					FText::FromString(TEXT("This model uses Twinmotion materials, which cannot be included in the packaged application under Epic's Twinmotion EULA. The geometry is shown without materials (marked in purple). Re-export the model using non-Twinmotion (Datasmith) materials to restore them.")),
					FText::FromString(TEXT("RuntimeMeshBuilder")));
			}
			bTwinmotionRefusedThisLoad = false;
		}
#endif

		EndLoadingWidget();
		FlowCounterSpawnerComponent->BeginSpawning();
		DecideAndExecuteSpawnStrategy();
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

	// MasterTypeCache is now a class-level static (see header) so EndPlay can
	// empty it; UMaterial* entries become stale across PIE sessions otherwise.

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
		UMaterial* ParentMaterial = Material ? Material->GetMaterial() : nullptr;

#if MOBIUS_TWINMOTION_PACKAGED_DISABLED
		// Packaged build: the Twinmotion master materials are excluded from cook (Epic Twinmotion
		// EULA — see MobiusCore.Build.cs / Config/DefaultGame.ini). A Datasmith slot that resolves
		// to no material is a model that needed Twinmotion content we cannot ship. Substitute the
		// purple "unsupported" placeholder, flag the load for a one-shot notice, and skip the remap.
		// Non-Twinmotion Datasmith imports resolve to the engine DatasmithRuntime masters and keep
		// a valid material here, so they fall through untouched.
		if (!Material || !ParentMaterial)
		{
			if (UMaterialInterface* Placeholder = GetUnsupportedMaterial())
			{
				MeshComp->SetMaterial(Index, Placeholder);
			}
			bTwinmotionRefusedThisLoad = true;
			continue;
		}
#else
		if (!Material)
		{
			continue;
		}
		if (!ParentMaterial)
		{
			continue;
		}
#endif

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

			if (NewType == EDatasmithMasterType::Unknown)
			{
				UE_LOG(LogTemp, Warning, TEXT("BuildDatasmithMaterialsForMesh: Unrecognized parent material '%s' on %s"),
					*MatName.ToString(), *MeshComp->GetName());
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
			if (MaterialInstances.Num() < 2)
			{
				UE_LOG(LogTemp, Error, TEXT("BuildDatasmithMaterialsForMesh: TMStdOpaque creation returned %d MIDs (expected 2) for %s slot %d"),
					MaterialInstances.Num(), *MeshComp->GetName(), Index);
				continue;
			}
			DatasmithMaterials.bIsOpaque.Add(true);
			break;

		case EDatasmithMasterType::TMStdTranslucent:
			MaterialInstances.Append(CreateOpaqueMaterials(Material));
			MaterialInstances.Append(CreateTranslucentMaterials(Material, /*bIsOpaque=*/false));
			if (MaterialInstances.Num() < 2)
			{
				UE_LOG(LogTemp, Error, TEXT("BuildDatasmithMaterialsForMesh: TMStdTranslucent creation returned %d MIDs (expected 2) for %s slot %d"),
					MaterialInstances.Num(), *MeshComp->GetName(), Index);
				continue;
			}
			DatasmithMaterials.bIsOpaque.Add(false);
			break;

		case EDatasmithMasterType::RuntimeOpaque:
			MaterialInstances.Append(CreateRuntimeOpaqueMaterials(Material));
			MaterialInstances.Append(CreateRuntimeTranslucentMaterials(Material, /*bIsOpaque=*/true));
			if (MaterialInstances.Num() < 2)
			{
				UE_LOG(LogTemp, Error, TEXT("BuildDatasmithMaterialsForMesh: RuntimeOpaque creation returned %d MIDs (expected 2) for %s slot %d"),
					MaterialInstances.Num(), *MeshComp->GetName(), Index);
				continue;
			}
			DatasmithMaterials.bIsOpaque.Add(true);
			break;

		case EDatasmithMasterType::RuntimeTranslucent:
			MaterialInstances.Append(CreateRuntimeOpaqueMaterials(Material));
			MaterialInstances.Append(CreateRuntimeTranslucentMaterials(Material, /*bIsOpaque=*/false));
			if (MaterialInstances.Num() < 2)
			{
				UE_LOG(LogTemp, Error, TEXT("BuildDatasmithMaterialsForMesh: RuntimeTranslucent creation returned %d MIDs (expected 2) for %s slot %d"),
					MaterialInstances.Num(), *MeshComp->GetName(), Index);
				continue;
			}
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

	// Cache for later toggling — only if we actually created materials
	if (DatasmithMaterials.MeshMaterials.Num() > 0)
	{
		DatasmithMaterialsMap.Add(MeshComp, MoveTemp(DatasmithMaterials));

		// Enable collision for this mesh (still batched separately)
		EnqueueCollisionEnable(MeshComp);
	}
}

void ARuntimeMeshBuilder::ReportError(UObject* ContextObject, FString ErrorTitleBar, FString ErrorTitle,
                                      FString ErrorMessage, FString ErrorLocation)
{
	if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(ContextObject))
	{
		Feedback->ReportError(
			FText::FromString(ErrorTitleBar),
			FText::FromString(ErrorTitle),
			FText::FromString(ErrorMessage),
			FText::FromString(ErrorLocation));
	}
}

UMobiusCustomLoggerSubsystem* ARuntimeMeshBuilder::GetStartupLogger()
{
	return GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
}
