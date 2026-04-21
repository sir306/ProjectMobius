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

#pragma once

#include "CoreMinimal.h"
#include "Actors/FlowCounter.h"
#include "AsyncAssimpMeshLoader.h"    // FAssimpSubmeshBuffers for staggered emit queue
#include "Containers/Ticker.h"        // FTSTicker for staggered section emit
#include "GameFramework/Actor.h"
#include "Interfaces/AssimpInterface.h"
#include "Interfaces/ProjectMobiusInterface.h"
#include "UObject/NameTypes.h"        // FName + GetTypeHash(FName)
#include "UObject/WeakObjectPtr.h"    // TWeakObjectPtr hashing (for safety)
#include "Materials/MaterialCache.h"
#include "RuntimeMeshBuilder.generated.h"


class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialInstanceConstant;
class UMaterial;
class UTexture;
class UMobiusCustomLoggerSubsystem;

/** Delegates */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMeshBuilt, FVector, BoundOrigins, FVector, BoundExtents);

// Which master family to use for a given mesh's slot classification
enum class EDatasmithMasterType : uint8
{
	Unknown,
	TMStdOpaque,
	TMStdTranslucent,
	RuntimeOpaque,
	RuntimeTranslucent
};

/** Structs */
/** */
USTRUCT()
struct FDatasmithMaterials
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> MeshMaterials;

	UPROPERTY()
	TArray<bool> bIsOpaque;
};

// One pending mesh that still needs its Datasmith MIDs created/applied
USTRUCT()
struct FPendingDatasmithMesh
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UStaticMeshComponent> Mesh;

	// We just store the component; slots are processed inside the worker
	// using the EDatasmithMasterType classification cache.
};


/** */
UCLASS()
class MOBIUSCORE_API ARuntimeMeshBuilder : public AActor, public IAssimpInterface, public IProjectMobiusInterface
{
	GENERATED_BODY()

public:
#pragma region PUBLIC_METHODS
	// Sets default values for this actor's properties
	ARuntimeMeshBuilder();

	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Called every frame
	virtual void Tick(float DeltaTime) override;

	virtual void OnConstruction(const FTransform& Transform) override;

	/**
	* Function to generate the Mobius Runtime Mesh from the given vertices, triangles, and normals
	*
	* @param InVertices The Vertices to generate the mesh from
	* @param InTriangles The Triangles to generate the mesh from
	* @param InNormals The Normals to generate the mesh from
	*
	*/
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Generation")
	void GenerateMobiusMesh(TArray<FVector> InVertices, TArray<int32> InTriangles, TArray<FVector> InNormals);

	/**
	 * Function to get the Mesh Data via the Assimp Interface
	 *
	 * @param MeshRotationOffset Different Modeling software have different coordinate systems, and may require rotation
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Generation")
	void GetMeshDataFromFile(FRotator MeshRotationOffset = FRotator::ZeroRotator);
	void ResetMeshCollisionAndPhysics();

	/**
	 * Update the Mesh file name, this is bound to the OnMeshFileChanged Delegate in the Game Instance
	 * and will call the methods to get the mesh data and rebuild the mesh
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|UpdateMethods")
	void UpdateMeshFileName();

private:
	/**
	 * Continuation of UpdateMeshFileName that runs after the DatasmithRuntime
	 * SceneImporter has processed its ResetScene task (one game-thread tick
	 * after Reset()). Dispatches to the .udatasmith/.ifc or fbx loader using
	 * the member MeshFileName. Called directly on first load, via
	 * DeferredLoadTimerHandle otherwise.
	 */
	void ContinueLoadAfterPurge();

	/** Pending one-shot timer for the deferred continuation. Cleared/replaced
	 *  on every new UpdateMeshFileName so rapid switches don't stack callbacks. */
	FTimerHandle DeferredLoadTimerHandle;

