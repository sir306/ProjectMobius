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

enum class EAxisOrientation : int32
{
    Unknown = 0,
    X = 1,
    Y = 2,
    Z = 3
};

enum class EAxisSign : int32
{
    Positive = 1,
    Negative = -1
};

static FMatrix MakeAxisTransform(EAxisOrientation Up, EAxisSign UpSign, EAxisOrientation Forward, EAxisSign ForwardSign)
{
    auto BaseMatrix = [](EAxisOrientation U, EAxisOrientation F) -> FMatrix
    {
        switch (U)
        {
        case EAxisOrientation::X:
            if (F == EAxisOrientation::Y)
            {
                return FMatrix(FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 0), FVector::ZeroVector); // Y,Z,X
            }
            return FMatrix(FVector(0, 0, 1), FVector(0, 1, 0), FVector(1, 0, 0), FVector::ZeroVector); // Z,Y,X

        case EAxisOrientation::Y:
            if (F == EAxisOrientation::Z)
            {
                return FMatrix(FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, 1, 0), FVector::ZeroVector); // Z,X,Y
            }
            return FMatrix(FVector(1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0), FVector::ZeroVector); // X,Z,Y

        case EAxisOrientation::Z:
            if (F == EAxisOrientation::Y || F == EAxisOrientation::Unknown)
            {
                return FMatrix(FVector(0, 1, 0), FVector(1, 0, 0), FVector(0, 0, 1), FVector::ZeroVector); // Y,X,Z
            }
            return FMatrix(FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), FVector::ZeroVector); // X,Y,Z
        default:
            return FMatrix::Identity;
        }
    };

    FMatrix M = BaseMatrix(Up, Forward);

    if (ForwardSign == EAxisSign::Negative)
    {
        for (int32 Col = 0; Col < 4; ++Col)
        {
            M.M[0][Col] *= -1.f;
            M.M[1][Col] *= -1.f;
        }
    }

    const float FS = ForwardSign == EAxisSign::Negative ? -1.f : 1.f;
    const float US = UpSign == EAxisSign::Negative ? -1.f : 1.f;

    for (int32 Col = 0; Col < 4; ++Col)
    {
        M.M[0][Col] *= FS;
        M.M[2][Col] *= US;
    }

    return M;
}


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
        const FMatrix Transform = MakeAxisTransform(static_cast<EAxisOrientation>(AxisUpOrientation),
                                                   static_cast<EAxisSign>(AxisUpSign),
                                                   static_cast<EAxisOrientation>(AxisForwardOrientation),
                                                   static_cast<EAxisSign>(AxisForwardSign));

        return FQuat(Transform).Rotator();
}

FVector FAssimpMeshLoaderRunnable::TransformNormal(const FVector& InNormal, int32 AxisUpOrientation, int32 AxisForwardOrientation, int32 AxisForwardSign, int32 AxisUpSign)
{
        FMatrix TransformMatrix = MakeAxisTransform(static_cast<EAxisOrientation>(AxisUpOrientation),
                                                    static_cast<EAxisSign>(AxisUpSign),
                                                    static_cast<EAxisOrientation>(AxisForwardOrientation),
                                                    static_cast<EAxisSign>(AxisForwardSign));
        FMatrix NormalMatrix = TransformMatrix.InverseFast().GetTransposed();
        return NormalMatrix.TransformVector(InNormal).GetSafeNormal();
}
//TODO: this method needs to be refactored to use matrix transformations as it doesn't work when it comes to normals and this is where the issue is for translucent materials

void FAssimpMeshLoaderRunnable::TransformMeshMatrix(FVector& InVector, int32 AxisUpOrientation, int32 AxisUpSign,
                                                    int32 AxisForwardOrientation, int32 AxisForwardSign)
{
        FMatrix TransformMatrix = MakeAxisTransform(static_cast<EAxisOrientation>(AxisUpOrientation),
                                                    static_cast<EAxisSign>(AxisUpSign),
                                                    static_cast<EAxisOrientation>(AxisForwardOrientation),
                                                    static_cast<EAxisSign>(AxisForwardSign));
        InVector = TransformMatrix.TransformVector(InVector);
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
