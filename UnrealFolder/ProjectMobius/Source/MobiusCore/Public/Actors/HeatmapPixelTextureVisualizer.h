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
#include "Containers/Ticker.h"         // FTSTicker for staggered tile emit
#include "HeatmapLOSBands.h"            // FHeatmapLOSBands is a by-value UPROPERTY below
#include "TrajectoryField.h"            // FTrajectoryField member + ETrajectoryMapMode UPROPERTY
#include "GameFramework/Actor.h"
#include "HeatmapPixelTextureVisualizer.generated.h"


class UProceduralMeshComponent;
struct FHeatmapTrajectorySegment;

/**
 * Per-section buffers for a tiled heatmap grid. Each tile owns its own vertex table and emits
 * its own ProcMesh section so the game-thread cost is split across frames and sections can be
 * filtered independently. Boundary verts on adjacent tiles are duplicated; the grid is small
 * enough that duplication cost is negligible compared to the hitch it replaces.
 */
struct FHeatmapTile
{
	TArray<FVector>   Verts;
	TArray<int32>     Tris;
	TArray<FVector2D> UVs;
};

/**
 * Enum to determine the type of heatmap to render
 */
UENUM()
enum EHeatmapType : uint8
{
	Eht_StandardHeatmap	= 1	UMETA(DisplayName = "Standard Heatmap"),
	Eht_VoronoiMap		= 2	UMETA(DisplayName = "Voronoi Map"),

	Eht_MAX					UMETA(DisplayName = "DefaultMAX")
};

UCLASS()
class MOBIUSCORE_API AHeatmapPixelTextureVisualizer : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AHeatmapPixelTextureVisualizer();
	
	// OnConstruction is called whenever the actor is placed or the values are changed in the editor
	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void PostInitializeComponents() override;
	/**
	 * The ONE place that decides which material instance the banded surface should be wearing.
	 *
	 * Exists because the rule was written out twice — here and in EmitNextTileSection — and the copies
	 * drifted: the tile-emit copy lacked the bTrajectoryHeatmap term, so tiles streaming in during
	 * trajectory mode were bound to the density instance and banded against Fruin edges. Any new site
	 * that needs a material must call this rather than re-derive it.
	 */
	UMaterialInstanceDynamic* SelectSurfaceMaterial() const;

	void AssignMaterialInstanceToMesh() const;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

