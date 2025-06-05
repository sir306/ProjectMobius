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
#include "GameFramework/Actor.h"
#include "RuntimeHeatmapBuilder.generated.h"

class UProceduralMeshComponent;

UCLASS()
class PROJECTMOBIUS_API ARuntimeHeatmapBuilder : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ARuntimeHeatmapBuilder();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
#pragma region PUBLIC_METHODS

	/**
	* Function to generate a runtime heatmap mesh from the given Vertices
	* 
	* @param[TArray<FVector>] InVertices The Vertices to generate the mesh from
	* 
	*/
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Generation")
	void GenerateHeatmapMesh(TArray<FVector> InVertices);


#pragma endregion PUBLIC_METHODS

#pragma region PRIVATE_METHODS
	/**
	 * Method to ensure the mesh shape is rectangular or square and if not, it will be converted to a square
	 *
	 * @param[TArray<FVector>] InVertices The Vertices to generate the mesh from
	 * @return[bool] True if the mesh is rectangular or square, false otherwise used to determine if the mesh was converted
	 */
	bool EnsureMeshIsRectangular(TArray<FVector>& InVertices);
	
#pragma endregion PRIVATE_METHODS

#pragma region PUBLIC_COMPONENTS_AND_PROPERTIES

	/** The Procedural Mesh Component used for generating meshes at runtime or on construction */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Component")
	TObjectPtr<UProceduralMeshComponent> RuntimeHeatmapMeshComponent;
	

	/** Material Instance for the specified Heatmap type */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Component")
	TObjectPtr<UMaterialInstanceDynamic> HeatmapMaterialInstance;

	/** Heatmap Type 1:Standard or 0:Voronoi */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshGenerator|Component")
	int32 HeatmapType = 1;

	/** Stores the vertex positions of the mesh as there will only be 4 per mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Component")
	TArray<FVector> MeshVertices = TArray<FVector>();

	/** Stores UV array position */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Component")
	TArray<FVector2D> MeshUVs = TArray<FVector2D>();

	/** Stores Triangle index order */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Component")
	TArray<int32> MeshTriangles = TArray<int32>();

	/** */

#pragma endregion PUBLIC_COMPONENTS_AND_PROPERTIES
};
