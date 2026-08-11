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
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include <array>
#include <vector>
#include <earcut_hpp/earcut.hpp>

#include "Hdf5SimulationReader.h"
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
		FMobiusErrorMessage Payload;
		Payload.TitleBarText = FText::FromString("Mesh Load Error");
		Payload.ErrorTitle = FText::FromString("Triangulation failed");
		Payload.ErrorMessage = FText::FromString("Assimp failed to triangulate the provided polygon data.");
		Payload.ErrorLocation = FText::FromString("AsyncAssimpMeshLoader");
		UMobiusUserFeedbackSubsystem::ReportErrorFromAnyThread(TWeakObjectPtr<UObject>(), Payload);
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

void SplitSubmeshByTriCap(const FAssimpSubmeshBuffers& In, int32 MaxTris, TArray<FAssimpSubmeshBuffers>& Out)
{
	const int32 TotalTris = In.Faces.Num() / 3;
	if (TotalTris == 0)
	{
		return;
	}

	if (MaxTris <= 0 || TotalTris <= MaxTris)
	{
		Out.Add(In);
		return;
	}

	const bool bHasUV = (In.UV.Num() == In.Vertices.Num());
	const bool bHasNormals = (In.Normals.Num() == In.Vertices.Num());

	TMap<int32, int32> OldToNew;
	OldToNew.Reserve(MaxTris * 3);

	int32 TriCursor = 0;
	while (TriCursor < TotalTris)
	{
		const int32 ChunkTriCount = FMath::Min(MaxTris, TotalTris - TriCursor);

		FAssimpSubmeshBuffers& Chunk = Out.AddDefaulted_GetRef();
		// Every chunk of one submesh came from the same source entity, so the provenance is copied,
		// not split. Without this a chunked IFC product loses its GUID/class and the section->entity
		// map silently goes blank for exactly the large products most worth identifying.
		Chunk.SourceGuid = In.SourceGuid;
		Chunk.SourceIfcClass = In.SourceIfcClass;
		Chunk.SourceMaterialName = In.SourceMaterialName;
		Chunk.Material = In.Material;
		Chunk.Vertices.Reserve(ChunkTriCount * 3);
		Chunk.Faces.Reserve(ChunkTriCount * 3);
		if (bHasNormals) { Chunk.Normals.Reserve(ChunkTriCount * 3); }
		if (bHasUV)      { Chunk.UV.Reserve(ChunkTriCount * 3); }

		OldToNew.Reset();

		for (int32 T = 0; T < ChunkTriCount; ++T)
		{
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const int32 OldIdx = In.Faces[(TriCursor + T) * 3 + Corner];
				int32* Existing = OldToNew.Find(OldIdx);
				int32 NewIdx;
				if (Existing)
				{
					NewIdx = *Existing;
				}
				else
				{
					NewIdx = Chunk.Vertices.Num();
					Chunk.Vertices.Add(In.Vertices[OldIdx]);
					if (bHasNormals) { Chunk.Normals.Add(In.Normals[OldIdx]); }
					if (bHasUV)      { Chunk.UV.Add(In.UV[OldIdx]); }
					OldToNew.Add(OldIdx, NewIdx);
				}
				Chunk.Faces.Add(NewIdx);
			}
		}

		TriCursor += ChunkTriCount;
	}
}

FAssimpMeshLoaderRunnable::FAssimpMeshLoaderRunnable(const FString InPathToMesh, TWeakObjectPtr<UObject> InWorldContextObject)
	: WorldContextObject(InWorldContextObject)
{
	if(InPathToMesh.IsEmpty())
	{
		
		return;
	}
	else if(!FPaths::FileExists(InPathToMesh))
	{
		// if the path to the mesh is not a valid file path and the string is not an obj string then return
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(WorldContextObject.Get()))
		{
			Feedback->ReportError(
				FText::FromString("Mesh Load Error"),
				FText::FromString("Mesh file not found"),
				FText::FromString("The mesh file path does not exist."),
				FText::FromString("AsyncAssimpMeshLoader"));
		}
		UE_LOG(LogTemp, Warning, TEXT("The path to the mesh is not a valid file path: %s"), *InPathToMesh);
		return;
	}
	
	PathToMesh = InPathToMesh;
	// if file has .wkt extension then it is a WKT file
	bIsWktExtension = PathToMesh.EndsWith(TEXT(".wkt"), ESearchCase::IgnoreCase);
	// .ifc goes to IFC++ via the MobiusIfcBridge shim, never to Assimp (its IFCLoader is IFC2X3-only)
	bIsIfcExtension = PathToMesh.EndsWith(TEXT(".ifc"), ESearchCase::IgnoreCase);


	// Create the thread -- The thread priority is set to TPri_Normal this may need to be adjusted based on the application
	Thread = FRunnableThread::Create(this, TEXT("FAssimpMeshLoaderRunnable"), 0, TPri_Normal);
}

