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

    // ——— 2) Parse geometry ————————————————————————————————
    FWktGeometry Root;
    if (!ParseWKTData(RawWkt, Root, ErrorMessage))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse WKT: %s"), *ErrorMessage);
        return;
    }

    // ——— 3) Generate OBJ from geometry hierarchy ——————————————————————————
    WktDataString.Empty();
    int32 VertexOffset = 0;
    BuildObjFromWKTGeometry(Root, WktDataString, VertexOffset);
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

static void SplitAtTopLevel(const FString& Src, TArray<FString>& OutTokens)
{
        int32 Depth = 0;
        int32 Start = 0;
        for (int32 i = 0; i < Src.Len(); ++i)
        {
                TCHAR C = Src[i];
                if (C == '(')
                {
                        ++Depth;
                }
                else if (C == ')')
                {
                        --Depth;
                }
                else if (C == ',' && Depth == 0)
                {
                        OutTokens.Add(Src.Mid(Start, i - Start));
                        Start = i + 1;
                }
        }
        if (Start < Src.Len())
        {
                OutTokens.Add(Src.Mid(Start));
        }
}

static void ParsePointList(const FString& Src, TArray<FVector2D>& OutPoints)
{
        TArray<FString> Pairs;
        SplitAtTopLevel(Src, Pairs);
        for (FString Pair : Pairs)
        {
                Pair.ReplaceInline(TEXT("("), TEXT(""));
                Pair.ReplaceInline(TEXT(")"), TEXT(""));
                Pair.TrimStartAndEndInline();
                TArray<FString> XY;
                Pair.ParseIntoArray(XY, TEXT(" "), true);
                if (XY.Num() >= 2)
                {
                        OutPoints.Add(FVector2D(FCString::Atod(*XY[0]), FCString::Atod(*XY[1])));
                }
        }
}

static bool ParsePolygonString(const FString& Src, FPolygonWithHoles& OutPoly)
{
        FString Clean = Src;
        Clean.TrimStartAndEndInline();
        if (Clean.StartsWith(TEXT("(")) && Clean.EndsWith(TEXT(")")))
        {
                Clean = Clean.Mid(1, Clean.Len() - 2);
        }
        TArray<FString> Rings;
        SplitAtTopLevel(Clean, Rings);
        if (Rings.Num() == 0)
        {
                return false;
        }

        for (int32 i = 0; i < Rings.Num(); ++i)
        {
                TArray<FVector2D> Points;
                ParsePointList(Rings[i], Points);
                if (Points.Num() < 3)
                {
                        continue;
                }
                if (i == 0)
                {
                        OutPoly.Outer = MoveTemp(Points);
                }
                else
                {
                        OutPoly.Holes.Add(MoveTemp(Points));
                }
        }

        return OutPoly.Outer.Num() >= 3;
}

