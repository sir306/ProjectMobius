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

#include "AsyncAssimpMeshLoader.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/mesh.h"
#include "assimp/material.h"
#include "assimp/texture.h"
#include "assimp/postprocess.h"
#include "Kismet/KismetMathLibrary.h"
#include <array>
#include <vector>
#include <earcut_hpp/earcut.hpp>
using Coord = std::array<double,2>;


UAsyncAssimpMeshLoader::UAsyncAssimpMeshLoader()
{
}

TArray<FIntVector> UAsyncAssimpMeshLoader::TriangulateWktPolygon(const TArray<FVector2D>& Polygon,
                                                                 TArray<FVector>& OutVertices)
{
	TArray<FIntVector> Triangles;

	if (Polygon.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("Polygon must have at least 3 points."));
		return Triangles;
	}

	// Generate OBJ data string
	FString OBJ = TEXT("o WKTPolygon\n");
	for (const FVector2D& P : Polygon)
	{
		OBJ += FString::Printf(TEXT("v %f %f 0.0\n"), P.X, P.Y);
	}
	OBJ += TEXT("f");
	for (int32 i = 1; i <= Polygon.Num(); ++i)
	{
		OBJ += FString::Printf(TEXT(" %d"), i);
	}
	OBJ += TEXT("\n");

	std::string OBJData = TCHAR_TO_UTF8(*OBJ);
	Assimp::Importer Importer;
	const aiScene* Scene = Importer.ReadFileFromMemory(
		OBJData.c_str(), OBJData.size(),
		aiProcess_Triangulate | aiProcess_JoinIdenticalVertices,
		"obj");

	if (!Scene || !Scene->HasMeshes())
	{
		UE_LOG(LogTemp, Error, TEXT("Assimp failed to triangulate: %s"), UTF8_TO_TCHAR(Importer.GetErrorString()));
		return Triangles;
	}

	const aiMesh* Mesh = Scene->mMeshes[0];
	OutVertices.Empty();
	for (unsigned int i = 0; i < Mesh->mNumVertices; ++i)
	{
		const aiVector3D& V = Mesh->mVertices[i];
		OutVertices.Add(FVector(V.x, V.y, V.z));
	}

	for (unsigned int i = 0; i < Mesh->mNumFaces; ++i)
	{
		const aiFace& Face = Mesh->mFaces[i];
		if (Face.mNumIndices == 3)
		{
			Triangles.Add(FIntVector(Face.mIndices[0], Face.mIndices[1], Face.mIndices[2]));
		}
	}

	return Triangles;
}

FAssimpMeshLoaderRunnable::FAssimpMeshLoaderRunnable(const FString InPathToMesh)
{
	if(InPathToMesh.IsEmpty())
	{
		
		return;
	}
	else if(!FPaths::FileExists(InPathToMesh))
	{
		// if the path to the mesh is not a valid file path and the string is not an obj string then return
		UE_LOG(LogTemp, Warning, TEXT("The path to the mesh is not a valid file path: %s"), *InPathToMesh);
		return;
	}
	
	PathToMesh = InPathToMesh;
	// if file has .wkt extension then it is a WKT file
	bIsWktExtension = PathToMesh.EndsWith(TEXT(".wkt"), ESearchCase::IgnoreCase);
	

	// Create the thread -- The thread priority is set to TPri_Normal this may need to be adjusted based on the application
	Thread = FRunnableThread::Create(this, TEXT("FAssimpMeshLoaderRunnable"), 0, TPri_Normal);
}

FAssimpMeshLoaderRunnable::~FAssimpMeshLoaderRunnable()
{
	// if the thread is still running, stop it
	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
	}
}

uint32 FAssimpMeshLoaderRunnable::Run()
{
	if (bIsWktExtension)
	{
		ProcessMeshFromString();
	}
	else
	{
		ProcessMeshFromFile();
	}

	// sleep the thread for 0.5 seconds
	FPlatformProcess::Sleep(0.5f);

	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		// Broadcast complete
		OnLoadMeshDataComplete.Broadcast();
	});
	
	return 0;
}

void FAssimpMeshLoaderRunnable::Stop()
{
	bShouldStop = true;
}

void FAssimpMeshLoaderRunnable::Exit()
{
	FRunnable::Exit();
}

