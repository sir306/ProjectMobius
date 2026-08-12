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
#include "Ifc/MobiusIfcMeshLoader.h"
#include "UE_Assimp/Public/AIScene.h"
#include "UObject/Object.h"
#include "AsyncAssimpMeshLoader.generated.h"

/** Orientation for a mesh axis. */
UENUM()
enum class EAxisOrientation : int32
{
    Unknown = 0,
    X = 1,
    Y = 2,
    Z = 3
};

/** Sign for a mesh axis direction. */
UENUM()
enum class EAxisSign : int32
{
    Negative = -1,
    Positive = 1
};

class FAssimpMeshLoaderRunnable;
/** Delegate to tell when finished loading */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoadMeshDataComplete);

// WKT collection geometry can contain multiple polygons, each with its own holes.
// this struct provides a way to remove holes from the outer polygon
struct FPolygonWithHoles
{
	TArray<FVector2D> Outer;
	TArray<TArray<FVector2D>> Holes;
};

/** Per-submesh buffers produced by the Assimp loader. Indices are submesh-local (no offset remap). */
struct FAssimpSubmeshBuffers
{
	TArray<FVector>   Vertices;
	TArray<int32>     Faces;
	TArray<FVector>   Normals;
	TArray<FVector2D> UV;

	/**
	 * Source provenance, populated ONLY by the IFC path (FMobiusIfcMeshLoader) and empty for every
	 * other format. Carried on the submesh rather than in a side table because SplitSubmeshByTriCap
	 * can turn one submesh into several sections, and after that split there is no other way to get
	 * from a ProcMesh section index back to the entity it came from.
	 *
	 * IfcGloballyUniqueId (22-char base64) of the IFC product. Kept because IFC semantics are the
	 * point of importing IFC at all: IfcSpace is the room-polygon source for the B-RISK work, and
	 * retrofitting per-section identity after the fact is painful.
	 */
	FString SourceGuid;

	/** IFC entity class exactly as IFC++ names it, e.g. "IfcWallStandardCase". Empty for non-IFC. */
	FString SourceIfcClass;

	/**
	 * SEMANTIC material name — IfcMaterial / layer-set name for IFC, aiMaterial name for fbx/obj.
	 * Metadata and filtering, not what gets rendered; that is Material below. The two are independent:
	 * a product can be named "Concrete" and carry no colour, or carry a colour and no name.
	 */
	FString SourceMaterialName;

	/**
	 * Visual material as authored in the source file, filled by BOTH import paths. When
	 * bHasMaterial is false the source said nothing about appearance and the consumer must keep its own
	 * material — false is NOT "black".
	 */
	FMobiusMeshMaterial Material;
};

/**
 * Split a submesh into chunks each capped at MaxTris triangles.
 *
 * Small submeshes pass through unchanged (single Out entry, zero vertex duplication).
 * Oversized submeshes are partitioned in triangle order; each chunk copies only the
 * vertices referenced by its own triangles, with indices remapped to the chunk-local
 * vertex table. Only boundary vertices (referenced from triangles in multiple chunks)
 * are duplicated across chunks.
 *
 * @param In       Source submesh. Untouched.
 * @param MaxTris  Triangle cap per output chunk. Values <= 0 disable splitting.
 * @param Out      Chunks are appended (existing entries preserved).
 */
void SplitSubmeshByTriCap(const FAssimpSubmeshBuffers& In, int32 MaxTris, TArray<FAssimpSubmeshBuffers>& Out);


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
	FAssimpMeshLoaderRunnable(const FString InPathToMesh, TWeakObjectPtr<UObject> InWorldContextObject = TWeakObjectPtr<UObject>());
	virtual ~FAssimpMeshLoaderRunnable() override;
	
	// The FRunnable interface functions
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