	/**
	 * Release the heavy data (vertex/index buffers, collision meshes) owned by
	 * UObjects the DatasmithRuntime plugin keeps alive via its static
	 * FAssetRegistry::RegistrationMap. Called from EndPlay so that even though
	 * the UObject shells survive past PIE stop, their ~500MB of GPU/CPU
	 * resources are freed.
	 */
	void ReleaseDatasmithSceneResources();

	/**
	 * Cached master-material classification. Class-level static (instead of
	 * function-local) so EndPlay can clear it — UMaterial* entries become stale
	 * across PIE sessions and would otherwise dereference freed pointers on the
	 * next load.
	 */
	static TMap<class UMaterial*, EDatasmithMasterType> MasterTypeCache;

public:

	/**
	 * Function to update the Mesh Data via the Async Assimp
	 *
	 * @param PathToMesh The Path to the Mesh to load
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|UpdateMethods")
	void AsyncUpdateMesh(const FString PathToMesh);

	/**
	 * Method to bind to the OnLoadMeshDataComplete Delegate so the generator can get the mesh data
	 * from the runnable thread
	 */
	UFUNCTION()
	void GetTheAsyncMeshData();

	/**
	 * Method to Update the material on the mesh with the given material, this is called after the mesh has been generated
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void UpdateMeshMaterial(UMaterialInstanceDynamic* InMaterial);

	// Datasmith change material methods
	/**
	 * Set the Datasmith mesh materials on the mesh to use the non modified material setting
	 *
	 * @param[bool] bUseNonModifiedMaterials if true the mesh will use the non modified materials, default is true
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithMeshToUseNonModifiedMaterials(bool bUseNonModifiedMaterials = true);


	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithMeshToTranslucentMaterials();

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithMeshToSolidMaterials();

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void UpdateDatasmithMeshOpacity(float Opacity = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void BoxDissolveDatasmithMesh(bool bDissolve = true);

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithToUseModifiedColour(bool bUseModifiedColour = true, FLinearColor NewColour = FLinearColor::White);

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithMeshToUseClearCoatMaterials(bool bUseClearCoatMaterials = true);

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithDissolveMeshSizeAndOrigin(FVector Origin, FVector Extents);

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithToOriginalMatStyle();


#pragma endregion PUBLIC_METHODS

#pragma region PRIVATE_METHODS
private:
	/** Internal method to set the material on the mesh - TODO: this will need to be converted to be called via a delegate  */
	void SetMaterialOnMesh();

	/** */
	void EndLoadingWidget();

	/** Internal Method that creates and maps datasmith materials */
	void CreateDatasmithMaterials();

	TArray<TObjectPtr<UMaterialInstanceDynamic>> CreateMaterialInstances(UMaterialInterface* InMaterial, const FString& MaterialPath);

	/** Internal Method to handle Opaque Material creation of datasmith materials */
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CreateOpaqueMaterials(UMaterialInterface* InMaterial);

	/** Internal Method to handle Translucent Material creation of datasmith materials */
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CreateTranslucentMaterials(UMaterialInterface* InMaterial, bool bIsOpaque = false);


	TArray<TObjectPtr<UMaterialInstanceDynamic>> CreateRuntimeOpaqueMaterials(UMaterialInterface* InMaterial);

	TArray<TObjectPtr<UMaterialInstanceDynamic>> CreateRuntimeTranslucentMaterials(UMaterialInterface* InMaterial, bool bIsOpaque = false);

	void EnqueueCollisionEnable(UStaticMeshComponent* Mesh);
	void ProcessPendingCollisionEnables(float DeltaSeconds);

	void ProcessPendingDatasmithMeshes(float DeltaSeconds);
	void BuildDatasmithMaterialsForMesh(UStaticMeshComponent* MeshComp);

#pragma endregion PRIVATE_METHODS

#pragma region PUBLIC_PROPERTIES_AND_COMPONENTS
public:
	/** The Procedural Mesh Component used for generating meshes at runtime or on construction */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Component")
	class UProceduralMeshComponent* MobiusProceduralMeshComponent;

