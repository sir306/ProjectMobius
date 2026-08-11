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

/**
 * Building material styles for PROCEDURAL geometry (IFC, fbx, obj, wkt), mirroring the four modes the
 * Datasmith path already offers and mapping onto the four MI_RuntimeMeshBuilder* instances that already
 * exist in the project:
 *
 *   OriginalColours            -> MI_RuntimeMeshBuilderOpaque
 *   OriginalColoursCutOut      -> MI_RuntimeMeshBuilderMasked                (masked blend IS the cut-out)
 *   TransparentWhite           -> MI_RuntimeMeshBuilderTranslucent
 *   OriginalColoursTransparent -> MI_RuntimeMeshBuilderTranslucentClearcoat
 *
 * "Original colours" means the colours authored in the SOURCE FILE -- IfcSurfaceStyle for IFC,
 * aiMaterial for fbx/obj. A section whose source said nothing about colour falls back to the master's
 * own plain-white chain (Use Modified Colour = 0), so each style degrades to its plain white
 * equivalent -- plain white / plain white cut out / plain white transparent / plain white clear coat --
 * rather than to black.
 *
 * Declared at global scope because UHT requires it: a UENUM inside a class body fails with
 * "Invalid use of keyword 'UENUM'. It may only appear in Global scopes".
 */
UENUM(BlueprintType)
enum class EMobiusBuildingMaterialStyle : uint8
{
	/** Opaque, source colours; plain white where a section has none. */
	OriginalColours              UMETA(DisplayName = "Original Colours"),

	/** Masked/cut-out, source colours; plain white cut out where a section has none. */
	OriginalColoursCutOut        UMETA(DisplayName = "Original Colours (Cut Out)"),

	/** Translucent, forced white regardless of what the source authored. */
	TransparentWhite             UMETA(DisplayName = "Transparent White"),

	/** Translucent clear coat, source colours; plain white clear coat where a section has none. */
	OriginalColoursTransparent   UMETA(DisplayName = "Original Colours (Transparent)"),
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
	 * Clear ONLY the procedural mesh geometry (sections + collision) without touching any
	 * imported Datasmith building. Used to tear down B-Risk room geometry when it is toggled
	 * off or replaced. Safe to call when no procedural geometry exists.
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Generation")
	void ClearMobiusProceduralMesh();

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

	/**
	 * Original RefractionIndex of every translucent-view MID, recorded when the MID is created and
	 * kept for as long as it lives.
	 *
	 * The Datasmith translucent master refracts at IOR 1.5. Across a whole floor slab seen
	 * edge-on that displaces the scene-colour sample far enough to smear unrelated geometry over
	 * the surface, which reads as a mirror and makes the translucent view unusable. The view
	 * therefore flattens the IOR to 1.0 for as long as it is active.
	 *
	 * Recorded at creation rather than on the first switch into the view: the switch order is
	 * decided in Blueprint, the view is entered and left repeatedly, and MaterialCache hands the
	 * same MID to many meshes — so a toggle-time snapshot can miss its chance to see the untouched
	 * value and then has nothing to restore. A constant cannot stand in for it either, since
	 * MaterialCache copies the value off each imported Datasmith material and it is per-model.
	 *
	 * Weak keys because one MID is shared across meshes and they die with the Datasmith scene.
	 */
	TMap<TWeakObjectPtr<UMaterialInstanceDynamic>, float> TranslucentViewRefractionSnapshot;

	/** True while the building is being shown translucent, so late meshes can match the live view. */
	bool bTranslucentViewActive = false;

	/** Remember a freshly created translucent MID's untouched refraction. Safe to call repeatedly. */
	void RecordOriginalRefraction(UMaterialInstanceDynamic* TranslucentMaterial);

	/**
	 * Whether the building is currently being shown translucent, answered from the materials on the
	 * meshes rather than from which entry point ran last — one mode change calls several of these
	 * from Blueprint and the order is not ours to assume. An opaque slot rendering its translucent
	 * MID is the only reliable tell.
	 */
	bool IsTranslucentViewActive() const;