FAssimpMeshLoaderRunnable::~FAssimpMeshLoaderRunnable()
{
	// Block until Run() returns naturally so the stack-allocated Assimp::Importer gets its
	// destructor. Kill(true) hard-terminates mid-import and leaks Assimp-internal state,
	// which can trip subsequent HDF5 reads that reuse the same worker path.
	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

uint32 FAssimpMeshLoaderRunnable::Run()
{
	if (bIsIfcExtension)
	{
		ProcessIfcFromFile();
	}
	else if (bIsWktExtension || PathToMesh.EndsWith(".h5", ESearchCase::IgnoreCase))
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
void FAssimpMeshLoaderRunnable::ProcessIfcFromFile()
{
	// Runs on this runnable's worker thread. FMobiusIfcMeshLoader touches no UObject and no game
	// thread state; the finished buffers are marshalled back by Run()'s existing GameThread broadcast,
	// exactly as the Assimp path does.
	FString IfcError;
	if (!FMobiusIfcMeshLoader::LoadIfcFile(PathToMesh, Submeshes, IfcLoadStats, IfcError))
	{
		ErrorMessageCode = IfcError;

		// Surface it to the user the same way every other loader failure in this file does. This is a
		// one-shot, load-time report -- not a per-product or tick-path log.
		FMobiusErrorMessage Payload;
		Payload.TitleBarText = FText::FromString("Mesh Load Error");
		Payload.ErrorTitle = FText::FromString("IFC load failed");
		Payload.ErrorMessage = FText::FromString(IfcError);
		Payload.ErrorLocation = FText::FromString("AsyncAssimpMeshLoader");
		UMobiusUserFeedbackSubsystem::ReportErrorFromAnyThread(WorldContextObject, Payload);

		Submeshes.Reset();
		return;
	}

	// Mirror into the flat aggregate buffers, matching FillDataFromScene's tail so transitional
	// callers that still read Vertices/Faces/Normals see IFC geometry too.
	Vertices.Empty();
	Faces.Empty();
	Normals.Empty();
	for (const FAssimpSubmeshBuffers& Sub : Submeshes)
	{
		const int32 VertexBase = Vertices.Num();
		Vertices.Append(Sub.Vertices);
		Normals.Append(Sub.Normals);
		Faces.Reserve(Faces.Num() + Sub.Faces.Num());
		for (int32 Idx : Sub.Faces)
		{
			Faces.Add(VertexBase + Idx);
		}
	}
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
		FMobiusErrorMessage Payload;
		Payload.TitleBarText = FText::FromString("Mesh Load Error");
		Payload.ErrorTitle = FText::FromString("Triangulation failed");
		Payload.ErrorMessage = FText::FromString("Assimp failed to triangulate the provided polygon data.");
		Payload.ErrorLocation = FText::FromString("AsyncAssimpMeshLoader");
		UMobiusUserFeedbackSubsystem::ReportErrorFromAnyThread(WorldContextObject, Payload);
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
		FMobiusErrorMessage Payload;
		Payload.TitleBarText = FText::FromString("Mesh Load Error");
		Payload.ErrorTitle = FText::FromString("WKT file load failed");
		Payload.ErrorMessage = FText::FromString(ErrorMessage);
		Payload.ErrorLocation = FText::FromString("AsyncAssimpMeshLoader");
		UMobiusUserFeedbackSubsystem::ReportErrorFromAnyThread(WorldContextObject, Payload);
        UE_LOG(LogTemp, Error, TEXT("Failed to load WKT file: %s"), *ErrorMessage);
        return;
    }

    // ——— 2) Parse into polygons (outer + holes) ————————————————————————————————————————
    TArray<FPolygonWithHoles> Polygons;
    if (!ParseGeometryCollectionWkt(RawWkt, Polygons, ErrorMessage) || Polygons.Num() == 0)
    {
		FMobiusErrorMessage Payload;
		Payload.TitleBarText = FText::FromString("Mesh Load Error");
		Payload.ErrorTitle = FText::FromString("WKT parse failed");
		Payload.ErrorMessage = FText::FromString(ErrorMessage);
		Payload.ErrorLocation = FText::FromString("AsyncAssimpMeshLoader");
		UMobiusUserFeedbackSubsystem::ReportErrorFromAnyThread(WorldContextObject, Payload);
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

bool FAssimpMeshLoaderRunnable::LoadWKTFile(const FString& FilePath, FString& OutWKTData, FString& OutErrorMessage)
{
	// Check if the file exists
	if (!FPaths::FileExists(FilePath))
	{
		OutErrorMessage = FString::Printf(TEXT("File not found: %s"), *FilePath);
		return false;
	}
	// TODO: add error handling to this Code and document
	if (FilePath.EndsWith(TEXT(".h5"), ESearchCase::IgnoreCase))
	{
		FHdf5SimulationReader Reader;
		if (Reader.OpenFile(FilePath))
		{
			if (Reader.ReadWktGeometry(OutWKTData))
			{
				Reader.CloseFile();
				return true;
			}
		}
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

	const bool bIsGeometryCollection = Clean.StartsWith(TEXT("GEOMETRYCOLLECTION"), ESearchCase::IgnoreCase);
	const bool bIsPolygon = Clean.StartsWith(TEXT("POLYGON"), ESearchCase::IgnoreCase);
	if (!bIsGeometryCollection && !bIsPolygon)
	{
		OutErrorMessage = TEXT("WKT is not GEOMETRYCOLLECTION or POLYGON");
		return false;
	}

	// --- 2) strip GEOMETRYCOLLECTION(   ) when present
	FString inner = Clean;
	if (bIsGeometryCollection)
	{
		int32 firstParen = Clean.Find(TEXT("("));
		int32 lastParen  = INDEX_NONE;
		Clean.FindLastChar(')', lastParen);
		if (firstParen == INDEX_NONE || lastParen == INDEX_NONE || lastParen <= firstParen)
		{
			OutErrorMessage = TEXT("Malformed GEOMETRYCOLLECTION parentheses");
			return false;
		}
		inner = Clean.Mid(firstParen + 1, lastParen - firstParen - 1);
	}

	// --- 3) find the first POLYGON(( ... ))
	int32 polyStart = inner.Find(TEXT("POLYGON"), ESearchCase::IgnoreCase);
	if (polyStart == INDEX_NONE)
	{
		OutErrorMessage = TEXT("No POLYGON found in WKT");
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


FRotator FAssimpMeshLoaderRunnable::GetMeshRotation(EAxisOrientation AxisUpOrientation, EAxisSign AxisUpSign,
                                                    EAxisOrientation AxisForwardOrientation, EAxisSign AxisForwardSign)
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
	
        int32 AxisOrientationPicker = (static_cast<int32>(AxisUpOrientation) == -1)
                                            ? 0
                                            : static_cast<int32>(AxisUpOrientation);
        int32 AxisUpSignPicker = (static_cast<int32>(AxisUpSign) == -1)
                                        ? 2
                                        : static_cast<int32>(AxisUpSign);

	// make rotator based on the up axis orientation and sign
	FRotator ReturnRotation = UpRotation[AxisOrientationPicker][AxisUpSignPicker];
	
	// modify the rotation based on the forward axis orientation and sign
        if(AxisForwardOrientation != EAxisOrientation::Unknown)
        {
		// modify the rotation based on the forward axis orientation and sign
		FRotator ForwardRotation = FRotator::ZeroRotator;
		switch (AxisForwardOrientation)
		{
                case EAxisOrientation::X:// X
                        switch (AxisUpOrientation)
                        {
                        case EAxisOrientation::X:
				// the up and forward axis are the same this isn't possible so assume the forward axis is unknown and use the up axis
				break;
                        case EAxisOrientation::Y: // Y
                                if(AxisUpSign == EAxisSign::Negative)
				{
					ForwardRotation = FRotator(0.0f, -90.0f, 0.0f);
				}
				else
				{
					ForwardRotation = FRotator(0.0f, 90.0f, 0.0f);
				}
				break;
                        case EAxisOrientation::Z: // Z
                                if(AxisUpSign == EAxisSign::Negative)
				{
					ForwardRotation = FRotator(0.0f, 0.0f, 90.0f);
				}
				else
				{
					ForwardRotation = FRotator(0.0f, 0.0f, -90.0f);
				}
				break;
                        default: // the up axis is unknown so use the forward axis
                                if(AxisForwardSign == EAxisSign::Negative)
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
                case EAxisOrientation::Y:// Y
			ForwardRotation = FRotator(0.0f, 0.0f, -90.0f);
			break;
                case EAxisOrientation::Z:// Z
			ForwardRotation = FRotator::ZeroRotator;
			break;
		default:// unknown
			break;
		}
		// modify the rotation based on the forward axis sign
                if(AxisForwardSign == EAxisSign::Negative)
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

FVector FAssimpMeshLoaderRunnable::TransformNormal(const FVector& InNormal, EAxisOrientation AxisUpOrientation, EAxisOrientation AxisForwardOrientation, EAxisSign AxisForwardSign, EAxisSign AxisUpSign)
{
	FMatrix TransformMatrix = FMatrix::Identity;

	// Define transformation matrix based on up and forward axes
        switch (AxisUpOrientation)
        {
        case EAxisOrientation::X: // X up
                switch (AxisForwardOrientation)
                {
                case EAxisOrientation::Y: // Y forward
                        TransformMatrix = FMatrix(FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), FVector::ZeroVector);
                        break;
                case EAxisOrientation::Z: // Z forward
                        TransformMatrix = FMatrix(FVector(1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0), FVector::ZeroVector);
                        break;
                }
                break;

        case EAxisOrientation::Y: // Y up
                switch (AxisForwardOrientation)
                {
                case EAxisOrientation::X: // X forward
                        TransformMatrix = FMatrix(FVector(0, 1, 0), FVector(1, 0, 0), FVector(0, 0, 1), FVector::ZeroVector);
                        break;
                case EAxisOrientation::Z: // Z forward
                        TransformMatrix = FMatrix(FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 0), FVector::ZeroVector);
                        break;
                }
                break;

        case EAxisOrientation::Z: // Z up
                switch (AxisForwardOrientation)
                {
                case EAxisOrientation::X: // X forward
                        TransformMatrix = FMatrix(FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, 1, 0), FVector::ZeroVector);
                        break;
                case EAxisOrientation::Y: // Y forward
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
        if (AxisForwardSign == EAxisSign::Negative)
        {
                TransformedNormal = FRotator(0.0f, 180.0f, 0.0f).RotateVector(TransformedNormal);
        }

        TransformedNormal.X *= static_cast<int32>(AxisForwardSign);
        TransformedNormal.Z *= static_cast<int32>(AxisUpSign);

	// Normalize to maintain proper orientation
	return TransformedNormal.GetSafeNormal();
}
//TODO: this method needs to be refactored to use matrix transformations as it doesn't work when it comes to normals and this is where the issue is for translucent materials
void FAssimpMeshLoaderRunnable::TransformMeshMatrix(FVector& InVector, EAxisOrientation AxisUpOrientation, EAxisSign AxisUpSign,
                                                    EAxisOrientation AxisForwardOrientation, EAxisSign AxisForwardSign)
{
	

	// manipulate the vector based on the up axis and the forward axis
        switch (AxisUpOrientation)
        {
        case EAxisOrientation::X: // X up
                switch (AxisForwardOrientation)
                {
                case EAxisOrientation::X: // X
                        // the up and forward axis are the same this isn't possible so assume the forward axis is unknown and use the up axis
                        InVector = FVector(InVector.Z, InVector.Y, InVector.X);
                        break;
                case EAxisOrientation::Y: // Y
                        InVector = FVector(InVector.Y, InVector.Z, InVector.X);
                        break;
                case EAxisOrientation::Z: // Z
                        InVector = FVector(InVector.Z, InVector.Y, InVector.X);
                        break;
                default: // the forward axis is unknown so use the up axis
                        InVector = FVector(InVector.Z, InVector.Y, InVector.X);
                        break;
                }
                break;

        case EAxisOrientation::Y: // Y up
                {
                        switch (AxisForwardOrientation)
                        {
                        case EAxisOrientation::X: // X
                                InVector = FVector(InVector.X, InVector.Z, InVector.Y);
                                break;
                        case EAxisOrientation::Y: // Y
                                // the up and forward axis are the same this isn't possible so assume the forward axis is unknown and use the up axis
                                InVector = FVector(InVector.X, InVector.Z, InVector.Y);
                                break;
                        case EAxisOrientation::Z: // Z
                                InVector = FVector(InVector.Z, InVector.X, InVector.Y);
                                break;
                        default: // the forward axis is unknown so use the up axis
                                InVector = FVector(InVector.X, InVector.Z, InVector.Y);
                                break;
                        }
                        break;
                }
        case EAxisOrientation::Z: // Z up
                {
                        switch (AxisForwardOrientation)
                        {
                        case EAxisOrientation::X: // X
                                InVector = FVector(InVector.X, InVector.Y, InVector.Z);
                                break;

                        case EAxisOrientation::Y: // Y

                                InVector = FVector(InVector.Y, InVector.X, InVector.Z);
                                break;

                        case EAxisOrientation::Z: // Z
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
        if(AxisForwardSign == EAxisSign::Negative)
        {
                InVector = FRotator(0.0f, 180.0f, 0.0f).RotateVector(InVector);
        }

	// multiple the in X and Z by the input sign
        InVector.X *= static_cast<int32>(AxisForwardSign);
        InVector.Z *= static_cast<int32>(AxisUpSign);

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

        int32 AxisUpOrientationInt = 0;
        int32 AxisUpSignInt = 0;
        int32 AxisForwardOrientationInt = 0;
        int32 AxisForwardSignInt = 0;

	if (Scene->mMetaData)
	{
                Scene->mMetaData->Get("UpAxis", AxisUpOrientationInt);
                Scene->mMetaData->Get("UpAxisSign", AxisUpSignInt);
                Scene->mMetaData->Get("FrontAxis", AxisForwardOrientationInt);
                Scene->mMetaData->Get("FrontAxisSign", AxisForwardSignInt);
        }

        const EAxisOrientation AxisUpOrientation = static_cast<EAxisOrientation>(AxisUpOrientationInt);
        const EAxisSign AxisUpSign = static_cast<EAxisSign>(AxisUpSignInt);
        const EAxisOrientation AxisForwardOrientation = static_cast<EAxisOrientation>(AxisForwardOrientationInt);
        const EAxisSign AxisForwardSign = static_cast<EAxisSign>(AxisForwardSignInt);

	FRotator Rotation = GetMeshRotation(AxisUpOrientation, AxisUpSign, AxisForwardOrientation, AxisForwardSign);

	Submeshes.Reset();
	Submeshes.Reserve(Scene->mNumMeshes);

	Vertices.Empty();
	Faces.Empty();
	Normals.Empty();

	for (uint32 MIndex = 0; MIndex < Scene->mNumMeshes; ++MIndex)
	{
		const aiMesh* Mesh = Scene->mMeshes[MIndex];
		FAssimpSubmeshBuffers& Sub = Submeshes.AddDefaulted_GetRef();

		// ---- Source material for fbx/obj ------------------------------------------------------
		// Until 2026-08-12 this loader ignored mMaterialIndex entirely, so every fbx and obj rendered
		// in one flat colour no matter what the file said. One aiMesh has exactly one material, so the
		// mapping onto FAssimpSubmeshBuffers is 1:1 with no splitting -- unlike IFC, where styles attach
		// per geometric item and a product has to be split by appearance.
		//
		// aiProcess_PreTransformVertices (used above) can MERGE meshes that share a material, which is
		// harmless here and in fact helps: fewer, larger submeshes with one material each.
		if (Scene->mMaterials && Mesh->mMaterialIndex < Scene->mNumMaterials)
		{
			if (const aiMaterial* Material = Scene->mMaterials[Mesh->mMaterialIndex])
			{
				aiString MaterialName;
				if (Material->Get(AI_MATKEY_NAME, MaterialName) == AI_SUCCESS)
				{
					Sub.SourceMaterialName = UTF8_TO_TCHAR(MaterialName.C_Str());
					Sub.Material.Name = Sub.SourceMaterialName;
				}

				// Diffuse is the channel every format in use here actually carries (obj's Kd, fbx's
				// DiffuseColor). BASE_COLOR is deliberately not consulted as a fallback: assimp only
				// populates it for genuinely PBR sources, and quietly mixing the two would make the
				// colour's provenance unclear when it looks wrong.
				aiColor4D Diffuse;
				if (Material->Get(AI_MATKEY_COLOR_DIFFUSE, Diffuse) == AI_SUCCESS)
				{
					float Opacity = 1.0f;
					if (Material->Get(AI_MATKEY_OPACITY, Opacity) != AI_SUCCESS)
					{
						// No explicit opacity: fall back to the diffuse alpha, which obj/fbx sometimes
						// carry instead, and treat a zero as fully opaque rather than invisible -- an
						// unset alpha reading 0 would otherwise erase the mesh.
						Opacity = (Diffuse.a > 0.0f) ? Diffuse.a : 1.0f;
					}

					Sub.Material.BaseColour = FLinearColor(Diffuse.r, Diffuse.g, Diffuse.b, Opacity);
					Sub.Material.bHasMaterial = true;
				}

				float Shininess = 0.0f;
				if (Material->Get(AI_MATKEY_SHININESS, Shininess) == AI_SUCCESS)
				{
					Sub.Material.SpecularExponent = Shininess;
				}
			}
		}
		Sub.Vertices.Reserve(Mesh->mNumVertices);
		Sub.Normals.Reserve(Mesh->mNumVertices);
		Sub.Faces.Reserve(Mesh->mNumFaces * 3);

		for (uint32 NumVertices = 0; NumVertices < Mesh->mNumVertices; ++NumVertices)
		{
			FVector Vertex = FVector(Mesh->mVertices[NumVertices].x * ScaleFactor,
			                         Mesh->mVertices[NumVertices].y * ScaleFactor,
			                         Mesh->mVertices[NumVertices].z * ScaleFactor);
			if (Rotation != FRotator::ZeroRotator)
			{
				TransformMeshMatrix(Vertex, AxisUpOrientation, AxisUpSign, AxisForwardOrientation, AxisForwardSign);
			}
			Sub.Vertices.Add(Vertex);

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

				Sub.Normals.Add(Normal.GetSafeNormal());
			}
			else
			{
				Sub.Normals.Add(FVector::ZeroVector);
			}
		}

		for (uint32 FaceIndex = 0; FaceIndex < Mesh->mNumFaces; ++FaceIndex)
		{
			const aiFace& Face = Mesh->mFaces[FaceIndex];
			if (Face.mNumIndices == 3)
			{
				Sub.Faces.Add(static_cast<int32>(Face.mIndices[0]));
				Sub.Faces.Add(static_cast<int32>(Face.mIndices[1]));
				Sub.Faces.Add(static_cast<int32>(Face.mIndices[2]));
			}
		}

		// Mirror into flat aggregate buffers for transitional callers still consuming monolithic data.
		const int32 VertexBase = Vertices.Num();
		Vertices.Append(Sub.Vertices);
		Normals.Append(Sub.Normals);
		Faces.Reserve(Faces.Num() + Sub.Faces.Num());
		for (int32 Idx : Sub.Faces)
		{
			Faces.Add(VertexBase + Idx);
		}
	}
}