	/**
	 * Cap per-section triangle count. Submeshes exceeding this are partitioned across extra sections
	 * with index-remapped vertex buffers (boundary vertices duplicated across adjacent chunks). High
	 * value keeps most submeshes whole; lowering it spreads game-thread CreateMeshSection cost across
	 * more sections (more draw calls, less hitch per section).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshGenerator|Component", meta=(ClampMin="1000"))
	int32 MaxTrisPerSection = 100000;

	/**
	 * Number of mesh sections pushed to the procedural mesh component per game-thread tick during
	 * staggered emit. 1 = one section per frame (smoothest, finishes over N frames); higher values
	 * compress the finish window at the cost of a bigger per-frame FScene_AddPrimitive spike.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshGenerator|Component", meta=(ClampMin="1"))
	int32 SectionsEmittedPerTick = 1;

	/**
	 * Rollback knob for the multi-section pipeline. true  = per-submesh chunks (default);
	 * false = legacy flatten-to-single-section-0 path. Use false to A/B against the old
	 * behaviour if a regression surfaces; intended to be removed once the tiled path has
	 * shipped a release.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshGenerator|Component")
	bool bEnableMultiSectionBatching = true;

	/** Flow Counter Spawner - handles spawning flow counters */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Component")
	class UFlowCounterSpawnerComponent* FlowCounterSpawnerComponent;

	/** Holds the filename to the mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|MeshData")
	FString MeshFileName;

	/** Async Loader for assimp */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshGenerator|AsyncLoader")
	TObjectPtr<class UAsyncAssimpMeshLoader> AsyncAssimpLoader = nullptr;

	/** To avoid material setting on mesh being generated we set this bool flag */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Material")
	bool bMeshBeingBuilt = false;

	/** Delegate we use to broadcast when the model has been built */
	UPROPERTY(EditAnywhere, BlueprintAssignable, Category = "MeshGenerator|Delegates")
	FOnMeshBuilt OnMeshBuilt;

	/** For Datasmith assets we need to store the imported content to a Runtime Datasmith actor */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Datasmith")
	TObjectPtr<class ADatasmithRuntimeActor> RuntimeDatasmithAnchor;

	/** For Datasmith we need to store the materials variations in a Map as a datasmith actor is made up of multiple
	 * meshes and materials, and we should only loop search once then only loop over this map */
	UPROPERTY(EditAnywhere, Category = "MeshGenerator|Datasmith")
	TMap<TWeakObjectPtr<UStaticMeshComponent>, FDatasmithMaterials> DatasmithMaterialsMap;

	/** This bool is used for working out whether this is a datasmith asset or using the procedural mesh component */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Datasmith")
	bool bIsDatasmithAsset = false;


	/*
	* Array to store the Procedural Meshes Vertex Colors to Generate
	* - These are stored as Linear Colour Structures, length must be the same as the length of vertices array
	*/

	/*
	* Array to store the Procedural Meshes Tangents to Generate
	* - These are stored as Proc Mesh Tangent Structures,
	* length must be the same as the length of vertices array
	*/

	/***/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshGenerator|FlowCounter")
	TSubclassOf<AFlowCounter> FlowCounterToAutoSpawn = nullptr;

protected:

	/** Door meshes we still need to spawn counters for (weak to avoid dangling refs). */
	UPROPERTY()
	TArray<TWeakObjectPtr<UStaticMeshComponent>> PendingDoorMeshes;

	/** How many flow counters to spawn per tick to smooth out hitches. */
	UPROPERTY(EditAnywhere, Category="Flow Counters")
	int32 MaxFlowCountersPerTick = 5;

	/** Are we currently processing the pending door queue? */
	bool bIsSpawningFlowCounters = false;

	/** Shared material cache used for Datasmith and runtime materials. */
	FMaterialCache MaterialCache;

	/** Meshes that still need their Datasmith materials created/applied. */
	UPROPERTY()
	TArray<FPendingDatasmithMesh> PendingDatasmithMeshes;