#pragma region PUBLIC_METHODS
	/**
	 * Initialize the heatmap if spawned in the world and pass values to the heatmap to setup with
	 *
	 * @param[int32] InHeatmapType The type of heatmap to render - 0 = Voronoi Map, 1 = Standard Heatmap, 2 = 3D Heatmap, if a value is out of range it will default to 0
	 * @param[bool] bIsLiveTrackingNeeded A bool to determine if the heatmap is cumulative or live tracking - default is true but Voronoi need to be false
	 * @param[FVector2D] MeshSize The size of the mesh in the X and Y direction
	 * @param[float] NewHeightDisplacement The height displacement of the heatmap, default is 0.0
	 * @param[bool] bIs3DHeatmap A bool to determine if the heatmap is 3D or 2D, default is false
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void InitializeHeatmap(int32 InHeatmapType, bool bIsLiveTrackingNeeded, const FVector2D& MeshSize, float NewHeightDisplacement = 0.0f, bool bIs3DHeatmap = false);

	/**
	 * Vertex-grid dimensions for a heatmap covering MeshSizeCm, one entry per axis.
	 *
	 * Takes NO 3D flag, on purpose. The 3D toggle is a realtime switch that must never regenerate
	 * geometry, so the mesh is always built dense enough to carry displacement (owner ruling A0-64) and
	 * the vertex count must not depend on it. Expressing that as a function WITHOUT the parameter makes
	 * the invariance structural rather than something a reader has to notice — but a branch can still be
	 * reintroduced at the CALL SITE, which is why the behavioural gate
	 * `Mobius.InGame.TrajectoryHeatmap.Mesh.VertexCountIndependentOf3DFlag` also exists.
	 *
	 * Rounding is TRUNCATION, matching the shipping path this replaced. Note the dead
	 * `CalculateNumberOfTriangles` uses `CeilToInt32` instead, so the two disagree on every extent that
	 * is not a multiple of 25 — do not treat that function as documentation for this one.
	 *
	 * The name says "triangles" nowhere: the returned value is the VERTEX count per axis, and the quad
	 * count is (X-1)*(Y-1). The old `NumTriangles` local meant vertices too, which misread easily.
	 *
	 * @param MeshSizeCm Floor extent in centimetres.
	 * @return Vertices per axis. 25 cm per cell, so a 50 cm floor still yields 2 and never collapses.
	 */
	static FIntPoint ComputeHeatmapVertexGrid(const FVector2D& MeshSizeCm);

	/**
	 * THE render-side world -> texture-UV mapping, in one place. `BuildTileBuffers` calls this for every
	 * vertex it emits, so this function IS the shipping behaviour rather than a description of it.
	 *
	 * Extracted 2026-08-10 (D-C) for one reason: the trajectory field has to write into the same texel the
	 * render will sample, and a gate that asserts they agree must not be allowed to re-implement this
	 * formula. A0-79 is the precedent — `Offset.MeshSpanMatchesExtent` asserted a function nothing called
	 * for two days while the shipping path went unguarded, and the only reason it could was that the test
	 * had its own copy of the maths.
	 *
	 * The mapping: the MAJOR axis fills 0..1, and the minor axis is scaled by the world-extent aspect
	 * ratio and centred — which is what letterboxes a non-square floor into a square texture.
	 *
	 * @param LocalOffsetCm Offset from the mesh's MINIMUM corner, i.e. vertex position before
	 *                      MeshOriginLocation is added.
	 * @param MeshSizeCm    Floor extent. A non-positive axis yields 0 on that axis rather than a NaN.
	 */
	static FVector2D HeatmapMeshUV(const FVector2D& LocalOffsetCm, const FVector2D& MeshSizeCm);

	/**
	 * The complete answer to "where does the trajectory grid sit inside the square texture" — the texel
	 * offset, the origin shift that phases the cell lattice onto the render's texel lattice, and the pad
	 * cells that keep the shifted grid covering the whole mesh. See PlanTrajectoryLatticePhase.
	 */
	struct FTrajectoryLatticePhase
	{
		/** Texel of grid cell (0,0) inside the square texture. */
		FIntPoint TexelOffset = FIntPoint::ZeroValue;

		/** Extra cells the shift needs, for FTrajectoryFieldConfig::ExtraGridCells. 0 or 1 per axis. */
		FIntPoint ExtraGridCells = FIntPoint::ZeroValue;

		/** Added to the mesh origin before Initialise. |shift| <= one cell; zero on the major axis. */
		FVector2D OriginShiftCm = FVector2D::ZeroVector;

		/** ResolveGrid's effective cm/texel, before padding. */
		float CmPerTexel = 0.0f;

		/** ResolveGrid's dims, BEFORE ExtraGridCells. */
		FIntPoint GridDims = FIntPoint::ZeroValue;

		/** max(GridDims) — the square texture side. Padding never changes it. */
		int32 SquareSide = 0;

		/** True when an axis hit the near-square fallback and kept the un-phased (sub-texel-wrong) offset. */
		bool bPhaseAbandoned = false;
	};

	/**
	 * Works out how the trajectory field must be positioned so that the texel it WRITES is the texel the
	 * render SAMPLES. Pure maths over the arguments — no actor state, no allocation, no world — which is
	 * what lets the alignment contract be gated from ProjectMobiusTests directly against the shipping
	 * function instead of against a re-derivation of it.
	 *
	 * `EnsureTrajectoryFieldSized` is its only production caller and applies the result verbatim. The
	 * three outputs are ONE answer and must be applied together: the offset alone, without the shift, is
	 * the D-C defect; the shift without the pad drops a strip of floor at one edge.
	 *
	 * Full derivation and the measured before/after are in the D-C comment block at the call site.
	 */
	static FTrajectoryLatticePhase PlanTrajectoryLatticePhase(const FVector2D& MeshSizeCm,
	                                                          const FTrajectoryFieldConfig& InConfig);


	/** Creates and assigns the materials to the instances if not already done */
	void CreateMaterialInstances();

	/**
	 * Create and setup, the render target and bind the heatmap material instance parameter to it
	 */
	void SetupDynamicTexture();

	/**
	 * 
	 * @param[FVector] AgentLocation Location of the agent is within the Z bounds of the add params for the heatmap
	 * @return[bool] true if the agent location is valid
	 */
	bool CheckHeatmapAndLocationValid(const FVector& AgentLocation) const;

	// TODO: This method is likely going to need to be async so that we can update the render target in real time and not block the main thread
	/**
	 * Using the AgentMaterialInstance, we can update the location of the agent on the render target
	 * Giving live updates to the heatmap
	 *
	 * @param AgentLocation - The location of the agent in world space
	 * @param bUpdateHeatmap - A bool to determine if the heatmap should be updated after the agent location is updated
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void UpdateHeatmap(const FVector& AgentLocation, bool bUpdateHeatmap = false) const;

	/**
	 * This is to pass an array of locations to the heatmap to update the heatmap with multiple agents
	 *
	 * @param AgentLocations An array of agent locations in world space
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void UpdateHeatmapWithMultipleAgents(const TArray<FVector>& AgentLocations);

	/**
	 * This is to pass an array of locations to the heatmap to update the heatmap with multiple agents.
	 * Unlike UpdateHeatmapWithMultipleAgents, this doesn't check if the agent locations Z values are within the bounds
	 * of the heatmap and is assumed this has already been done in the calling function
	 *
	 * @param AgentLocations An array of agent locations in world space
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void UpdateHeatmapWithMultipleAgents_NoCheck(const TArray<FVector>& AgentLocations);

	/**
	 * Deposits travelled agent-path segments into the canonical trajectory field and refreshes the
	 * display encode. No brush, no minimum-visible seed, no clamping into texture bounds — see
	 * FTrajectoryField for the deposition contract.
	 */
	void UpdateHeatmapWithTrajectorySegments(const TArray<FHeatmapTrajectorySegment>& Segments);

	/**
	 * Switches which canonical quantity the surface presents. Purely a display operation: the two
	 * canonical accumulators are both maintained unconditionally, so this re-encodes and never re-walks
	 * or discards a segment. Safe to call mid-playback.
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Trajectory")
	void SetTrajectoryMapMode(ETrajectoryMapMode NewMode);

	UFUNCTION(BlueprintPure, Category = "Heatmap|Trajectory")
	ETrajectoryMapMode GetTrajectoryMapMode() const { return TrajectoryMapMode; }

	/**
	 * As statical widgets require updated agent floor counts, we can quickly itterate
	 * over the data and update these counts without having to update the heatmap texture
	 *
	 * @param AgentLocations An array of agent locations in world space
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void UpdateHeatmapAgentCount(const TArray<FVector>& AgentLocations);

	/**TODO: look over this as it is late when you wrote it
	 * A way to translate the world space coordinates to UV coordinates
	 *
	 * @param EntityWorldLocation - The Agents location in world space
	 * @return FVector2D - The UV coordinates
	 * 
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	FVector2D ActorWorldToUV(const FVector& EntityWorldLocation) const;

	/**
	 * Updates the texture renders of the heatmap
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void UpdateHeatmapTextureRender() const;


	void ClearTexture();

	/**
	 * This method is used to update the mesh size
	 *
	 * @param[FVector2D&] NewMeshSize - The new size of the mesh
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void UpdateMeshSize(const FVector2D& NewMeshSize);

	/**
	 * Update Switch Heatmap material type
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void UpdateHeatmapType(bool bIsStandardHeatmap, bool bIsLiveTrackingNeeded = true);

	/** Enables the playback-history trajectory view. Enabling it clears prior heatmap pixels. */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void SetTrajectoryHeatmapEnabled(bool bEnabled);

	/**
	 * Update the bounds, size and location of the heatmap mesh
	 * 
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void UpdateHeatmapMeshBounds();

	/**
	 * Build Mesh plane using built in grid mesh generation
	 *
	 * @param[FVector2D] MeshSize The size of the mesh in the X and Y direction
	 * @param[bool] bIsStandardHeatmap - A bool to determine if a simple plane is needed vs a dense mesh
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void BuildGridMeshPlane(const FVector2D& MeshSize, bool bIsStandardHeatmap = true);

	/**
	 * Used to set the color vision deficiency settings of a heatmap - this is used for Nicks Master Research but will be
	 * used as an accessibility setting later on
	 * 
	 * @param[EColorVisionDeficiency] ColourDeficiency - The type of color vision deficiency to correct for
	 * @param[float] DeficiencyLevel - The level of deficiency - default is 10.0 and values should only be between 0.0 and 10.0
	 * @param[bool] bCorrectDeficiency - A bool to determine if the color vision deficiency should be corrected for
	 * @param[bool] bSimulateColourCorrectionWithDeficiency - A bool to determine if the colour correction should be simulated with the deficiency
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void UpdateHeatmapCVDSettings(EColorVisionDeficiency ColourDeficiency, float DeficiencyLevel = 10.0f, bool bCorrectDeficiency = true, bool bSimulateColourCorrectionWithDeficiency = true);

	/**
	 * Save Heatmap to PNG.
	 *
	 * In trajectory mode this exports a THREE-file set under Saved/Heatmap/, per the sidecar convention
	 * in MobiusPerf/analysis/METADATA_SCHEMA.md:
	 *   <base>.png        presentation field, CPU-colourised with the same band edges as the material
	 *   <base>.csv        canonical field: cell_x, cell_y, person_metres, person_seconds
	 *   <base>.meta.json  schema_version 1 sidecar, including the four-bucket audit totals
	 * The PNG is display-only; every number that can be checked lives in the CSV + sidecar pair.
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void SaveHeatmapToPNG() const;
	void SaveHeatmapToPNG(const FString& CurrentTimeString) const;

#if !UE_BUILD_SHIPPING
	/**
	 * Automation access to the raw trajectory accumulation buffer.
	 *
	 * The trajectory texture is private because nothing in the runtime should draw to it outside
	 * UpdateHeatmapWithTrajectorySegments. Calibration tests still need to read the accumulated
	 * counts back, so this exposes it read-only rather than widening the drawing surface.
	 *
	 * @return The accumulation texture, or nullptr before SetupDynamicTexture has run.
	 */
	const class UDynamicPixelRenderingTexture* GetTrajectoryAccumulationTextureForTesting() const { return TrajectoryAccumulationTexture; }

	/**
	 * Mutable variant, for diagnostics that must call the texture's non-const helpers - specifically
	 * SaveDynamicTextureToPNG, which needs to build a colourised copy. Still read-only in intent: the
	 * accumulation must only ever be drawn to by UpdateHeatmapWithTrajectorySegments.
	 */
	class UDynamicPixelRenderingTexture* GetTrajectoryAccumulationTextureMutableForTesting() const { return TrajectoryAccumulationTexture; }

	/**
	 * The DENSITY texture, read-only, so the sampler-filter gate can assert both surfaces rather than only
	 * the trajectory one. Added because the two textures are created at three separate call sites and a
	 * filter set on one but not the others renders blocky beside smooth, which nobody would think to check.
	 *
	 * @return The density texture, or nullptr before SetupDynamicTexture has run.
	 */
	const class UDynamicPixelRenderingTexture* GetDensityDynamicTextureForTesting() const { return DynamicTexture; }

	/**
	 * Maps a world location to the texel the trajectory pipeline would write, for test assertions.
	 * Returns (-1,-1) for a location outside the grid — it deliberately does NOT clamp, because clamping
	 * is the bug the field replaced and a clamped answer would hide an off-floor sample.
	 */
	FIntPoint WorldToTexelForTesting(const FVector& WorldLocation) const;

	/** Canonical field read-only, so a conservation test can assert against the accumulators directly. */
	const FTrajectoryField& GetTrajectoryFieldForTesting() const { return TrajectoryField; }

	/**
	 * The trajectory material instance, so a test can read back the band scalars ACTUALLY pushed to the
	 * GPU and compare them against the ones handed to the CPU colouriser. Those two silently diverging is
	 * the whole reason the exported PNG and the in-world render can disagree.
	 */
	const class UMaterialInstanceDynamic* GetTrajectoryMaterialForTesting() const { return TrajectoryMaterialInstance; }

	/** Emitted mesh sections. One per kept tile when batching is on, else 1. */
	int32 CountEmittedMeshSectionsForTesting() const;

	/**
	 * Vertices summed across every emitted section — the number the 3D-toggle invariant is stated in, and
	 * the one the geometry-cost measurement reports.
	 *
	 * ⚠️ This is NOT the size of the logical vertex grid when batching is on. Tiles own their own buffers
	 * and `BuildTileBuffers` maps global grid indices to LOCAL ones per tile, so a vertex on a tile border
	 * is materialised once per tile that touches it. Expect a total ABOVE ComputeHeatmapVertexGrid's
	 * product, by more as GridTileSize shrinks. Set bEnableMultiSectionBatching = false for a total that
	 * equals the grid exactly.
	 */
	int32 CountEmittedMeshVerticesForTesting() const;

	/** Triangles summed across every emitted section — KEPT triangles, i.e. after quad culling. */
	int32 CountEmittedMeshTrianglesForTesting() const;

	/**
	 * Quads that `FindAllQuads` currently derives from the world's building geometry, i.e. the culling
	 * mask the mesh was built against.
	 *
	 * **Zero means no culling, not no mesh.** `BuildTileBuffers` opens with
	 * `bKeep = Quads.Num() == 0`, so an empty mask KEEPS EVERY CELL. That is the state in a world with a
	 * RuntimeMeshBuilder present but no floor plan loaded, and it is what makes the emitted vertex count
	 * exactly predictable in a test. With a plan loaded this is non-zero and kept geometry is a subset.
	 */
	int32 CountCullingQuadsForTesting() const;