void FAssimpMeshLoaderRunnable::ProcessMeshFromFile()
{
	// Broadcast the current percentage of the data loaded as 0 this way the ui will show

	Assimp::Importer Importer;
	const std::string Filename(TCHAR_TO_UTF8(*PathToMesh));
	const aiScene* Scene = Importer.ReadFile(Filename, aiProcess_MakeLeftHanded | aiProcess_FlipUVs |
	                                         aiProcess_PreTransformVertices | aiProcess_Triangulate |
	                                         aiProcess_GenNormals | aiProcess_CalcTangentSpace);

	if (!Scene)
	{
		ErrorMessageCode = Importer.GetErrorString();
		return;
	}

	if (!Scene->HasMeshes())
	{
		ErrorMessageCode = "The scene does not have any meshes";
		return;
	}

	FillDataFromScene(Scene);
}
// this version loads boundaries correctly and shows where holes are needed
void FAssimpMeshLoaderRunnable::ProcessMeshFromString()
{
	LoadWKTDataToObjString();
	std::string OBJData = TCHAR_TO_UTF8(*WktDataString);
	Assimp::Importer Importer;
	const aiScene* Scene = Importer.ReadFileFromMemory(
		OBJData.c_str(), OBJData.size(),
		aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenNormals | aiProcess_CalcTangentSpace,
		//TODO: Need to workout a way to handle normals for WKT data -> filters just don't work for this
		"obj");

	if (!Scene || !Scene->HasMeshes())
	{
		UE_LOG(LogTemp, Error, TEXT("Assimp failed to triangulate: %s"), UTF8_TO_TCHAR(Importer.GetErrorString()));
		return;
	}

	FillDataFromScene(Scene);
}
void FAssimpMeshLoaderRunnable::LoadWKTDataToObjString()
{
    // ——— 1) Load raw WKT from disk —————————————————————————————————————————————————————
    FString RawWkt;
    if (!LoadWKTFile(PathToMesh, RawWkt, ErrorMessage))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load WKT file: %s"), *ErrorMessage);
        return;
    }

    // ——— 2) Parse into polygons (outer + holes) ————————————————————————————————————————
    TArray<FPolygonWithHoles> Polygons;
    if (!ParseGeometryCollectionWkt(RawWkt, Polygons, ErrorMessage) || Polygons.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse WKT: %s"), *ErrorMessage);
        return;
    }

    // Merge all into one outer ring + all hole rings
    FPolygonWithHoles Combined = MoveTemp(Polygons[0]);
    for (int32 i = 1; i < Polygons.Num(); ++i)
    {
        Combined.Holes.Add(MoveTemp(Polygons[i].Outer));
        for (auto& inner : Polygons[i].Holes)
            Combined.Holes.Add(MoveTemp(inner));
    }

    // ——— 3) Build rings for Earcut ————————————————————————————————————————————————
    std::vector<std::vector<Coord>> Rings;
    Rings.reserve(1 + Combined.Holes.Num());

    // outer ring
    Rings.emplace_back();
    for (auto& P : Combined.Outer)
        Rings[0].push_back({ double(P.X), double(P.Y) });

    // hole rings
    for (auto& Hole : Combined.Holes)
    {
        Rings.emplace_back();
        for (auto& P : Hole)
            Rings.back().push_back({ double(P.X), double(P.Y) });
    }

    // ——— 4) Triangulate floor ——————————————————————————————————————————————————————
    std::vector<size_t> Indices = mapbox::earcut<size_t>(Rings);

    // ——— 5) Emit OBJ: floor + walls ——————————————————————————————————————————————
    WktDataString.Empty();

    // 5a) Bottom vertices at Z = 0
    int32 TotalBaseVerts = 0;
    for (auto& ring : Rings)
    {
        for (auto& c : ring)
        {
            WktDataString += FString::Printf(
                TEXT("v %f %f 0.0\n"),
                float(c[0] * 100.0),
                float(c[1] * 100.0)
            );
            ++TotalBaseVerts;
        }
    }

    // 5b) Floor faces (double-sided)
    for (size_t i = 0; i + 2 < Indices.size(); i += 3)
    {
        int32 A = int32(Indices[i]   + 1);
        int32 B = int32(Indices[i+1] + 1);
        int32 C = int32(Indices[i+2] + 1);

        // upward-facing
        WktDataString += FString::Printf(TEXT("f %d %d %d\n"), A, B, C);
        // downward-facing (reverse winding)
        WktDataString += FString::Printf(TEXT("f %d %d %d\n"), A, C, B);
    }

    // ——— 6) Extrude walls up 1 m (100 cm) ————————————————————————————————————————
    const float Height = 100.0f;
    int32 VertexOffsetTop = TotalBaseVerts;
    int32 Offset = 0;

    // 6a) Top vertices at Z = Height
    for (auto& ring : Rings)
    {
        for (auto& c : ring)
        {
            WktDataString += FString::Printf(
                TEXT("v %f %f %f\n"),
                float(c[0] * 100.0),
                float(c[1] * 100.0),
                Height
            );
        }
    }

    // 6b) Wall faces (double-sided quads)
    for (auto& ring : Rings)
    {
        int32 N = int32(ring.size());
        for (int32 i = 0; i < N; ++i)
        {
            int32 A    = Offset + i;
            int32 B    = Offset + ((i + 1) % N);
            int32 ATop = VertexOffsetTop + Offset + i;
            int32 BTop = VertexOffsetTop + Offset + ((i + 1) % N);

            // outward‐facing
            WktDataString += FString::Printf(TEXT("f %d %d %d\n"), A+1, B+1, BTop+1);
            WktDataString += FString::Printf(TEXT("f %d %d %d\n"), A+1, BTop+1, ATop+1);
            // inward‐facing
            WktDataString += FString::Printf(TEXT("f %d %d %d\n"), BTop+1, B+1, A+1);
            WktDataString += FString::Printf(TEXT("f %d %d %d\n"), ATop+1, BTop+1, A+1);
        }
        Offset += N;
    }

    // Now hand WktDataString off to ProcessMeshFromString()/Assimp…
}