	/** Flatten or restore every recorded MID's refraction to match the view that is actually up. */
	void ApplyRefractionForCurrentView();

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

	/**
	 * Placeholder material used in packaged builds when a Datasmith slot resolves to no
	 * material — the signature of Twinmotion-sourced content whose masters were excluded
	 * from cook under Epic's Twinmotion EULA (see MOBIUS_TWINMOTION_PACKAGED_DISABLED).
	 * Loads M_MobiusUnsupported (a vivid "render error" purple) on first use and caches it.
	 */
	UMaterialInterface* GetUnsupportedMaterial();

	/** Evaluate whether to flush all door spawns immediately or fall back to batched tick. Reports a warning popup when batched mode is chosen. */
	void DecideAndExecuteSpawnStrategy();

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

	/** FPS must be above this value for immediate flush spawning after Datasmith load. Below or within ±10, batched tick is used instead. */
	UPROPERTY(EditAnywhere, Category="Flow Counters", meta=(ClampMin="10", ClampMax="120"))
	float SpawnFPSThreshold = 30.0f;

	/** Free physical memory ratio below which batched spawning is preferred (0.15 = 15% free). */
	UPROPERTY(EditAnywhere, Category="Flow Counters", meta=(ClampMin="0.05", ClampMax="0.5"))
	float MemFreeThreshold = 0.15f;


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

	/**
	 * Set during a Datasmith load when one or more slots came back unresolved (Twinmotion
	 * content excluded from cook). Checked once when the pending-mesh queue drains, to raise a
	 * single EULA notice per load rather than one per mesh. Reset at the start of each load.
	 */
	bool bTwinmotionRefusedThisLoad = false;

	/** Cached placeholder material for unsupported (Twinmotion) slots — see GetUnsupportedMaterial(). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> UnsupportedMaterialCache = nullptr;

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

	/**
	 * Diagnostics from the most recent .ifc load: source schema (read from the file's own FILE_SCHEMA
	 * header, never from IFC++'s unreliable accessor), product/triangle counts, the render-filter
	 * summary line, and every IfcSpace room volume the allowlist kept out of the render mesh.
	 *
	 * Empty (SourceSchema.IsEmpty()) after loading any non-IFC format, which is also how the emit path
	 * tells whether IfcSectionInfo below is meaningful. RoomVolumes is the handoff point for the B-RISK
	 * room work — IFC carries the rooms natively, so they no longer have to be derived elsewhere.
	 */
	FMobiusIfcLoadStats LastIfcLoadStats;

	/**
	 * IFC provenance per emitted ProcMesh section, index-parallel to the sections actually created
	 * (i.e. to PendingMeshChunks as it was at emit time, AFTER SplitSubmeshByTriCap — one IFC product
	 * can become several sections, and each carries the same GUID/class).
	 *
	 * Empty for non-IFC loads. Kept because the entity identity is the reason to import IFC rather
	 * than a triangle soup, and recovering it after the fact means re-parsing the file.
	 */
	TArray<FMobiusIfcSectionInfo> IfcSectionInfo;


	/**
	 * Applies a source-authored material (IFC IfcSurfaceStyle, or aiMaterial for fbx/obj) to one
	 * emitted section, by creating a MID from the building material's parent and driving the same
	 * colour parameters the Datasmith path uses.
	 *
	 * Returns false — and leaves the caller to apply the shared material — when the section has no
	 * source material OR when the building material's parent does not expose those parameters. The
	 * parent comes from Blueprint, so whether it can express a colour is not knowable at compile time;
	 * probing and reporting beats calling SetVectorParameterValue into the void, which would render
	 * every section in the default colour and look like the importer had lost the material.
	 */
	bool ApplySourceMaterialToSection(int32 SectionIdx, const struct FMobiusMeshMaterial& SourceMaterial);