#endif

#pragma endregion PUBLIC_METHODS

#pragma region PUBLIC_PROPERTIES_AND_COMPONENTS
	
#pragma region RUNTIME_MESH_GENERATION
	/** The Procedural Mesh Component used for generating meshes at runtime or on construction */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Component")
	TObjectPtr<UProceduralMeshComponent> RuntimeHeatmapMeshComponent;

	/** Stores the vertex positions of the mesh as there will only be 4 per mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Component")
	TArray<FVector> MeshVertices = TArray<FVector>();

	/** Stores UV array position */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Component")
	TArray<FVector2D> MeshUVs = TArray<FVector2D>();

	/** Stores Triangle index order */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Component")
	TArray<int32> MeshTriangles = TArray<int32>();
	
#pragma endregion RUNTIME_MESH_GENERATION
	
	/** The width of the Render Target Texture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	int32 TextureWidth;

	/** The height of the Render Target Texture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	int32 TextureHeight;

	/** This float value sets the height displacement on the heatmap allowing a 2D plane to become 3D */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	float HeightDisplacement;

	/** For simplicity, we expose a name variable to blueprints that can set the name of each actor this should resolve Texture issues*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	FString ActorName;

	/** This bool is to set whether it is an accumulative heatmap or live tracking heatmap
	 * - the accumulative heatmap is useful for seeing the where agents have walked
	 * - whereas live tracking is better suited for realtime viewing*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	bool bLiveTrackingHeatmap;

	/** Draw connected travelled path segments instead of position-density circles. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Heatmap|Trajectory")
	bool bTrajectoryHeatmap = false;

	/** Live/cumulative setting to restore after leaving trajectory mode. */
	UPROPERTY(Transient)
	bool bLiveTrackingBeforeTrajectory = false;

	/** Max Add height value, this is the maximum height from the heatmap where an entity can be added to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	float MaxAddHeight;

	/** Value to store the heatmap mesh plane size */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	FVector2D HeatmapMeshSize2D;

	/** Store the UV scale of the texture size compared to the mesh */
	UPROPERTY(EditAnywhere, Category = "Heatmap|MaterialsAndTextures")
	FVector2D UVScale;

	/** The location of the mesh origin in world space */
	UPROPERTY(EditAnywhere, Category = "Heatmap|MaterialsAndTextures")
	FVector MeshOriginLocation;

	/*
	 * The Initial Color Value used for the heatmap - Values per channel are 0.0 to 1.0
	 * Red Channel - Density Value per person per square meter default is 0.0
	 * Green/Blue Channel - Not used
	 * Alpha Channel - Controls the opacity of the heatmap at that location default is 1.0
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	FLinearColor InitialColorValue = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	 
	 /*
	 * The Color Value used to plot an agent on the heatmap - Values per channel are 0.0 to 1.0
	 * Red Channel - Density Value per person per square meter default is 0.1478 (6.766 people per square meter)
	 * Green/Blue Channel - Not used
	 * Alpha Channel - Controls the opacity of the heatmap at that location default is 1.0
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	FLinearColor AgentColorValue = FLinearColor(0.1478f, 0.0f, 0.0f, 1.0f);

	/*
	 * The Circle Radius Size used to plot an agent on the heatmap
	 * TODO: Currently using integer values, this should be changed to a float value for more precise locations and scale to texture and mesh size
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	int32 CircleRadius = 110; // 110 = 1.1m for our scaled data - TODO: SORT THIS OUT FOR BETTER SCALING

	/**
	 * Which canonical quantity the trajectory surface presents. Display-only: both accumulators are always
	 * maintained, so switching costs one presentation rebuild and loses nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|Trajectory")
	ETrajectoryMapMode TrajectoryMapMode = ETrajectoryMapMode::RouteUsage;

	/**
	 * D2 — world centimetres per canonical grid cell. THIS, not a texture size, is what makes a route the
	 * same real-world width on every floor: the grid dimensions vary with the building, the cell does not.
	 * The old path pinned the render target to 1024x1024 and let cm/texel float with floor size, which is
	 * why one walk measured 5.9 cm wide on a 20 m floor and 73 cm on a 250 m one.
	 *
	 * May be RAISED by the field to honour TrajectoryMaxGridDim, then LOWERED a hair so the major axis
	 * divides its extent evenly (D-A, the fix for the stroke landing beside the agent). So this property is
	 * the REQUEST, not the outcome — read the outcome back from the field's GetEffectiveCmPerTexel(),
	 * which is also what the export sidecar records.
	 *
	 * 15 cm since 2026-08-10 (was 20 for part of that day), and now PURE SAMPLING RESOLUTION: DECOUPLED from
	 * TrajectoryDisplayPathWidthCm. The Route Usage band edges derive from the stroke WIDTH
	 * (RefreshTrajectoryCrossingBands), so refining the cell buys a smoother silhouette without changing
	 * what a single colour means. Before that decoupling the two had to be EQUAL — equal values put the
	 * kernel radius at exactly 0.5 texels, collapsing the splat to the identity — and refining the cell
	 * alone would have divided each crossing across a mass-conserving kernel and sent the map back to cyan.
	 *
	 * WHY 15, AND WHY IT WAS BRIEFLY 20. 15 was rejected earlier the same day because BuildKernel's half
	 * extent is ceil(R - 0.5), which steps the tap count at R = 0.5, 1.5, 2.5 ...; at width 45, 15 cm puts
	 * R on exactly 1.5, the knife edge between a 9- and a 25-tap kernel, and D-A's snap pushes it over. 20
	 * cm sat safely at R = 1.125. **D-D voided that objection**: the deposit now splats the PHASE table,
	 * whose footprint is ceil(R + 0.5), and that is 25 taps at 15 cm and 25 taps at 20 cm alike. There is
	 * no boundary left to fall off, and 15 costs only 1.78x the cells of 20 — not more taps.
	 *
	 * WHAT 15 BUYS, and it is the whole reason: the drawn stroke is finally the right WIDTH. The display
	 * classifies each texel into one band, so a stroke can only be a whole number of cells wide. 45 / 15
	 * is exactly 3, so with the D-E route threshold the stroke renders exactly 45 cm at EVERY sub-cell
	 * phase. 45 / 20 is 2.25 — no threshold exists that renders 45, and the width swung between 40 and 60
	 * as the path slid across the lattice. Swept over all 9 phases: 15 cm admits a threshold window of
	 * 0.278-0.382 crossings; 20 cm and 11.25 cm admit none.
	 *
	 * Cost: 15 cm holds ~9x the cells of 45 cm, at 25 taps against 1 — per deposited cell, per segment,
	 * per frame, at ~17k agents. 🚩 NOT measured on this hardware. Measure before a demo, and note
	 * TrajectoryMaxGridDim starts binding on large plans well before 5 cm.
	 *
	 * Residual MISALIGNMENT no longer scales with this value either: D-D placed the deposit sub-cell, so
	 * the centroid error is half a phase BIN (~0.8 cm here), not half a cell. The systematic 18-68 cm bias
	 * toward the floor's minimum corner went with D-A/D-B/D-C. So this property now trades frame cost for
	 * exactly two things — stroke-width fidelity (see above) and silhouette smoothness.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|Trajectory", meta = (ClampMin = "0.1"))
	float TrajectoryWorldCmPerTexel = 15.0f;

	/**
	 * Presentation stroke WIDTH in world centimetres (replaces the old TrajectoryCircleRadius, which was a
	 * radius and doubled as a deposition brush). It feeds the splat kernel and — since 2026-08-10 — the
	 * Route Usage band edges. Canonical person-metres and person-seconds are unchanged by it, and
	 * Sum(presentation) == Sum(canonical) is the invariant that proves so. This is a path width, not a
	 * body footprint.
	 *
	 * 45 cm (owner ruling 2026-08-10). It is now the PHYSICAL BASIS of a crossing count: an edge means
	 * "N + 0.5 person-passes through a 45 cm-wide corridor centred here", and RefreshTrajectoryCrossingBands
	 * derives the edges from THIS rather than from the cell. Widening it alone no longer collapses the
	 * bands toward cyan — the edges widen with it, so the change restates what is being counted instead of
	 * silently rescaling it. That is what decoupled it from TrajectoryWorldCmPerTexel; see that property.
	 *
	 * WHY 45 cm rather than a number picked by eye. It is the major axis of Fruin's body ellipse
	 * (45.7 x 33 cm), which is already the anthropometry behind the DENSITY surface's LOS bands — so the
	 * two surfaces now describe a person at the same scale. It also fixes a semantic problem that 10 cm
	 * had: at 10 cm the count measures FOOT PLACEMENT, so two people walking abreast down the same corridor
	 * never share a cell and both read as separate single crossings. At body width they do share, and
	 * counting them together is the physically true statement that they occupied the same floor.
	 *
	 * Measured on a live capture at the old 10 cm (3,775 touched texels): p50 was 1.02 crossings and p75
	 * exactly 2.00 — i.e. the median cell was walked by one person and almost nothing accumulated, which is
	 * what "too fine" looks like in numbers.
	 *
	 * History: 20 cm originally, 10 cm at A0-47 (2026-08-05) when the goal was a footfall trace, 45 cm
	 * locked to the cell 2026-08-10, then decoupled from it later the same day.
	 *
	 * Deliberately NOT changed on FTrajectoryFieldConfig::DisplayPathWidthCm, which stays at 20: the oracle
	 * derivations and the calibration tests are written against a radius of exactly 1.0 texel and must not
	 * be re-tuned to match a display preference. Those tests pass their own explicit values.
	 *
	 * EditAnywhere + BlueprintReadWrite is the hook for the eventual sizing UI — a widget can drive this
	 * directly with no C++ change.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|Trajectory", meta = (ClampMin = "0.1"))
	float TrajectoryDisplayPathWidthCm = 45.0f;

	/**
	 * How far the material softens each band BOUNDARY, in screen pixels. 0 = the hard comparison chain.
	 *
	 * 🛑 **0 BY OWNER RULING, 2026-08-10 — it was observed to INTRODUCE ARTEFACTS on screen and is off.**
	 * It shipped at 1.0 on the argument recorded below, which was reasoned from the shader's behaviour and
	 * never checked against the rendered surface. Do not raise it back without a look in the app: the
	 * argument for it is theory, the ruling against it is observation, and observation wins. Everything
	 * from here down is the ORIGINAL rationale, kept because it explains what the knob does and why the
	 * mechanism cannot blur a cell interior — not because it justifies turning it on.
	 *
	 * THE REVERT SWITCH. The shader computes the transition width as fwidth(RVal) * 0.5 * this, so at 0 the
	 * smoothstep degenerates to a step and the output is the same hard-banded image as before the change —
	 * no shader edit, no rebuild, just set this to 0 (here, on the material instance, or from Blueprint).
	 * That is deliberate: edge softening is a taste call and it needed to be reversible without a code
	 * round trip.
	 *
	 * WHAT IT DOES AND DOES NOT FIX. It antialiases the one-pixel jaggies along a cell edge. It does NOT
	 * remove the STAIRCASE a diagonal route makes across a square grid — that shape is the grid itself, not
	 * a sampling artefact, and the only things that soften it are smaller cells (finer steps) or blending
	 * values between cells, which is exactly what TF_Bilinear did and what made the counts unreadable.
	 * Expect this to take the harshness off the edges, not to turn the surface into a smooth contour.
	 *
	 * Under TF_Nearest the sampled value is piecewise constant, so fwidth is ~0 inside a texel and spikes
	 * only at texel boundaries — which is precisely where the jaggies are and the only place any softening
	 * happens. That is why this is safe: it cannot blur a value across the interior of a cell.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|Trajectory", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float TrajectoryBandEdgeSoftness = 0.0f;

	/**
	 * D2b — hard ceiling on either grid axis. Exceeding it coarsens cm/texel rather than stretching the
	 * grid, so cell area stays uniform within an export. 2048 keeps the square render target at 16 MiB.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|Trajectory", meta = (ClampMin = "16", ClampMax = "8192"))
	int32 TrajectoryMaxGridDim = 2048;

	// TODO(perf, owner-flagged 2026-08-10): reduce the trajectory field's memory footprint by condensing
	// overlapping data points, rather than by coarsening the grid.
	//
	// Context, so this is not re-litigated. 10 cm cells were CHOSEN over 25/50 cm on 2026-08-10: the width
	// matches an agent's outer foot-to-foot distance, and precision was ruled more valuable than memory.
	// The cost is real - two floats per cell across the whole floor, and at 10 cm a 100 m x 100 m floor is
	// 1000x1000 cells, so ~8 MB per floor per accumulator pair, before the square render target.
	//
	// The suspected win is that the field is extremely SPARSE and highly repetitive: crowds funnel down the
	// same routes, so most cells are zero and the occupied ones cluster. Candidates worth measuring before
	// picking one - a sparse/tiled representation that only allocates touched blocks; quantising the stored
	// accumulators (float32 -> float16 or a fixed-point count) now that the display quantises to a byte
	// anyway; or sharing structure between floors that overlap.
	//
	// Do NOT "solve" this by raising TrajectoryWorldCmPerTexel. That trades away the precision the owner
	// explicitly bought, and it silently changes what every band means (the edges are derived from the
	// effective cell size - see RefreshTrajectoryCrossingBands).

	/**
	 * Dead under the field pipeline — kept only because they are BlueprintReadWrite and may be referenced
	 * by existing Blueprints. Nothing in C++ reads them: sample spacing is no longer a thing (the DDA
	 * visits every crossed cell exactly once), per-sample weight is replaced by physical person-metres,
	 * and the minimum-visible seed was removed outright because it is what made band A meaningless.
	 * Delete these together with their Blueprint references in a follow-up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|Trajectory|Deprecated", meta = (ClampMin = "1"))
	float TrajectorySampleSpacing = 20.0f;

	/** Dead — see TrajectorySampleSpacing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|Trajectory|Deprecated", meta = (ClampMin = "0.0001"))
	float TrajectorySampleWeight = 0.05f;

	/** Dead — see TrajectorySampleSpacing. The seed is gone; an untouched cell now encodes to byte 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|Trajectory|Deprecated", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TrajectoryMinimumVisibleValue = 0.10f;

	// A TrajectoryLineBrushRadius fixed in texels lived here, then a TrajectoryCircleRadius in world
	// centimetres. Both were deposition brushes, and a brush cannot be a measurement: a wider brush
	// multiplied the mass a crossing deposited. TrajectoryWorldCmPerTexel now fixes the cell size, so a
	// texel means the same on every floor, and TrajectoryDisplayPathWidthCm only widens the presentation
	// splat, whose weights sum to 1 and so conserve the canonical total exactly.

	/** Legacy trajectory blur setting. Trajectory rendering currently uses no blur. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|Trajectory", meta = (ClampMin = "3", ClampMax = "15"))
	int32 TrajectoryBlurKernelSize = 5;

	/**
	 * ROUTE-INTENSITY BAND EDGES (D9). Not Fruin Level of Service — LOS is a density concept and stays
	 * with the density surface. The type and the material parameter names still say "LOS" because
	 * renaming FHeatmapLOSBands' fields and the LOS_A_Band..LOS_E_Band scalars requires editing
	 * M_HeatmapRT_Trajectory, which is out of scope this week. Read every "LOS" on this surface as
	 * "route-intensity band".
	 *
	 * Units: normalised red-channel (stored byte / 255), which is what the struct's [0,1] clamp permits
	 * and what both consumers compare against — the material samples red, and the PNG colouriser divides
	 * the stored byte by 255. Pushing the identical struct to both is what keeps the export and the
	 * in-world render in step; see ApplyTrajectoryLOSBands.
	 *
	 * PROVISIONAL under D9, but no longer meaningless: REFIT 2026-08-04 against the first real canonical
	 * export, and the encode now uses a FIXED reference density instead of per-capture auto-exposure, so a
	 * normalised edge maps to a stated person/m figure rather than to "some fraction of this frame's
	 * brightest cell". See FHeatmapLOSBands::Trajectory() for the numbers and their provenance.
	 *
	 * ONE SET PER MODE. Route Usage and Route Exposure are different quantities with different reference
	 * densities (100 person/m vs 200 person*s/m^2), so a single normalised set cannot serve both — reusing
	 * Usage's edges for Exposure mis-bands it by the ratio of the references. ApplyTrajectoryLOSBands picks
	 * whichever matches the active mode.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|Trajectory")
	FHeatmapLOSBands TrajectoryLOSBands = FHeatmapLOSBands::Trajectory();

	/** Route Exposure's own edges. See TrajectoryLOSBands for why the two cannot be one set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|Trajectory")
	FHeatmapLOSBands TrajectoryExposureLOSBands = FHeatmapLOSBands::TrajectoryExposure();

	/**
	 * Is this a Standard Heatmap or a Voronoi Map:
	 * 1 = Standard Heatmap
	 * 0 = Voronoi Map
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	uint8 HeatmapType = 1;

	/** For Statistic Widgets we need a to store the number of agents */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	int32 NumberOfAgentsOnHeatmap = 0;
	
	/** Floor ID of the heatmap */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	int32 FloorID = 0;

	/** Cells per tile edge. Each tile becomes one ProcMesh section. Smaller = more sections, smaller per-section cost, more boundary-vert duplication. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures", meta = (ClampMin = "4"))
	int32 GridTileSize = 32;

	/** Emit tiled grid as multiple sections (true) or legacy single section 0 (false). Rollback knob. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	bool bEnableMultiSectionBatching = true;

	/**
	 * Tile sections pushed per tick during staggered emit. 1 = smoothest (finishes over N frames);
	 * higher = faster finish at the cost of bigger per-frame FScene_AddPrimitive spike.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures", meta = (ClampMin = "1"))
	int32 SectionsEmittedPerTick = 1;

	/** Cached tile buffers built off the GT and drained into ProcMesh sections on the GT. */
	TArray<FHeatmapTile> Tiles;

	/**
	 * Completed tile-emit passes, bumped once in FinalizeTileEmit. Monotonic for the actor's lifetime.
	 *
	 * A test cannot detect "the mesh is finished" from the other state: before the thread-pool build
	 * returns, `Tiles` is empty and `TileEmitTickerHandle` is invalid — which is indistinguishable from
	 * drained. Polling for that pair passes instantly, before generation has even started, and would make
	 * any mesh assertion vacuous. Watching this counter increment is unambiguous.
	 *
	 * Not a tick-path cost: written once per generation, not per tile and not per frame.
	 */
	int32 CompletedTileEmitCount = 0;

