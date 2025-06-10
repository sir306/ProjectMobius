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
#include "assimp/scene.h"
#include "UObject/Object.h"
#include "AsyncAssimpMeshLoader.generated.h"

class FAssimpMeshLoaderRunnable;
/** Delegate to tell when finished loading */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoadMeshDataComplete);

/**
 * 
 */
UCLASS()
class MOBIUSCORE_API UAsyncAssimpMeshLoader : public UObject
{
	GENERATED_BODY()

public:
	/** Constructor */
	UAsyncAssimpMeshLoader();

	/** The runnable task */
	FAssimpMeshLoaderRunnable* MeshLoaderRunnable = nullptr;

	/**
	 * Convert a WKT Polygon to a triangulated mesh. Using the assimp library to triangulate the polygon.
	 * 
	 * @param InPolygonData WKT Polygon data as an array of FVector2D points.
	 * @param OutVertices The output of the triangulated vertices.
	 * @return returns an array of FIntVector indices representing the triangulated polygon triangles.
	 */
	UFUNCTION(BlueprintCallable)
	static TArray<FIntVector> TriangulateWktPolygon(const TArray<FVector2D>& InPolygonData, TArray<FVector>& OutVertices);
	
};

class MOBIUSCORE_API FAssimpMeshLoaderRunnable final : public FRunnable
{
public:
	FAssimpMeshLoaderRunnable(const FString InPathToMesh, bool bInStringIsObjString = false);
	virtual ~FAssimpMeshLoaderRunnable() override;
	
	// The FRunnable interface functions
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

#pragma region MESH_PROPERTIES
	FString PathToMesh;
	int32 SectionCount;
	FString ErrorMessageCode;
	TArray<FVector> Vertices;
	TArray<int32> Faces;
	TArray<FVector> Normals;
	TArray<FVector2D> UV;
	TArray<FVector> Tangents;
#pragma endregion MESH_PROPERTIES
	/** is the file path actually an obj string */
	bool bIsObjString = false;
	TArray<FVector2D> Polygon = TArray<FVector2D>();

	/** Delegate to broadcast when the mesh data has finished loading */
	FOnLoadMeshDataComplete OnLoadMeshDataComplete;

protected:
	/** Pointer to a thread */
	FRunnableThread* Thread = nullptr;

	/** Bool to tell when the thread should stop */
	bool bShouldStop = false;

	/**
	 * method to process an actual mesh from a directory
	 */
	void ProcessMeshFromFile();

	/**
	 * Process a mesh from an obj string
	 */
	void ProcessMeshFromString();

	/**
	 * This function is called to rotate the mesh data to the correct orientation based on the axis data from the metadata,
	 * It returns an FRotator to be used to rotate the mesh data to the correct orientation
	 * @note
	 * The Axis orientations are as follows:\n
	 * X = 1 \n
	 * Y = 2 \n
	 * Z = 3 \n\n
	 * The Axis signs are as follows: \n
	 * Negate = -1 \n
	 * Positive = 1 \n\n
	 * If the data is not found it has to be assumed that the data is in the correct orientation as the data is
	 * missing or corrupt and this is a user error not a system error
	 *
	 * @param[int32] AxisUpOrientation - The axis data from the metadata that tells which axis is up
	 * @param[int32] AxisUpSign - This is the sign of the up axis
	 * @param[int32] AxisForwardOrientation - The axis data from the metadata that tells which axis is forward
	 * @param[int32] AxisForwardSign - This is the sign of the forward axis
	 * @return[FRotator] - The rotation to be applied to the mesh data
	 * 
	 */
	static FRotator GetMeshRotation(int32 AxisUpOrientation, int32 AxisUpSign, int32 AxisForwardOrientation = 0, int32 AxisForwardSign = 0);// todo see if forward is needed
	FVector TransformNormal(const FVector& InNormal, int32 AxisUpOrientation, int32 AxisForwardOrientation,
	                        int32 AxisForwardSign, int32 AxisUpSign);

	static void TransformMeshMatrix(FVector& InVector, int32 AxisUpOrientation, int32 AxisUpSign, int32 AxisForwardOrientation = 0, int32 AxisForwardSign = 0);
};