#pragma region MESH_PROPERTIES
	FString PathToMesh;
	int32 SectionCount;
	FString ErrorMessageCode;

	/**
	 * IFC load diagnostics (schema, product/triangle counts, render-filter summary, IfcSpace room
	 * volumes). Only populated when bIsIfcExtension; default-constructed otherwise. The consumer
	 * (ARuntimeMeshBuilder::GetTheAsyncMeshData) moves this out and logs FilterSummary exactly once,
	 * on the game thread, after the load -- never inside the per-product loop.
	 */
	FMobiusIfcLoadStats IfcLoadStats;

	/** Per-submesh buffers. One entry per aiMesh in the source scene. Consumers iterate this to emit one ProcMesh section per submesh. */
	TArray<FAssimpSubmeshBuffers> Submeshes;

	/** Flattened aggregate buffers. Retained for transitional callers that still expect monolithic data; will be removed once all callers consume Submeshes. */
	TArray<FVector> Vertices;
	TArray<int32> Faces;
	TArray<FVector> Normals;
	TArray<FVector2D> UV;
	TArray<FVector> Tangents;

	/** Sum of per-submesh vertex counts. Useful for memory stats and parity checks against the flat buffer. */
	int32 GetTotalVertexCount() const
	{
		int32 Total = 0;
		for (const FAssimpSubmeshBuffers& Sub : Submeshes)
		{
			Total += Sub.Vertices.Num();
		}
		return Total;
	}
#pragma endregion MESH_PROPERTIES
	/** is the file path actually an obj string */
	bool bIsWktExtension = false;

	/** .ifc input -- routed to FMobiusIfcMeshLoader instead of Assimp (Assimp's IFCLoader is IFC2X3-only). */
	bool bIsIfcExtension = false;


	/** Delegate to broadcast when the mesh data has finished loading */
	FOnLoadMeshDataComplete OnLoadMeshDataComplete;

protected:
	/** Optional world context for routing errors to UI on the game thread. */
	TWeakObjectPtr<UObject> WorldContextObject;
	/** Pointer to a thread */
	FRunnableThread* Thread = nullptr;

	/** Bool to tell when the thread should stop */
	bool bShouldStop = false;

	FString ErrorMessage = FString();
	FString WktDataString = FString();

	/**
	 * method to process an actual mesh from a directory
	 */
	void ProcessMeshFromFile();

	/**
	 * Process a mesh from an obj string
	 */
	void ProcessMeshFromString();

	/**
	 * Load an .ifc file through FMobiusIfcMeshLoader (IFC++ via the MobiusIfcBridge C shim) and fill
	 * Submeshes + IfcLoadStats. Assimp is not involved: its IFCLoader is hard-coded to IFC2X3
	 * (`fileSchema.substr(0,4) != "IFC2"`) and Mobius has to read IFC4X3_ADD2.
	 */
	void ProcessIfcFromFile();

	/**
	 * Load and convert WKT data into obj string format.
	 */
	void LoadWKTDataToObjString();

	bool LoadWKTFile(const FString& FilePath, FString& OutWKTData, FString& OutErrorMessage);

	TArray<FVector2D> ParseWKTData(const FString& InWKTDataString, FString& OutErrorMessage);
	
        bool ParseGeometryCollectionWkt(const FString& WKTString, TArray<FPolygonWithHoles>& OutPolygons, FString& OutErrorMessage);
        //bool ParseGeometryCollectionWkt(const FString& WKTString, TArray<TArray<FVector2D>>& OutGeometries, FString& OutErrorMessage);

       /**
        * Fill Vertices, Faces and Normals arrays using data from an aiScene.
        * Scaling and orientation are handled using the scene meta data when
        * available. If no meta data exists sensible defaults are used.
        */
       void FillDataFromScene(const aiScene* Scene);

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
        * @param AxisUpOrientation   Which axis is considered up
        * @param AxisUpSign          The sign of the up axis
        * @param AxisForwardOrientation Which axis is considered forward
        * @param AxisForwardSign     The sign of the forward axis
	 * @return[FRotator] - The rotation to be applied to the mesh data
	 * 
	 */
       static FRotator GetMeshRotation(EAxisOrientation AxisUpOrientation, EAxisSign AxisUpSign,
                                       EAxisOrientation AxisForwardOrientation = EAxisOrientation::Unknown,
                                       EAxisSign AxisForwardSign = EAxisSign::Positive);// todo see if forward is needed
       FVector TransformNormal(const FVector& InNormal, EAxisOrientation AxisUpOrientation,
                               EAxisOrientation AxisForwardOrientation,
                               EAxisSign AxisForwardSign, EAxisSign AxisUpSign);

       static void TransformMeshMatrix(FVector& InVector, EAxisOrientation AxisUpOrientation,
                                       EAxisSign AxisUpSign,
                                       EAxisOrientation AxisForwardOrientation = EAxisOrientation::Unknown,
                                       EAxisSign AxisForwardSign = EAxisSign::Positive);
};