#pragma endregion PUBLIC_PROPERTIES_AND_COMPONENTS

private:
#pragma region PRIVATE_METHODS
	/**
	 * Push TrajectoryLOSBands to the trajectory material instance and to the accumulation texture's
	 * colouriser. Both consume the same edges in the same units, so the saved PNG and the in-world render
	 * band identically by construction rather than by two hand-matched conversions. Called whenever the
	 * bands or the trajectory MID change.
	 *
	 * const: it writes only through the MID and the texture object, never to this actor.
	 */
	void ApplyTrajectoryLOSBands() const;

	/**
	 * Recomputes TrajectoryLOSBands from the field's EFFECTIVE cell size and pushes it to both consumers.
	 *
	 * Separate from ApplyTrajectoryLOSBands, which only pushes what is already stored. Deriving inside
	 * that function instead would be wrong in a subtle way: it is called from six sites, several of them
	 * before the field is sized, and it is const — so it would silently turn the TrajectoryLOSBands
	 * UPROPERTY into a value nothing can ever set. Keeping the derivation here means there is exactly one
	 * writer, and it runs exactly where the cell size becomes known.
	 */
	void RefreshTrajectoryCrossingBands();

	/**
	 * Sizes/allocates the canonical field from the mesh's world extent and origin, and resizes the
	 * accumulation texture to match. Idempotent: re-initialising only happens when the extent, origin or
	 * sizing policy actually changed, because FTrajectoryField::Initialise resets every accumulator and a
	 * gratuitous call would silently erase accumulated mass.
	 */
	void EnsureTrajectoryFieldSized();

	/**
	 * Encodes the field's presentation copy into the accumulation texture and pushes it to the GPU.
	 * Only cells whose byte changed since the last refresh are written, so a flush costs a W*H byte
	 * compare plus the actual changes rather than a full texture rewrite. Auto-exposure means a rising
	 * maximum can LOWER an unrelated cell's byte, which is why the comparison is against the previous
	 * encode and not against "is this cell non-zero".
	 */
	void RefreshTrajectoryDisplay() const;

	/**
	 * Grid cell containing a world location, or false when it falls outside the grid. Diagnostic use
	 * only: it takes plain floor() and so differs from the field's lower-index-owns rule for a point
	 * lying exactly on a grid line. Never clamps.
	 */
	bool TrajectoryWorldToCell(const FVector& WorldLocation, FIntPoint& OutCell) const;

	/**
	 * Writes the three-file export set for one basename (relative to Saved/): presentation PNG, canonical
	 * CSV, metadata sidecar. The PNG comes from the same encode that is on screen — exporting the raw
	 * canonical field instead is what made the saved image a hairline while the screen showed a route.
	 */
	void ExportTrajectorySurface(const FString& RelativeBaseName) const;

	/** Canonical CSV + .meta.json sidecar for one export basename (relative to Saved/). */
	void WriteTrajectoryCanonicalExport(const FString& RelativeBaseName) const;

	/**
	 * Method to generate a square cell size that 2 triangles will be within
	 *
	 * @param[FIntPoint&] NumberOfTriangles The number of triangles needed for the mesh in the X and Y direction
	 * @param[FVector2D&] MeshSize The size of the mesh in the X and Y direction
	 * @return[FVector2D] The size of the square cell
	 */
	// PUBLIC deliberately. Pure static maths over the arguments with no actor state, which is what makes
	// the span invariant assertable from ProjectMobiusTests without spawning a world:
	//     span = (ComputeHeatmapVertexGrid(..) - 1) * GenerateSquareCellSize(..)
	// That span MUST equal the heatmap's world extent, or the texture is stretched across the wrong
	// distance and the whole image shifts. It was wrong by exactly one cell until 2026-08-05 (A0-60), and
	// no test could see it because the suite only ever measured sums, which are position-blind.
	//
	// This sentence named `CalculateNumberOfTriangles` until 2026-08-07 (A0-79). That was wrong in the way
	// that matters: it has no callers anywhere, so the invariant as written paired a live function with a
	// dead one — and the gate that asserted it, `Offset.MeshSpanMatchesExtent`, guarded nothing shipping.
	// The live grid comes from `ComputeHeatmapVertexGrid` (declared above, near InitializeHeatmap).