// void FAssimpMeshLoaderRunnable::LoadWKTDataToObjString()
// {
// 	// 1) Load the raw WKT (JSON-wrapped) from disk
// 	FString RawWkt;
// 	if (!LoadWKTFile(PathToMesh, RawWkt, ErrorMessage))
// 	{
// 		UE_LOG(LogTemp, Error, TEXT("Failed to load WKT file: %s"), *ErrorMessage);
// 		return;
// 	}
//
// 	// 2) Parse into one or more polygons
// 	TArray<TArray<FVector2D>> Geometries;
// 	if (!ParseGeometryCollectionWkt(RawWkt, Geometries, ErrorMessage))
// 	{
// 		UE_LOG(LogTemp, Error, TEXT("Failed to parse geometry: %s"), *ErrorMessage);
// 		return;
// 	}
//
// 	// 3) Begin building the OBJ string
// 	WktDataString.Empty();
// 	int32 VertexOffset = 0;  // keep track of 1-based OBJ indexing
//
// 	for (const TArray<FVector2D>& Geometry : Geometries)
// 	{
// 		const int32 NumPts = Geometry.Num();
// 		if (NumPts < 3) continue;
//
// 		// keep track of where our base vertices start in the global OBJ index
// 		const int32 BaseStart = VertexOffset;
//
// 		// 1) emit the base ring (Z = 0)
// 		for (const FVector2D& P : Geometry)
// 		{
// 			// assuming input is in meters, scale to centimeters for OBJ (multiply by 100)
// 			FVector V(P.X * 100.0f, P.Y * 100.0f, 0.0f);
// 			WktDataString += FString::Printf(TEXT("v %f %f %f\n"), V.X, V.Y, V.Z);
// 		}
// 		
// 		// 2) cap *that* ring double-sided (so it can only ever seal the floor)
// 		{
// 			// original side
// 			WktDataString += TEXT("f");
// 			for (int32 i = 0; i < NumPts; ++i)
// 			{
// 				WktDataString += FString::Printf(TEXT(" %d"), BaseStart + i + 1);
// 			}
// 			WktDataString += TEXT("\n");
//
// 			// reversed side
// 			WktDataString += TEXT("f");
// 			for (int32 i = NumPts - 1; i >= 0; --i)
// 			{
// 				WktDataString += FString::Printf(TEXT(" %d"), BaseStart + i + 1);
// 			}
// 			WktDataString += TEXT("\n");
// 		}
//
// 		// // 3) emit the top ring (Z = 100)
// 		// const int32 TopStart = BaseStart + NumPts;
// 		// for (const FVector2D& P : Geometry)
// 		// {
// 		// 	FVector V(P.X * 100.0f, P.Y * 100.0f, 100.0f);
// 		// 	WktDataString += FString::Printf(TEXT("v %f %f %f\n"), V.X, V.Y, V.Z);
// 		// }
// 		//
// 		// // 4) now build double-sided walls
// 		// for (int32 i = 0; i < NumPts; ++i)
// 		// {
// 		// 	const int32 A    = BaseStart + i + 1;
// 		// 	const int32 B    = BaseStart + ((i + 1) % NumPts) + 1;
// 		// 	const int32 ATop = TopStart  + i + 1;
// 		// 	const int32 BTop = TopStart  + ((i + 1) % NumPts) + 1;
// 		//
// 		// 	// outer
// 		// 	WktDataString += FString::Printf(TEXT("f %d %d %d\n"), A,    B,    BTop);
// 		// 	WktDataString += FString::Printf(TEXT("f %d %d %d\n"), A,    BTop, ATop);
// 		//
// 		// 	// inner (reversed)
// 		// 	WktDataString += FString::Printf(TEXT("f %d %d %d\n"), BTop, B,    A);
// 		// 	WktDataString += FString::Printf(TEXT("f %d %d %d\n"), ATop, BTop, A);
// 		// }
//
// 		// advance for next polygon
// 		//VertexOffset += NumPts * 2;
// 		VertexOffset += NumPts;
// 	}
// 	
// }
// void FAssimpMeshLoaderRunnable::LoadWKTDataToObjString()
// {
// 	// 1) Load the raw WKT (JSON-wrapped) from disk
//     FString RawWkt;
//     if (!LoadWKTFile(PathToMesh, RawWkt, ErrorMessage))
//     {
//         UE_LOG(LogTemp, Error, TEXT("Failed to load WKT file: %s"), *ErrorMessage);
//         return;
//     }
//
//     // 2) Parse into polygons-with-holes
//     TArray<FPolygonWithHoles> Polygons;
//     if (!ParseGeometryCollectionWkt(RawWkt, Polygons, ErrorMessage))
//     {
//         UE_LOG(LogTemp, Error, TEXT("Failed to parse geometry: %s"), *ErrorMessage);
//         return;
//     }
//     if (Polygons.Num() == 0)
//     {
//         UE_LOG(LogTemp, Error, TEXT("No polygons found in WKT."));
//         return;
//     }
//
//     // 3) **Combine** all parsed rings into a single outer+holes
//     FPolygonWithHoles Combined = MoveTemp(Polygons[0]);
//     for (int32 i = 1; i < Polygons.Num(); ++i)
//     {
//         Combined.Holes.Add(MoveTemp(Polygons[i].Outer));
//         for (auto& innerHole : Polygons[i].Holes)
//         {
//             Combined.Holes.Add(MoveTemp(innerHole));
//         }
//     }
//
//     // 4) Debug draw (keep your existing debug code)
//     UWorld* World = nullptr;
//     if (GEngine && GEngine->GameViewport)
//         World = GEngine->GameViewport->GetWorld();
//
//     if (World)
//     {
//         const float Zpt      = 10.0f;
//         const float Zline    = 10.0f;
//         const float Duration = 30.0f;
//         const float Width    = 2.0f;
//         const float PointSize= 8.0f;
//
//         // Outer ring: green points + lines
//         int32 N = Combined.Outer.Num();
//         for (int32 i = 0; i < N; ++i)
//         {
//             FVector V(Combined.Outer[i].X * 100, Combined.Outer[i].Y * 100, Zpt);
//             DrawDebugPoint(World, V, PointSize, FColor::Green, true, Duration);
//             FVector A = V;
//             FVector B(Combined.Outer[(i+1)%N].X * 100,
//                       Combined.Outer[(i+1)%N].Y * 100,
//                       Zline);
//             DrawDebugLine(World, A, B, FColor::Green, true, Duration, 0, Width);
//         }
//
//         // Hole rings: red points + lines
//         for (auto& Hole : Combined.Holes)
//         {
//             int32 M = Hole.Num();
//             for (int32 j = 0; j < M; ++j)
//             {
//                 FVector V(Hole[j].X * 100, Hole[j].Y * 100, Zpt);
//                 DrawDebugPoint(World, V, PointSize, FColor::Red, true, Duration);
//                 FVector A = V;
//                 FVector B(Hole[(j+1)%M].X * 100,
//                           Hole[(j+1)%M].Y * 100,
//                           Zline);
//                 DrawDebugLine(World, A, B, FColor::Red, true, Duration, 0, Width);
//             }
//         }
//     }
//
//     // 5) Create triangulated mesh using ear clipping or similar approach
//     // Since Assimp doesn't handle polygon-with-holes well in OBJ format,
//     // we'll use a simpler approach: create individual triangles
//     
//     WktDataString.Empty();
//     int32 VertexOffset = 0;
//
//     // Helper to emit vertices
//     auto EmitRing = [&](const TArray<FVector2D>& Ring, TArray<int32>& OutIndices)
//     {
//         OutIndices.Reserve(Ring.Num());
//         for (int32 i = 0; i < Ring.Num(); ++i)
//         {
//             FVector V(Ring[i].X * 100.0, Ring[i].Y * 100.0, 0.0f);
//             WktDataString += FString::Printf(TEXT("v %f %f %f\n"), V.X, V.Y, V.Z);
//             OutIndices.Add(VertexOffset + i);
//         }
//         VertexOffset += Ring.Num();
//     };
//
//     // Emit all vertices first
//     TArray<int32> OuterIdx;
//     EmitRing(Combined.Outer, OuterIdx);
//     
//     TArray<TArray<int32>> HoleIdx;
//     for (auto& Hole : Combined.Holes)
//     {
//         TArray<int32> ThisHole;
//         EmitRing(Hole, ThisHole);
//         HoleIdx.Add(MoveTemp(ThisHole));
//     }
//
//     // 6) Use simple triangulation for outer ring (fan triangulation)
//     // This works for convex polygons and simple cases
//     if (Combined.Holes.Num() == 0)
//     {
//         // Simple case: no holes, just fan triangulate
//         for (int32 i = 1; i < OuterIdx.Num() - 1; ++i)
//         {
//             WktDataString += FString::Printf(TEXT("f %d %d %d\n"), 
//                 OuterIdx[0] + 1, OuterIdx[i] + 1, OuterIdx[i + 1] + 1);
//         }
//     }
//     else
//     {
//         // Complex case: use your existing triangulation method
//         // Create a temporary OBJ with just the outer ring for triangulation
//         FString TempOBJ = TEXT("# Temp OBJ for triangulation\n");
//         
//         // Add vertices
//         for (const FVector2D& P : Combined.Outer)
//         {
//             TempOBJ += FString::Printf(TEXT("v %f %f 0.0\n"), P.X, P.Y);
//         }
//         
//         // Add face (this will be triangulated by Assimp)
//         TempOBJ += TEXT("f");
//         for (int32 i = 1; i <= Combined.Outer.Num(); ++i)
//         {
//             TempOBJ += FString::Printf(TEXT(" %d"), i);
//         }
//         TempOBJ += TEXT("\n");
//         
//         // Use Assimp to triangulate
//         std::string TempOBJData = TCHAR_TO_UTF8(*TempOBJ);
//         Assimp::Importer TempImporter;
//         const aiScene* TempScene = TempImporter.ReadFileFromMemory(
//             TempOBJData.c_str(), TempOBJData.size(),
//             aiProcess_Triangulate | aiProcess_JoinIdenticalVertices,
//             "obj");
//             
//         if (TempScene && TempScene->HasMeshes())
//         {
//             const aiMesh* TempMesh = TempScene->mMeshes[0];
//             
//             // Copy triangulated faces to our OBJ string
//             for (unsigned int i = 0; i < TempMesh->mNumFaces; ++i)
//             {
//                 const aiFace& Face = TempMesh->mFaces[i];
//                 if (Face.mNumIndices == 3)
//                 {
//                     WktDataString += FString::Printf(TEXT("f %d %d %d\n"),
//                         Face.mIndices[0] + 1,
//                         Face.mIndices[1] + 1, 
//                         Face.mIndices[2] + 1);
//                 }
//             }
//         }
//         
//         // TODO: Handle holes properly - this requires more complex triangulation
//         // For now, just triangulate the outer ring
//     }
// }