	/**
	 * Walks up past every UMaterialInstanceDynamic to the nearest ASSET material (UMaterialInstanceConstant
	 * or UMaterial), and returns null if there is none.
	 *
	 * A MID must never be the parent of another MID here, and that is not a style preference — it is the
	 * defect the owner spotted on 2026-08-12. WBP_SetBuildingMat builds its material with
	 * CreateDynamicMaterialInstance, which parents on the component's CURRENT material, i.e. on a section
	 * MID this class created. Parenting the next section MID on that one stacks a level per widget
	 * interaction, and once the chain closes back on itself UMaterialInstance::SetParentInternal refuses it
	 * ("It is already dependent on this material") and leaves Parent NULL — a MID with no parent, which
	 * renders as nothing recognisable. Resolving to an asset makes the operation idempotent: the same
	 * parent every time, no matter how many times Blueprint hands us a derived MID.
	 */
	static UMaterialInterface* ResolveNonDynamicParent(UMaterialInterface* InMaterial);

	/**
	 * One reused MID per section for the given parent. Rebuilt only when the parent changes, so a widget
	 * click restyles in place instead of orphaning a MID per section per click.
	 */
	UMaterialInstanceDynamic* GetOrCreateSectionMID(int32 SectionIdx, UMaterialInterface* Parent);

	/** Section colour MIDs and the parent they were built from; see GetOrCreateSectionMID. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SectionColourMIDs;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SectionColourMIDParent = nullptr;

	/** Probe state for the above; re-evaluated per load AND whenever the resolved parent changes. */
	bool bSourceColourParamProbeDone = false;
	bool bSourceColourParamsAvailable = false;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SourceColourProbedParent = nullptr;

	/** Sections that actually received a source-authored material, for the load summary line. */
	int32 SourceMaterialSectionsApplied = 0;

	/**
	 * Source material per EMITTED SECTION, index-parallel to the ProcMesh sections, for EVERY format —
	 * not just IFC (IfcSectionInfo covers IFC alone and carries entity provenance too).
	 *
	 * Kept because a style switch has to re-apply each section's own colour onto a different parent
	 * material, which means the colours must outlive the emit. Without this, switching to translucent
	 * and back would lose every imported colour.
	 */
	TArray<FMobiusMeshMaterial> SectionSourceMaterials;

	/** Style currently applied; None-equivalent is "whatever UpdateMeshMaterial supplied", the default. */
	EMobiusBuildingMaterialStyle CurrentBuildingMaterialStyle = EMobiusBuildingMaterialStyle::OriginalColours;

	/** True once SetBuildingMaterialStyle has run at least once, so the default look is not disturbed. */
	bool bBuildingMaterialStyleChosen = false;

	/** Resolves the MI_RuntimeMeshBuilder* instance backing a style. Null (and one log line) if missing. */
	UMaterialInterface* ResolveStyleParentMaterial(EMobiusBuildingMaterialStyle Style) const;

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

	/**
	 * Diagnostics from the most recent .ifc load — schema read from the file's own FILE_SCHEMA header,
	 * product/triangle counts, the render-filter summary, and the IfcSpace room volumes the allowlist
	 * kept out of the render mesh (the B-RISK room channel). Default-constructed, with an empty
	 * SourceSchema, after loading any non-IFC format.
	 *
	 * Not a UFUNCTION: FMobiusIfcLoadStats is a plain struct, not a USTRUCT, so it cannot cross the
	 * reflection boundary. C++ consumers (the automation tests, and whatever consumes the room volumes)
	 * only ever need the const ref.
	 */
	FORCEINLINE const FMobiusIfcLoadStats& GetLastIfcLoadStats() const { return LastIfcLoadStats; }

	/** IFC GUID + class per emitted ProcMesh section, index-parallel to the sections. Empty for non-IFC. */
	FORCEINLINE const TArray<FMobiusIfcSectionInfo>& GetIfcSectionInfo() const { return IfcSectionInfo; }

	/** True when the current load went through DatasmithRuntime (.udatasmith only — never .ifc). */
	FORCEINLINE bool IsDatasmithAsset() const { return bIsDatasmithAsset; }