public:
	static FVector2d GenerateSquareCellSize(const FIntPoint& NumberOfTriangles, const FVector2D& MeshSize);

	/**
	 * DEAD as of 2026-08-07 (A0-79) — nothing calls this. Verified: no C++ caller, and no name-literal hit
	 * across all 1751 project .uasset/.umap files, so no Blueprint calls it either.
	 *
	 * Kept, not deleted, because removing public API is the owner's call — but do not treat it as a
	 * reference for the real grid. It CEILS `MeshSize/25` where the shipping `ComputeHeatmapVertexGrid`
	 * TRUNCATES, so the two return different vertex counts for every extent that is not a multiple of 25,
	 * and `TextureSize` is ignored outright. It sat here long enough to capture a gate:
	 * `Offset.MeshSpanMatchesExtent` asserted against it for two days while the shipping path went
	 * unguarded. Listed in _CurrentHandoff\DEAD_CODE_CLEANUP.md.
	 *
	 * @param[FVector2D&] MeshSize - The size of the mesh in the X and Y direction
	 * @param[FIntPoint&] TextureSize - Unused.
	 * @return[FIntPoint] Vertices per axis, ceiled.
	 */
	static FIntPoint CalculateNumberOfTriangles(const FVector2D& MeshSize, const FIntPoint& TextureSize);