bool FAssimpMeshLoaderRunnable::LoadWKTFile(const FString& FilePath, FString& OutWKTData, FString& OutErrorMessage)
{
	// Check if the file exists
	if (!FPaths::FileExists(FilePath))
	{
		OutErrorMessage = FString::Printf(TEXT("File not found: %s"), *FilePath);
		return false;
	}

	// Load the file content
	if (FFileHelper::LoadFileToString(OutWKTData, *FilePath))
	{
		// Successfully loaded the file
		return true;
	}

	// failed to load the file and parse as string
	OutErrorMessage = FString::Printf(TEXT("Failed to load WKT file: %s"), *FilePath);

	// failed to load the file
	return false;
}

TArray<FVector2D> FAssimpMeshLoaderRunnable::ParseWKTData(const FString& InWKTDataString, FString& OutErrorMessage)
{
	FString CleanWKT = InWKTDataString;
	CleanWKT.TrimStartAndEndInline();
	CleanWKT = CleanWKT.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(""));

	FString Prefix;
	FString CoordBlock;

	// Extract prefix and inner coordinates
	int32 OpenParenIndex;
	if (CleanWKT.FindChar('(', OpenParenIndex))
	{
		Prefix = CleanWKT.Left(OpenParenIndex).ToUpper().TrimStartAndEnd();
		CoordBlock = CleanWKT.Mid(OpenParenIndex);
		CoordBlock = CoordBlock.Replace(TEXT("("), TEXT("")).Replace(TEXT(")"), TEXT(""));
	}

	TArray<FVector2D> ParsedPoints;

	if (Prefix == TEXT("POINT"))
	{
		TArray<FString> XY;
		CoordBlock.ParseIntoArray(XY, TEXT(" "), true);
		if (XY.Num() == 2)
		{
			ParsedPoints.Add(FVector2D(FCString::Atof(*XY[0]), FCString::Atof(*XY[1])));
		}
	}
	else if (Prefix == TEXT("LINESTRING") || Prefix == TEXT("POLYGON"))
	{
		if (Prefix == TEXT("POLYGON"))
		{
			// POLYGON can have nested parentheses
			int32 InnerStart = CleanWKT.Find(TEXT("(("));
			int32 InnerEnd = CleanWKT.Find(TEXT("))"));
			if (InnerStart != INDEX_NONE && InnerEnd != INDEX_NONE)
			{
				CoordBlock = CleanWKT.Mid(InnerStart + 2, InnerEnd - InnerStart - 2);
			}
		}

		TArray<FString> Pairs;
		CoordBlock.ParseIntoArray(Pairs, TEXT(","), true);
		for (const FString& Pair : Pairs)
		{
			TArray<FString> XY;
			Pair.TrimStartAndEnd().ParseIntoArray(XY, TEXT(" "), true);
			if (XY.Num() == 2)
			{
				ParsedPoints.Add(FVector2D(FCString::Atof(*XY[0]), FCString::Atof(*XY[1])));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Unsupported WKT type: %s"), *Prefix);
		OutErrorMessage = FString::Printf(TEXT("Unsupported WKT type: %s"), *Prefix);
	}

	return ParsedPoints;
}

// bool FAssimpMeshLoaderRunnable::ParseGeometryCollectionWkt(const FString& WKTString,
//                                                            TArray<TArray<FVector2D>>& OutGeometries, FString& OutErrorMessage)
// {
// 	FString CleanWKT = WKTString;
// 	CleanWKT.TrimStartAndEndInline();
// 	CleanWKT = CleanWKT.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(""));
//
// 	if (!CleanWKT.StartsWith(TEXT("GEOMETRYCOLLECTION"), ESearchCase::IgnoreCase))
// 	{
// 		OutErrorMessage = TEXT("WKT does not begin with GEOMETRYCOLLECTION");
// 		return false;
// 	}
//
// 	// Extract the content inside the GEOMETRYCOLLECTION (...)
// 	int32 OpenParen = CleanWKT.Find(TEXT("("));
// 	int32 CloseParen = INDEX_NONE;
// 	if (OpenParen == INDEX_NONE || !CleanWKT.FindLastChar(')', CloseParen) || CloseParen <= OpenParen)
// 	{
// 		OutErrorMessage = TEXT("Malformed GEOMETRYCOLLECTION WKT.");
// 		return false;
// 	}
//
// 	FString Inner = CleanWKT.Mid(OpenParen + 1, CloseParen - OpenParen - 1).TrimStartAndEnd();
//
// 	// Look for each POLYGON block
// 	int32 Pos = 0;
// 	while (true)
// 	{
// 		int32 PolygonStart = Inner.Find(TEXT("POLYGON"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Pos);
// 		if (PolygonStart == INDEX_NONE) break;
//
// 		int32 FirstParen = Inner.Find(TEXT("(("), ESearchCase::IgnoreCase, ESearchDir::FromStart, PolygonStart);
// 		int32 EndParen = Inner.Find(TEXT("))"), ESearchCase::IgnoreCase, ESearchDir::FromStart, FirstParen + 2);
// 		if (FirstParen == INDEX_NONE || EndParen == INDEX_NONE) break;
//
// 		FString PolygonBlock = Inner.Mid(FirstParen + 2, EndParen - FirstParen - 2);
//
// 		FString DummyError;
// 		TArray<FVector2D> PolygonPoints = ParseWKTData(TEXT("LINESTRING(") + PolygonBlock + TEXT(")"), DummyError);
// 		if (!PolygonPoints.IsEmpty())
// 		{
// 			OutGeometries.Add(PolygonPoints);
// 		}
//
// 		Pos = EndParen + 2;
// 	}
//
// 	if (OutGeometries.Num() == 0)
// 	{
// 		OutErrorMessage = TEXT("No valid POLYGON found in GEOMETRYCOLLECTION.");
// 		return false;
// 	}
//
// 	return true;
// }
bool FAssimpMeshLoaderRunnable::ParseGeometryCollectionWkt(
	const FString& WKTString,
	TArray<FPolygonWithHoles>& OutPolygons,
	FString& OutErrorMessage)
{
	// --- 1) clean up
	FString Clean = WKTString;
	Clean.TrimStartAndEndInline();
	Clean.ReplaceInline(TEXT("\r"), TEXT(""));
	Clean.ReplaceInline(TEXT("\n"), TEXT(""));

	if (!Clean.StartsWith(TEXT("GEOMETRYCOLLECTION"), ESearchCase::IgnoreCase))
	{
		OutErrorMessage = TEXT("WKT does not begin with GEOMETRYCOLLECTION");
		return false;
	}

	// --- 2) strip GEOMETRYCOLLECTION(   )
	int32 firstParen = Clean.Find(TEXT("("));
	int32 lastParen  = INDEX_NONE;
	Clean.FindLastChar(')', lastParen);
	if (firstParen == INDEX_NONE || lastParen == INDEX_NONE || lastParen <= firstParen)
	{
		OutErrorMessage = TEXT("Malformed GEOMETRYCOLLECTION parentheses");
		return false;
	}
	FString inner = Clean.Mid(firstParen + 1, lastParen - firstParen - 1);

	// --- 3) find the first POLYGON(( ... ))
	int32 polyStart = inner.Find(TEXT("POLYGON"), ESearchCase::IgnoreCase);
	if (polyStart == INDEX_NONE)
	{
		OutErrorMessage = TEXT("No POLYGON found in GEOMETRYCOLLECTION");
		return false;
	}
	// locate the “((” and its matching “))”
	int32 ringBlockStart = inner.Find(TEXT("(("), ESearchCase::IgnoreCase, ESearchDir::FromStart, polyStart);
	int32 ringBlockEnd   = inner.Find(TEXT("))"), ESearchCase::IgnoreCase, ESearchDir::FromStart, ringBlockStart + 2);
	if (ringBlockStart == INDEX_NONE || ringBlockEnd == INDEX_NONE)
	{
		OutErrorMessage = TEXT("Malformed POLYGON(( ... )) block");
		return false;
	}

	// extract just the comma-delimited rings, WITHOUT the outer “((” and final “))”
	FString ringBlock = inner.Mid(ringBlockStart + 2, ringBlockEnd - (ringBlockStart + 2));
	TArray<FString> ringStrings;
	ringBlock.ParseIntoArray(ringStrings, TEXT("),"), /*bCullEmpty=*/true);

	if (ringStrings.Num() == 0)
	{
		OutErrorMessage = TEXT("No rings found inside POLYGON");
		return false;
	}

	// --- 4) parse each ring
	FPolygonWithHoles poly;
	poly.Outer.Empty();
	poly.Holes.Empty();

	for (int32 i = 0; i < ringStrings.Num(); ++i)
	{
		// remove any stray parens or whitespace
		FString coords = ringStrings[i];
		coords.ReplaceInline(TEXT("("), TEXT(""));
		coords.ReplaceInline(TEXT(")"), TEXT(""));
		coords.TrimStartAndEndInline();

		// now coords == "x1 y1, x2 y2, x3 y3, …"
		TArray<FString> pairs;
		coords.ParseIntoArray(pairs, TEXT(","), /*bCullEmpty=*/true);

		TArray<FVector2D> pts;
		pts.Reserve(pairs.Num());
		for (auto& p : pairs)
		{
			TArray<FString> xy;
			p.TrimStartAndEndInline();                  // mutate p in place
			p.ParseIntoArray(xy, TEXT(" "), /*bCullEmpty=*/true);
			if (xy.Num() == 2)
			{
				double X = FCString::Atod(*xy[0]);
				double Y = FCString::Atod(*xy[1]);
				pts.Add(FVector2D(X, Y));
			}
		}

		if (pts.Num() >= 3)
		{
			if (i == 0)
			{
				poly.Outer = MoveTemp(pts);
			}
			else
			{
				poly.Holes.Add(MoveTemp(pts));
			}
		}
	}

	if (poly.Outer.Num() < 3)
	{
		OutErrorMessage = TEXT("Outer ring has fewer than 3 points");
		return false;
	}

	OutPolygons.Add(MoveTemp(poly));
	return true;
}


FRotator FAssimpMeshLoaderRunnable::GetMeshRotation(int32 AxisUpOrientation, int32 AxisUpSign,
                                                    int32 AxisForwardOrientation, int32 AxisForwardSign)
{

	static const FRotator UpRotation[4][3] =
	{
		{// sign unknown assume zero
			{ // sign unknown assume 
				FRotator::ZeroRotator
			},
			{ // positive
				FRotator::ZeroRotator
			},
			{ // negative
				FRotator::ZeroRotator
			}
		},
		{ // X is up
			{ // sign unknown assume positive x 
				FRotator(90.0f, 0.0f, 0.0f)
			},
			{ // positive
				FRotator(90.0f, 0.0f, 0.0f)
			},
			{ // negative
				FRotator(-90.0f, 0.0f, 0.0f)
			}
		},
		{ // y is up
			{ // sign unknown assume 
				FRotator(0.0f, 0.0f, -90.0f)
			},
			{ // positive
				FRotator(0.0f, 0.0f, -90.0f)
			},
			{ // negative
				FRotator(0.0f, 0.0f, 90.0f)
			}
		},
		{ // Z is up
			{ // sign unknown assume positive
				FRotator::ZeroRotator
			},
			{ // positive
				FRotator::ZeroRotator
			},
			{ // negative
				FRotator(180.0f, 0.0f, 0.0f)
			}
		}
	};
	
	int32 AxisOrientationPicker = AxisUpOrientation == -1 ? 0 : AxisUpOrientation;
	int32 AxisUpSignPicker = AxisUpSign == -1 ? 2 : AxisUpSign;

	// make rotator based on the up axis orientation and sign
	FRotator ReturnRotation = UpRotation[AxisOrientationPicker][AxisUpSignPicker];
	
	// modify the rotation based on the forward axis orientation and sign
	if(AxisForwardOrientation != 0)
	{
		// modify the rotation based on the forward axis orientation and sign
		FRotator ForwardRotation = FRotator::ZeroRotator;
		switch (AxisForwardOrientation)
		{
		case 1:// X
			switch (AxisUpOrientation)
			{
			case 1:
				// the up and forward axis are the same this isn't possible so assume the forward axis is unknown and use the up axis
				break;
			case 2: // Y
				if(AxisUpSign == -1)
				{
					ForwardRotation = FRotator(0.0f, -90.0f, 0.0f);
				}
				else
				{
					ForwardRotation = FRotator(0.0f, 90.0f, 0.0f);
				}
				break;
			case 3: // Z
				if(AxisUpSign == -1)
				{
					ForwardRotation = FRotator(0.0f, 0.0f, 90.0f);
				}
				else
				{
					ForwardRotation = FRotator(0.0f, 0.0f, -90.0f);
				}
				break;
			default: // the up axis is unknown so use the forward axis
				if(AxisForwardSign == -1)
				{
					ForwardRotation = FRotator(0.0f, 0.0f, 90.0f);
				}
				else
				{
					ForwardRotation = FRotator(0.0f, 0.0f, -90.0f);
				}
				break;
			}
			break;
		case 2:// Y
			ForwardRotation = FRotator(0.0f, 0.0f, -90.0f);
			break;
		case 3:// Z
			ForwardRotation = FRotator::ZeroRotator;
			break;
		default:// unknown
			break;
		}
		// modify the rotation based on the forward axis sign
		if(AxisForwardSign == -1)
		{
			ReturnRotation += ForwardRotation;
		}
		else
		{
			ReturnRotation -= ForwardRotation;
		}
	}

	
	return ReturnRotation;
}

FVector FAssimpMeshLoaderRunnable::TransformNormal(const FVector& InNormal, int32 AxisUpOrientation, int32 AxisForwardOrientation, int32 AxisForwardSign, int32 AxisUpSign)
{
	FMatrix TransformMatrix = FMatrix::Identity;

	// Define transformation matrix based on up and forward axes
	switch (AxisUpOrientation)
	{
	case 1: // X up
		switch (AxisForwardOrientation)
		{
		case 2: // Y forward
			TransformMatrix = FMatrix(FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), FVector::ZeroVector);
			break;
		case 3: // Z forward
			TransformMatrix = FMatrix(FVector(1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0), FVector::ZeroVector);
			break;
		}
		break;

	case 2: // Y up
		switch (AxisForwardOrientation)
		{
		case 1: // X forward
			TransformMatrix = FMatrix(FVector(0, 1, 0), FVector(1, 0, 0), FVector(0, 0, 1), FVector::ZeroVector);
			break;
		case 3: // Z forward
			TransformMatrix = FMatrix(FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 0), FVector::ZeroVector);
			break;
		}
		break;

	case 3: // Z up
		switch (AxisForwardOrientation)
		{
		case 1: // X forward
			TransformMatrix = FMatrix(FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, 1, 0), FVector::ZeroVector);
			break;
		case 2: // Y forward
			TransformMatrix = FMatrix(FVector(0, 0, 1), FVector(0, 1, 0), FVector(1, 0, 0), FVector::ZeroVector);
			break;
		}
		break;

	default:
		break;
	}

	// Invert and transpose the matrix for normal transformations
	FMatrix NormalTransformMatrix = TransformMatrix.Inverse().GetTransposed();

	// Transform the normal
	FVector TransformedNormal = NormalTransformMatrix.TransformVector(InNormal);

	// Flip directions based on signs
	if (AxisForwardSign == -1)
	{
		TransformedNormal = FRotator(0.0f, 180.0f, 0.0f).RotateVector(TransformedNormal);
	}

	TransformedNormal.X *= AxisForwardSign;
	TransformedNormal.Z *= AxisUpSign;

	// Normalize to maintain proper orientation
	return TransformedNormal.GetSafeNormal();
}
//TODO: this method needs to be refactored to use matrix transformations as it doesn't work when it comes to normals and this is where the issue is for translucent materials
void FAssimpMeshLoaderRunnable::TransformMeshMatrix(FVector& InVector, int32 AxisUpOrientation, int32 AxisUpSign,
                                                    int32 AxisForwardOrientation, int32 AxisForwardSign)
{
	

	// manipulate the vector based on the up axis and the forward axis
	switch (AxisUpOrientation)
	{
	case 1: // X up
		switch (AxisForwardOrientation)
		{
		case 1: // X
			// the up and forward axis are the same this isn't possible so assume the forward axis is unknown and use the up axis
			InVector = FVector(InVector.Z, InVector.Y, InVector.X);
			break;
		case 2: // Y
			InVector = FVector(InVector.Y, InVector.Z, InVector.X);
			break;
		case 3: // Z
			InVector = FVector(InVector.Z, InVector.Y, InVector.X);
			break;
		default: // the forward axis is unknown so use the up axis
			InVector = FVector(InVector.Z, InVector.Y, InVector.X);
			break;
		}
		break;

	case 2: // Y up
		{
			switch (AxisForwardOrientation)
			{
			case 1: // X
				InVector = FVector(InVector.X, InVector.Z, InVector.Y);
				break;
			case 2: // Y
				// the up and forward axis are the same this isn't possible so assume the forward axis is unknown and use the up axis
				InVector = FVector(InVector.X, InVector.Z, InVector.Y);
				break;
			case 3: // Z
				InVector = FVector(InVector.Z, InVector.X, InVector.Y);
				break;
			default: // the forward axis is unknown so use the up axis
				InVector = FVector(InVector.X, InVector.Z, InVector.Y);
				break;
			}
			break;
		}
	case 3: // Z up
		{
			switch (AxisForwardOrientation)
			{
			case 1: // X
				InVector = FVector(InVector.X, InVector.Y, InVector.Z);
				break;
				
			case 2: // Y
					
				InVector = FVector(InVector.Y, InVector.X, InVector.Z);
				break;

			case 3: // Z
				// the up and forward axis are the same this isn't possible so assume the forward axis is unknown and use the up axis
				InVector = FVector(InVector.X, InVector.Y, InVector.Z);
				break;
			default:
				InVector = FVector(InVector.Y, InVector.X, InVector.Z);
				break;
			}
			break;
		}
	default:
		break;
	}

	// // if the forward axis is negative then we need to rotate the vector by 180 degrees
	if(AxisForwardSign == -1)
	{
		InVector = FRotator(0.0f, 180.0f, 0.0f).RotateVector(InVector);
	}

	// multiple the in X and Z by the input sign
	InVector.X *= AxisForwardSign;
	InVector.Z *= AxisUpSign;

}

