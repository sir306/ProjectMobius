// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 Nicholas R. Harding
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
#include "GameFramework/Actor.h"
#include "HeatmapPixelTextureVisualizer.generated.h"


class UProceduralMeshComponent;
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
class VISUALIZATION_API AHeatmapPixelTextureVisualizer : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AHeatmapPixelTextureVisualizer();
	
	// OnConstruction is called whenever the actor is placed or the values are changed in the editor
	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void PostInitializeComponents() override;
	void AssignMaterialInstanceToMesh() const;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

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
	
	
	/** Creates and assigns the materials to the instances if not already done */
	void CreateMaterialInstances();

	/**
	 * Create and setup, the render target and bind the heatmap material instance parameter to it
	 */
	void SetupDynamicTexture() const;

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
	 * Save Heatmap to PNG
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void SaveHeatmapToPNG() const;
	void SaveHeatmapToPNG(const FString& CurrentTimeString) const;
	
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
	//int32 CircleRadius = 101; // 101 = 1.01m for our scaled data - TODO: SORT THIS OUT FOR BETTER SCALING 
	int32 CircleRadius = 110; // 110 = 1.1m for our scaled data - TODO: SORT THIS OUT FOR BETTER SCALING 

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
	
#pragma endregion PUBLIC_PROPERTIES_AND_COMPONENTS

private:
#pragma region PRIVATE_METHODS
	/**
	 * Method to generate a square cell size that 2 triangles will be within
	 *
	 * @param[FIntPoint&] NumberOfTriangles The number of triangles needed for the mesh in the X and Y direction
	 * @param[FVector2D&] MeshSize The size of the mesh in the X and Y direction
	 * @return[FVector2D] The size of the square cell
	 */
	static FVector2d GenerateSquareCellSize(const FIntPoint& NumberOfTriangles, const FVector2D& MeshSize);
	
	/**
	 * Method to calculate the number of triangles needed for the mesh when using 3D heatmaps - TODO: this may be needed for all heatmap types due to the cost asscoiated with the mesh generation
	 *
	 * @param[FVector2D&] MeshSize - The size of the mesh in the X and Y direction
	 * @param[FIntPoint&] TextureSize - The size of the texture in the X and Y direction
	 * @return[FIntPoint] The number of triangles needed for the mesh in the X and Y direction
	 */
	static FIntPoint CalculateNumberOfTriangles(const FVector2D& MeshSize, const FIntPoint& TextureSize);
	void CreateMeshVertexsAndUVs(FIntPoint NumTriangles, FVector2D CellSize);
	void GenerateMeshTrianglesInQuadMapping(FIntPoint NumTriangles, TArray<FBox3d> Quads);
	/**
	 * Method to generate the mesh vertices, UVs and triangles for the heatmap mesh
	 *
	 * @param[FVector2D&] MeshSize The size of the mesh in the X and Y direction
	 * @param[FIntPoint&] TextureSize The size of the texture in the X and Y direction
	 * @param[bool] bIs3DHeatmap A bool to determine if the heatmap is 3D or 2D
	 */
	void GenerateMeshVerticesUVsAndTriangles(const FVector2D& MeshSize, const FIntPoint& TextureSize, bool bIs3DHeatmap = false);

	/**
	 * Helper method to find all the quads that will be valid for mesh building the heatmap
	 */
	TArray<FBox3d> FindAllQuads(class ARuntimeMeshBuilder* MeshBuilder = nullptr) const;

#pragma endregion PRIVATE_METHODS

#pragma region PRIVATE_PROPERTIES_AND_COMPONENTS
	
	/** The Dynamic Material Instance for standard heatmaps */
	UPROPERTY(EditAnywhere, Category = "Heatmap|MaterialsAndTextures", Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeatmapMaterialInstance;

	/** The Dynamic Material Instance for voronoi heatmaps */
	UPROPERTY(EditAnywhere, Category = "Heatmap|MaterialsAndTextures", Transient)
	TObjectPtr<UMaterialInstanceDynamic> VoronoiMaterialInstance;

	/** The Dynamic Texture for heatmap */
	UPROPERTY(EditAnywhere, Category = "Heatmap|MaterialsAndTextures", Transient)
	TObjectPtr<class UDynamicPixelRenderingTexture> DynamicTexture;
	
	/** Stores the meshes inverse transform, this makes it so we can translate world space coordinates to UV coordinates */
	UPROPERTY(VisibleAnywhere, Category = "Heatmap|MaterialsAndTextures")
	FTransform MeshTransform;

	/** The world reference */
	UPROPERTY()
	TObjectPtr<UWorld> World;

	/** Scaled Circle Size */
	UPROPERTY()
	int32 ScaledCircleSize; // TODO: This should be a float value for more precise locations and scale to texture and mesh size
	
#pragma endregion PRIVATE_PROPERTIES_AND_COMPONENTS
	
};