private:
	/**
	 * Build a single tile's verts / tris / UVs for the cell range [TileX0, TileX1) x [TileY0, TileY1).
	 * Only verts referenced by kept quads are added to the tile; empty tiles return with Tris.Num()==0.
	 * Quad-intersect filter matches the legacy GenerateMeshTrianglesInQuadMapping behaviour.
	 */
	void BuildTileBuffers(int32 TileX0, int32 TileY0, int32 TileX1, int32 TileY1,
	                      const FIntPoint& NumTriangles, const FVector2D& CellSize,
	                      const TArray<FBox3d>& Quads, FHeatmapTile& Out) const;
       /**
        * Method to generate the mesh vertices, UVs and triangles for the heatmap mesh.
        * The method performs sanity checks on the input data before spawning any
        * threaded tasks. If the inputs are invalid the function will log an error
        * and exit early.
        *
        * @param[FVector2D&] MeshSize   The size of the mesh in the X and Y direction
        * @param[FIntPoint&] TextureSize The size of the texture in the X and Y direction
        * @param[bool]       bIs3DHeatmap A bool to determine if the heatmap is 3D or 2D
        */
       void GenerateMeshVerticesUVsAndTriangles(const FVector2D& MeshSize, const FIntPoint& TextureSize, bool bIs3DHeatmap = false);

	/**
	 * Helper method to find all the quads that will be valid for mesh building the heatmap
	 */
	TArray<FBox3d> FindAllQuads(class ARuntimeMeshBuilder* MeshBuilder = nullptr) const;

	/** Ticker pump that pushes up to SectionsEmittedPerTick tile sections per frame. Returns false when drained. */
	bool EmitNextTileSection(float DeltaTime);

	/** Runs once after the final tile is emitted — applies material across all sections and clears the timer handle. */
	void FinalizeTileEmit();

	/** Index of the next tile in Tiles[] to push via CreateMeshSection_LinearColor. */
	int32 PendingTileEmitIndex = 0;

	/** Ticker handle for the staggered tile emit pump. Reset once all tiles are pushed. */
	FTSTicker::FDelegateHandle TileEmitTickerHandle;

	/** FPlatformTime::Seconds() sample taken when the tile emit pump started — used to log total wall time. */
	double TileEmitStartTime = 0.0;