void FAssimpMeshLoaderRunnable::FillDataFromScene(const aiScene* Scene)
{
	if (!Scene || !Scene->HasMeshes())
	{
		return;
	}

	float ScaleFactor = 1.0f;
	if (Scene->mMetaData)
	{
		Scene->mMetaData->Get("UnitScaleFactor", ScaleFactor);
		if (ScaleFactor == 0.0f)
		{
			ScaleFactor = 1.0f;
		}
	}

	int32 AxisUpOrientation = 0;
	int32 AxisUpSign = 0;
	int32 AxisForwardOrientation = 0;
	int32 AxisForwardSign = 0;

	if (Scene->mMetaData)
	{
		Scene->mMetaData->Get("UpAxis", AxisUpOrientation);
		Scene->mMetaData->Get("UpAxisSign", AxisUpSign);
		Scene->mMetaData->Get("FrontAxis", AxisForwardOrientation);
		Scene->mMetaData->Get("FrontAxisSign", AxisForwardSign);
	}

	FRotator Rotation = GetMeshRotation(AxisUpOrientation, AxisUpSign, AxisForwardOrientation, AxisForwardSign);

	Vertices.Empty();
	Faces.Empty();
	Normals.Empty();

	for (uint32 MIndex = 0; MIndex < Scene->mNumMeshes; ++MIndex)
	{
		const aiMesh* Mesh = Scene->mMeshes[MIndex];
		int32 VertexBase = Vertices.Num();

		for (uint32 NumVertices = 0; NumVertices < Mesh->mNumVertices; ++NumVertices)
		{
			FVector Vertex = FVector(Mesh->mVertices[NumVertices].x * ScaleFactor,
			                         Mesh->mVertices[NumVertices].y * ScaleFactor,
			                         Mesh->mVertices[NumVertices].z * ScaleFactor);
			if (Rotation != FRotator::ZeroRotator)
			{
				TransformMeshMatrix(Vertex, AxisUpOrientation, AxisUpSign, AxisForwardOrientation, AxisForwardSign);
			}
			Vertices.Add(Vertex);

			if (Mesh->HasNormals())
			{
				FVector Normal(
					Mesh->mNormals[NumVertices].x * ScaleFactor,
					Mesh->mNormals[NumVertices].y * ScaleFactor,
					Mesh->mNormals[NumVertices].z * ScaleFactor
				);

				// Apply exactly the same rotation you used on vertices:
				if (!Rotation.IsZero())
				{
					const FQuat Q = Rotation.Quaternion();
					Normal = Q.RotateVector(Normal);
				}
				if (bIsWktExtension)
				{
					Normal *= -1.0f; // WKT normals are inverted
				}

				Normals.Add(Normal.GetSafeNormal());
			}
			else
			{
				Normals.Add(FVector::ZeroVector);
			}
		}

		for (uint32 FaceIndex = 0; FaceIndex < Mesh->mNumFaces; ++FaceIndex)
		{
			const aiFace& Face = Mesh->mFaces[FaceIndex];
			if (Face.mNumIndices == 3)
			{
				Faces.Add(VertexBase + Face.mIndices[0]);
				Faces.Add(VertexBase + Face.mIndices[1]);
				Faces.Add(VertexBase + Face.mIndices[2]);
			}
		}
	}
}