bool FAssimpMeshLoaderRunnable::ParseWKTData(const FString& InWKTDataString, FWktGeometry& OutGeometry, FString& OutErrorMessage)
{
        FString CleanWKT = InWKTDataString;
        CleanWKT.TrimStartAndEndInline();
        CleanWKT.ReplaceInline(TEXT("\r"), TEXT(""));
        CleanWKT.ReplaceInline(TEXT("\n"), TEXT(""));

        int32 OpenParenIndex;
        FString Prefix;
        FString Inner;
        if (CleanWKT.FindChar('(', OpenParenIndex))
        {
                Prefix = CleanWKT.Left(OpenParenIndex).ToUpper().TrimStartAndEnd();
                Inner = CleanWKT.Mid(OpenParenIndex + 1, CleanWKT.Len() - OpenParenIndex - 2);
        }
        else
        {
                Prefix = CleanWKT.ToUpper();
                Inner = TEXT("");
        }

        if (Prefix == TEXT("POINT"))
        {
                OutGeometry.Type = EWktGeometryType::Point;
                ParsePointList(Inner, OutGeometry.Points);
                return true;
        }
        else if (Prefix == TEXT("LINESTRING"))
        {
                OutGeometry.Type = EWktGeometryType::LineString;
                ParsePointList(Inner, OutGeometry.Points);
                return true;
        }
        else if (Prefix == TEXT("POLYGON"))
        {
                OutGeometry.Type = EWktGeometryType::Polygon;
                FPolygonWithHoles Poly;
                if (ParsePolygonString(Inner, Poly))
                {
                        OutGeometry.Polygons.Add(MoveTemp(Poly));
                        return true;
                }
        }
        else if (Prefix == TEXT("MULTIPOINT"))
        {
                OutGeometry.Type = EWktGeometryType::MultiPoint;
                ParsePointList(Inner, OutGeometry.Points);
                return true;
        }
        else if (Prefix == TEXT("MULTILINESTRING"))
        {
                OutGeometry.Type = EWktGeometryType::MultiLineString;
                TArray<FString> Parts;
                SplitAtTopLevel(Inner, Parts);
                for (auto& Part : Parts)
                {
                        TArray<FVector2D> LS;
                        ParsePointList(Part, LS);
                        if (LS.Num() > 0)
                                OutGeometry.LineStrings.Add(MoveTemp(LS));
                }
                return true;
        }
        else if (Prefix == TEXT("MULTIPOLYGON"))
        {
                OutGeometry.Type = EWktGeometryType::MultiPolygon;
                TArray<FString> Parts;
                SplitAtTopLevel(Inner, Parts);
                for (auto& Part : Parts)
                {
                        FPolygonWithHoles Poly;
                        if (ParsePolygonString(Part, Poly))
                                OutGeometry.Polygons.Add(MoveTemp(Poly));
                }
                return OutGeometry.Polygons.Num() > 0;
        }
        else if (Prefix == TEXT("GEOMETRYCOLLECTION"))
        {
                OutGeometry.Type = EWktGeometryType::GeometryCollection;
                TArray<FString> Parts;
                SplitAtTopLevel(Inner, Parts);
                for (auto& Part : Parts)
                {
                        FWktGeometry Child;
                        if (ParseWKTData(Part, Child, OutErrorMessage))
                        {
                                OutGeometry.Children.Add(MoveTemp(Child));
                        }
                }
                return OutGeometry.Children.Num() > 0;
        }

        OutErrorMessage = FString::Printf(TEXT("Unsupported WKT type: %s"), *Prefix);
        return false;
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

static void ExtrudePolygonObj(const FPolygonWithHoles& Poly, FString& OutObj, int32& VertexOffset)
{
        std::vector<std::vector<Coord>> Rings;
        Rings.reserve(1 + Poly.Holes.Num());

        Rings.emplace_back();
        for (auto& P : Poly.Outer)
                Rings[0].push_back({ double(P.X), double(P.Y) });

        for (auto& Hole : Poly.Holes)
        {
                Rings.emplace_back();
                for (auto& P : Hole)
                        Rings.back().push_back({ double(P.X), double(P.Y) });
        }

        std::vector<size_t> Indices = mapbox::earcut<size_t>(Rings);

        int32 BaseOffset = VertexOffset;
        for (auto& ring : Rings)
        {
                for (auto& c : ring)
                {
                        OutObj += FString::Printf(TEXT("v %f %f 0.0\n"), float(c[0] * 100.0), float(c[1] * 100.0));
                        ++VertexOffset;
                }
        }

        for (size_t i = 0; i + 2 < Indices.size(); i += 3)
        {
                int32 A = BaseOffset + int32(Indices[i]);
                int32 B = BaseOffset + int32(Indices[i+1]);
                int32 C = BaseOffset + int32(Indices[i+2]);
                OutObj += FString::Printf(TEXT("f %d %d %d\n"), A+1, B+1, C+1);
                OutObj += FString::Printf(TEXT("f %d %d %d\n"), A+1, C+1, B+1);
        }

        int32 TopStart = VertexOffset;
        const float Height = 100.0f;
        for (auto& ring : Rings)
        {
                for (auto& c : ring)
                {
                        OutObj += FString::Printf(TEXT("v %f %f %f\n"), float(c[0] * 100.0), float(c[1] * 100.0), Height);
                        ++VertexOffset;
                }
        }

        int32 Offset = 0;
        for (auto& ring : Rings)
        {
                int32 N = int32(ring.size());
                for (int32 i = 0; i < N; ++i)
                {
                        int32 A    = BaseOffset + Offset + i;
                        int32 B    = BaseOffset + Offset + ((i+1)%N);
                        int32 ATop = TopStart + Offset + i;
                        int32 BTop = TopStart + Offset + ((i+1)%N);

                        OutObj += FString::Printf(TEXT("f %d %d %d\n"), A+1, B+1, BTop+1);
                        OutObj += FString::Printf(TEXT("f %d %d %d\n"), A+1, BTop+1, ATop+1);
                        OutObj += FString::Printf(TEXT("f %d %d %d\n"), BTop+1, B+1, A+1);
                        OutObj += FString::Printf(TEXT("f %d %d %d\n"), ATop+1, BTop+1, A+1);
                }
                Offset += N;
        }
}

void FAssimpMeshLoaderRunnable::BuildObjFromWKTGeometry(const FWktGeometry& Geometry, FString& OutObjString, int32& VertexOffset)
{
        switch (Geometry.Type)
        {
        case EWktGeometryType::Point:
        case EWktGeometryType::MultiPoint:
                for (const FVector2D& P : Geometry.Points)
                {
                        OutObjString += FString::Printf(TEXT("v %f %f 0.0\n"), P.X * 100.0f, P.Y * 100.0f);
                        OutObjString += FString::Printf(TEXT("p %d\n"), VertexOffset + 1);
                        ++VertexOffset;
                }
                break;
        case EWktGeometryType::LineString:
                if (Geometry.Points.Num() > 0)
                {
                        int32 Start = VertexOffset;
                        for (auto& P : Geometry.Points)
                        {
                                OutObjString += FString::Printf(TEXT("v %f %f 0.0\n"), P.X*100.0f, P.Y*100.0f);
                                ++VertexOffset;
                        }
                        OutObjString += TEXT("l");
                        for (int32 i=0;i<Geometry.Points.Num();++i)
                                OutObjString += FString::Printf(TEXT(" %d"), Start + i + 1);
                        OutObjString += TEXT("\n");
                }
                break;
        case EWktGeometryType::MultiLineString:
                for (auto& LS : Geometry.LineStrings)
                {
                        FWktGeometry Tmp; Tmp.Type = EWktGeometryType::LineString; Tmp.Points = LS;
                        BuildObjFromWKTGeometry(Tmp, OutObjString, VertexOffset);
                }
                break;
        case EWktGeometryType::Polygon:
                for (auto& P : Geometry.Polygons)
                        ExtrudePolygonObj(P, OutObjString, VertexOffset);
                break;
        case EWktGeometryType::MultiPolygon:
                for (auto& P : Geometry.Polygons)
                        ExtrudePolygonObj(P, OutObjString, VertexOffset);
                break;
        case EWktGeometryType::GeometryCollection:
                for (auto& Child : Geometry.Children)
                        BuildObjFromWKTGeometry(Child, OutObjString, VertexOffset);
                break;
        }
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