#pragma endregion PRIVATE_METHODS

#pragma region PRIVATE_PROPERTIES_AND_COMPONENTS
	
	/** The Dynamic Material Instance for standard heatmaps */
	UPROPERTY(EditAnywhere, Category = "Heatmap|MaterialsAndTextures", Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeatmapMaterialInstance;

	/** The Dynamic Material Instance for voronoi heatmaps */
	UPROPERTY(EditAnywhere, Category = "Heatmap|MaterialsAndTextures", Transient)
	TObjectPtr<UMaterialInstanceDynamic> VoronoiMaterialInstance;

	/**
	 * The Dynamic Material Instance for the trajectory surface. Same graph as the standard heatmap, but
	 * its band edges are scalar parameters set from TrajectoryLOSBands rather than the density constants
	 * baked into M_HeatmapRT_V2 — see FHeatmapLOSBands for why the two surfaces cannot share edges.
	 */
	UPROPERTY(EditAnywhere, Category = "Heatmap|MaterialsAndTextures", Transient)
	TObjectPtr<UMaterialInstanceDynamic> TrajectoryMaterialInstance;

	/** The Dynamic Texture for heatmap */
	UPROPERTY(EditAnywhere, Category = "Heatmap|MaterialsAndTextures", Transient)
	TObjectPtr<class UDynamicPixelRenderingTexture> DynamicTexture;

	/** Unblurred path counts. It is copied into DynamicTexture before each trajectory display blur. */
	UPROPERTY(Transient)
	TObjectPtr<class UDynamicPixelRenderingTexture> TrajectoryAccumulationTexture;
	
	/** Stores the meshes inverse transform, this makes it so we can translate world space coordinates to UV coordinates */
	UPROPERTY(VisibleAnywhere, Category = "Heatmap|MaterialsAndTextures")
	FTransform MeshTransform;

	/** The world reference */
	UPROPERTY()
	TObjectPtr<UWorld> World;

	/** Scaled Circle Size */
	UPROPERTY()
	int32 ScaledCircleSize; // TODO: This should be a float value for more precise locations and scale to texture and mesh size

	/**
	 * The canonical trajectory surface. Plain C++ (no UPROPERTY): it holds only float/double arrays and no
	 * UObject reference, so there is nothing for the GC to see and nothing to serialise.
	 */
	FTrajectoryField TrajectoryField;

	/**
	 * Side of the SQUARE accumulation texture, = max(GridDims.X, GridDims.Y).
	 *
	 * The field grid is non-square (it matches the floor), but the procedural mesh's UVs letterbox the
	 * minor axis into the centre of a square texture — BuildTileBuffers applies exactly the same
	 * aspect correction ActorWorldToUV does. Copying the W*H field into the centre of a
	 * max(W,H) square therefore lands cell (i,j) under the mesh point above world cell (i,j) with no
	 * further correction. Fixing the mesh UVs instead would change where the DENSITY surface samples.
	 *
	 * Kept separate from TextureWidth/TextureHeight, which stay at 1024 for the density render target:
	 * driving those from the grid would move ScaledCircleSize, i.e. silently retune density.
	 */
	int32 TrajectoryTextureSize = 0;

	/**
	 * Texel offset of grid cell (0,0) inside the square texture — the letterbox margin. Since D-B
	 * (2026-08-10) it is 0.5 * S * (1 - minorExt / majorExt), ROUNDED and clamped into [0, S - Dim], not
	 * the old (S - Dim) / 2 integer division: that truncated, and measured the margin against the ceil'd
	 * grid dims while the mesh UVs letterbox against the world-extent ratio.
	 */
	FIntPoint TrajectoryTexelOffset = FIntPoint::ZeroValue;

	/** Extent/origin the field was last sized against, so EnsureTrajectoryFieldSized can no-op. */
	FVector2D TrajectoryFieldExtentCm = FVector2D::ZeroVector;
	FVector2D TrajectoryFieldOriginCm = FVector2D::ZeroVector;
	float TrajectoryFieldSizedCmPerTexel = 0.0f;
	int32 TrajectoryFieldSizedMaxGridDim = 0;

	/** Scratch for EncodeToDisplay, reused so a 10 Hz flush does not churn a multi-MB allocation. */
	mutable TArray<uint8> TrajectoryEncodedBGRA;

	/** Red byte written per grid cell by the previous refresh; empty forces a full rewrite. */
	mutable TArray<uint8> TrajectoryPreviousRed;

#pragma endregion PRIVATE_PROPERTIES_AND_COMPONENTS
	
};