	/**
	 * True from AsyncUpdateMesh until FinalizeMeshEmit — i.e. for the whole load INCLUDING the
	 * staggered per-frame section emit, not just the async parse.
	 *
	 * Anything polling for "the building is loaded" must wait on this and not merely on
	 * GetNumSections() > 0. The emit pump pushes SectionsEmittedPerTick sections per frame, so the
	 * first non-zero section count appears with one small section present and the component's bounds
	 * covering only that one product. An automation test written against "sections > 0" measured
	 * 1 section / 88 triangles / 135 x 4 x 50 cm bounds out of a 37-section, 3008-triangle,
	 * 940 x 640 x 405 cm building (2026-08-12) and read like a geometry bug.
	 */
	FORCEINLINE bool IsMeshBeingBuilt() const { return bMeshBeingBuilt; }

	/**
	 * Switch the whole building to one of the four material styles — the same four the Datasmith path
	 * offers: original colours, original colours cut out, transparent white, original colours
	 * transparent. Sections whose source carried no colour fall back to the plain white equivalent of
	 * each style rather than to black.
	 *
	 * Dispatches on bIsDatasmithAsset so ONE widget control works for every geometry source: a
	 * .udatasmith building routes to the existing SetDatasmith* implementations (behaviour unchanged by
	 * construction), while a procedural building (IFC, fbx, obj, wkt) re-parents each emitted ProcMesh
	 * section onto the style's MI_RuntimeMeshBuilder* instance and re-applies that section's own source
	 * colour from SectionSourceMaterials.
	 *
	 * Before this existed, every material control in WBP_SetBuildingMat was Datasmith-only — they all
	 * iterate DatasmithMaterialsMap, which a procedurally-built building never populates, so the panel's
	 * solid / translucent / cut-out / colour controls silently did nothing for an IFC or fbx building.
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetBuildingMaterialStyle(EMobiusBuildingMaterialStyle Style);

	/** The style currently applied. Until one is requested, sections keep the BP-supplied material. */
	UFUNCTION(BlueprintPure, Category = "MeshGenerator|Material")
	EMobiusBuildingMaterialStyle GetBuildingMaterialStyle() const { return CurrentBuildingMaterialStyle; }

	// ---------------------------------------------------------------------------------------------
	// One parameterless entry point per style, for the widget buttons.
	//
	// These exist for a concrete tooling reason, not as sugar: a Blueprint node calling the enum
	// overload needs its enum pin DEFAULT set, and in this project setting a pin default on a byte/enum
	// pin through the MCP bridge silently fails (auto-memory reference-mcp-pin-default-noop) — the node
	// would be created, report success, and carry the wrong style. A parameterless call has no pin to
	// set, so the graph cannot be wrong in that particular invisible way. They are also easier to read
	// in the widget graph than an enum literal.
	//
	// Each is a one-line forward to SetBuildingMaterialStyle, which is where all the logic lives.
	// ---------------------------------------------------------------------------------------------

	/** Opaque, source colours; plain white where a section has none. */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetBuildingOriginalColours() { SetBuildingMaterialStyle(EMobiusBuildingMaterialStyle::OriginalColours); }

	/** Masked cut-out, source colours; plain white cut out where a section has none. */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetBuildingOriginalColoursCutOut() { SetBuildingMaterialStyle(EMobiusBuildingMaterialStyle::OriginalColoursCutOut); }

	/** Translucent, forced white regardless of what the source authored. */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetBuildingTransparentWhite() { SetBuildingMaterialStyle(EMobiusBuildingMaterialStyle::TransparentWhite); }

	/** Translucent clear coat, source colours; plain white clear coat where a section has none. */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetBuildingOriginalColoursTransparent() { SetBuildingMaterialStyle(EMobiusBuildingMaterialStyle::OriginalColoursTransparent); }

	/** Setters */
	/** Set the Material Instance for the mesh */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	FORCEINLINE void SetMobiusMaterialInstanceDynamic(UMaterialInstanceDynamic* InMobiusMaterialInstanceDynamic) { MobiusMaterialInstanceDynamic = InMobiusMaterialInstanceDynamic; }
#pragma endregion GETTERS_SETTERS

};