	/** How many Datasmith meshes we process per frame to avoid hitches. */
	UPROPERTY(EditAnywhere, Category="MeshGenerator|Datasmith")
	int32 MaxDatasmithMeshesPerFrame = 25;

	/** Are we currently in the middle of batched Datasmith material setup? */
	bool bDatasmithMaterialSetupInProgress = false;

	/**
	 * Datasmith registers components across frames — bounds/render state on individual
	 * UStaticMeshComponents only settle as they register. OnMeshBuilt can't fire until all
	 * queued comps have been processed or the heatmap consumer walks partial geometry.
	 * This flag marks "scene import done, queue still draining — fire once drained".
	 */
	bool bHeatmapBroadcastPending = false;

private:
	/** TODO: We eventually want to get the mesh material and apply our materials to it as a mask or material function to it
	 * Material Instance Dynamic to apply to the Procedural Mesh Component after a mesh has been generated and set with
	 * the UI from user input.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Material", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMaterialInstanceDynamic> MobiusMaterialInstanceDynamic = nullptr;

	// Components that still need collision turned on
	UPROPERTY()
	TArray<TWeakObjectPtr<UStaticMeshComponent>> PendingCollisionEnable;

	// How many components we allow per frame
	UPROPERTY(EditAnywhere, Category="Collision")
	int32 MaxCollisionEnablesPerFrame = 10;

	// Are we currently resetting / swapping out the mesh and Datasmith anchor?
	UPROPERTY(Transient)
	bool bIsResettingForNewLoad = false;

	/**
	 * Mesh sections queued for staggered emit. GetTheAsyncMeshData fills this, then EmitNextChunkSection
	 * drains it one (or SectionsEmittedPerTick) at a time from the core ticker. Empty when idle.
	 */
	TArray<FAssimpSubmeshBuffers> PendingMeshChunks;

	/** Index of the next chunk in PendingMeshChunks to push via CreateMeshSection_LinearColor. */
	int32 PendingChunkEmitIndex = 0;

	/** Ticker handle for the staggered emit pump. Reset once all chunks are pushed. */
	FTSTicker::FDelegateHandle ChunkEmitTickerHandle;

	/** FPlatformTime::Seconds() sample taken when the emit pump started — used to log total wall time. */
	double ChunkEmitStartTime = 0.0;

	/** Pump that pushes up to SectionsEmittedPerTick sections per frame. Returns false once drained. */
	bool EmitNextChunkSection(float DeltaTime);

	/** Runs once after the final section is emitted: broadcast bounds, close loading widget, clear flags. */
	void FinalizeMeshEmit();

	/** Report a RuntimeMeshBuilder error through the user feedback subsystem. */
	void ReportError(UObject* ContextObject, FString ErrorTitleBar, FString ErrorTitle, FString ErrorMessage, FString ErrorLocation);

	/** Access the startup logger subsystem without an extra dependency on the game instance. */
	static UMobiusCustomLoggerSubsystem* GetStartupLogger();

#pragma endregion PUBLIC_PROPERTIES_AND_COMPONENTS
public:
#pragma region GETTERS_SETTERS
	/** Getters */
	/** Get the Material Instance for the mesh */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	FORCEINLINE UMaterialInstanceDynamic* GetMobiusMaterialInstanceDynamic() const { return MobiusMaterialInstanceDynamic; }

	/**
	 * Get the DatasmithMaterialsMap
	 */
	FORCEINLINE TMap<TWeakObjectPtr<UStaticMeshComponent>, FDatasmithMaterials> GetDatasmithMaterialsMap() const { return DatasmithMaterialsMap; }

	/** Setters */
	/** Set the Material Instance for the mesh */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	FORCEINLINE void SetMobiusMaterialInstanceDynamic(UMaterialInstanceDynamic* InMobiusMaterialInstanceDynamic) { MobiusMaterialInstanceDynamic = InMobiusMaterialInstanceDynamic; }
#pragma endregion GETTERS_SETTERS

};
