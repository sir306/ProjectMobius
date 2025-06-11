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
	// Broadcast the current percentage of the data loaded
	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		// Broadcast the current percentage of the data loaded as 0 this way the ui will show
		
	});
	
	// ensure the tread is not stopped
	bShouldStop = false;
	
	// Create a new importer
	Assimp::Importer Importer;

	// Create the filename for assimp
	const std::string Filename(TCHAR_TO_UTF8(*PathToMesh));

	// Read the file
	const aiScene* Scene = Importer.ReadFile(Filename,aiProcess_MakeLeftHanded |aiProcess_FlipUVs |
	                                         aiProcess_PreTransformVertices | aiProcess_Triangulate |
	                                         aiProcess_GenNormals | aiProcess_CalcTangentSpace);
	while (!bShouldStop)
	{
		// is the scene valid
		if (!Scene)
		{
			ErrorMessageCode = Importer.GetErrorString();
			bShouldStop = true;
		}
		// Check to see if the scene has any meshes
		if (!Scene->HasMeshes())
		{
			ErrorMessageCode = "The scene does not have any meshes";
			bShouldStop = true;
		}

		// Get the fbx scale unit factor
		float ScaleFactor(0.0);
		Scene->mMetaData->Get("UnitScaleFactor", ScaleFactor);

		// log scale factor
		UE_LOG(LogTemp, Warning, TEXT("Scale Factor: %f"), ScaleFactor);

		int numProps = Scene->mMetaData->mNumProperties;

		FString bGreaterThanZero = ScaleFactor > 0.0 ? FString("True") : FString("False");

		// log the scale factor and if it is greater than 0
		UE_LOG(LogTemp, Warning, TEXT("Scale Factor: %f, Greater Than Zero: %s"), ScaleFactor, *bGreaterThanZero);

		
		// if a scale factor is 0 then set it to 1
		if (ScaleFactor == 0.0)
		{
			ScaleFactor = 1.0;
		}
	
		// Get the number of meshes in the scene
		int32 MeshIndex = Scene->mNumMeshes;
		UE_LOG(LogTemp, Warning, TEXT("Mesh Index: %d"), MeshIndex);
		
		int32 FaceIndex = 0;

		// Warning      LogTemp                   Key: UpAxis
		// Warning      LogTemp                   Key: UpAxisSign
		// Warning      LogTemp                   Key: FrontAxis
		/* TODO: Need to work out the rotation for the forward based on the found up, currently only up is being used */
			
		int32 AxisUpOrientation = 0;
		int32 AxisUpSign = 0;
		int32 AxisForwardOrientation = 0;
		int32 AxisForwardSign = 0;

		// get the up axis orientation
		Scene->mMetaData->Get("UpAxis", AxisUpOrientation);
		// get the up axis sign
		Scene->mMetaData->Get("UpAxisSign", AxisUpSign);
		// get the forward axis orientation
		Scene->mMetaData->Get("FrontAxis", AxisForwardOrientation);
		// get the forward axis sign
		Scene->mMetaData->Get("FrontAxisSign", AxisForwardSign);
		
		FRotator Rotation = GetMeshRotation(AxisUpOrientation, AxisUpSign, AxisForwardOrientation, AxisForwardSign);

		
		
		// to avoid constant if rotations the Rotation variable is checked first and the corresponding loop is run
		if(Rotation == FRotator::ZeroRotator)
		{
			// No rotation is needed
			for (int32 MIndex = 0; MIndex < MeshIndex; MIndex++)
			{
				for (unsigned __int32 NumVertices = 0; NumVertices < Scene->mMeshes[MIndex]->mNumVertices; NumVertices
				     ++)
				{
					Vertices.Add(FVector(Scene->mMeshes[MIndex]->mVertices[NumVertices].x * ScaleFactor,
					                     Scene->mMeshes[MIndex]->mVertices[NumVertices].y * ScaleFactor,
					                     Scene->mMeshes[MIndex]->mVertices[NumVertices].z * ScaleFactor));

					// add face index for the runtime mesh builder
					Faces.Add(FaceIndex);

					//Scene->mMeshes[MIndex]->mTangents
					//Scene->mMeshes[MIndex]->mTextureCoords
					//Scene->mMeshes[MIndex]->mNumUVComponents
					

					// get the normals for this face
					if(Scene->mMeshes[MIndex]->HasNormals())
					{
						Normals.Add(FVector(Scene->mMeshes[MIndex]->mNormals[NumVertices].x,
						                    Scene->mMeshes[MIndex]->mNormals[NumVertices].y,
						                    Scene->mMeshes[MIndex]->mNormals[NumVertices].z));
					}
					else
					{
						// add a zero vector
						Normals.Add(FVector::ZeroVector);
					}
					
					FaceIndex++;
				}
			}
		}
		else
		{
			// Rotation is needed
			for (int32 MIndex = 0; MIndex < MeshIndex; MIndex++)
			{
				for (unsigned __int32 NumVertices = 0; NumVertices < Scene->mMeshes[MIndex]->mNumVertices; NumVertices
				     ++)
				{
					FVector MeshVertice = FVector(Scene->mMeshes[MIndex]->mVertices[NumVertices].x * ScaleFactor,
															   Scene->mMeshes[MIndex]->mVertices[NumVertices].y * ScaleFactor,
															   Scene->mMeshes[MIndex]->mVertices[NumVertices].z * ScaleFactor);
					TransformMeshMatrix(MeshVertice, AxisUpOrientation, AxisUpSign, AxisForwardOrientation, AxisForwardSign);
					Vertices.Add(MeshVertice);
					
					// add face index for the runtime mesh builder
					Faces.Add(FaceIndex);

					// get the normals for this face
					if(Scene->mMeshes[MIndex]->HasNormals())
					{
						// we have to transform the normals as well as the vertices
						FVector MeshNormal = FVector(Scene->mMeshes[MIndex]->mNormals[NumVertices].x * ScaleFactor,
														Scene->mMeshes[MIndex]->mNormals[NumVertices].y * ScaleFactor,
														Scene->mMeshes[MIndex]->mNormals[NumVertices].z * ScaleFactor);
						
						TransformMeshMatrix(MeshNormal, AxisUpOrientation, AxisUpSign, AxisForwardOrientation, AxisForwardSign);

						Normals.Add(MeshNormal);
					}
					else
					{
						// add a zero vector
						Normals.Add(FVector::ZeroVector);
					}
					
					FaceIndex++;
				}
			}
		}

		

		
	
		// Check to see if the scene has any materials
		if (!Scene->HasMaterials())
		{
			ErrorMessageCode = "The scene does not have any materials";
			UE_LOG(LogTemp, Warning, TEXT("The scene does not have any materials"));
		}
		else
		{
			//Materials = Scene->mMaterials;
			UE_LOG(LogTemp, Warning, TEXT("The scene has materials, Number of Materials: %d"), Scene->mNumMaterials);
		}

		

		// log the number of properties
		UE_LOG(LogTemp, Warning, TEXT("Number of Properties: %d"), numProps);
		

		// Debugging the metadata see what we can get
		for (int i = 0; i < numProps; i++)
		{
			const aiString* Key;
			const aiMetadataEntry* Entry;
			bool result = Scene->mMetaData->Get(size_t(i), Key, Entry);
			//UE_LOG(LogTemp, Warning, TEXT("Key: %s"), Key.C_Str());
			if(result)
			{
				FString KeyString = FString(Key->C_Str());
				UE_LOG(LogTemp, Warning, TEXT("Key: %s"), *KeyString);

				// get the type of the entry
				aiMetadataType Type = Entry->mType;

				if(Type == AI_AISTRING) UE_LOG(LogTemp, Warning, TEXT("Type: AI_AISTRING"));
				if(Type == AI_AIVECTOR3D) UE_LOG(LogTemp, Warning, TEXT("Type: AI_AIVECTOR3D"));
				if(Type == AI_AIMETADATA) UE_LOG(LogTemp, Warning, TEXT("Type: AI_AIMETADATA"));
				if(Type == AI_INT32)
				{
					// get the int32 value
					int32 Value;
					Scene->mMetaData->Get(*Key, Value);
					UE_LOG(LogTemp, Warning, TEXT("Type: AI_INT32, Value: %d"), Value);
					
				}

				// log the type
				UE_LOG(LogTemp, Warning, TEXT("Type: %d"), Type);
			}
		}
		break;// break the loop as we only need to load the mesh data once - but this will change in the future
	}
}

void FAssimpMeshLoaderRunnable::ProcessMeshFromString()
{
	LoadWKTDataToObjString();
	std::string OBJData = TCHAR_TO_UTF8(*WktDataString);
	Assimp::Importer Importer;
	const aiScene* Scene = Importer.ReadFileFromMemory(
		OBJData.c_str(), OBJData.size(),
		aiProcess_Triangulate | aiProcess_JoinIdenticalVertices,
		"obj");

	if (!Scene || !Scene->HasMeshes())
	{
		UE_LOG(LogTemp, Error, TEXT("Assimp failed to triangulate: %s"), UTF8_TO_TCHAR(Importer.GetErrorString()));
		return;
	}

	const aiMesh* Mesh = Scene->mMeshes[0];
	Vertices.Empty();
	for (unsigned int i = 0; i < Mesh->mNumVertices; ++i)
	{
		const aiVector3D& V = Mesh->mVertices[i];
		Vertices.Add(FVector(V.x, V.y, V.z));
	}

	for (unsigned int i = 0; i < Mesh->mNumFaces; ++i)
	{
		const aiFace& Face = Mesh->mFaces[i];
		if (Face.mNumIndices == 3)
		{
			Faces.Add(Face.mIndices[0]);
			Faces.Add(Face.mIndices[1]);
			Faces.Add(Face.mIndices[2]);
		}
	}
}

void FAssimpMeshLoaderRunnable::LoadWKTDataToObjString()
{
	// now we can use the WKTParser to parse WKT data from a string
	FString OutFileData;
	

	if (LoadWKTFile(PathToMesh, OutFileData, ErrorMessage))
	{
		// if the outfiledata contains geometry collections use correct parser
		TArray<TArray<FVector2D>> Geometries;
		if (ParseGeometryCollectionWkt(OutFileData, Geometries, ErrorMessage))
		{
			for (const TArray<FVector2D>& Geometry : Geometries)
			{
				for (const FVector2D& P : Geometry)
				{
					UE_LOG(LogTemp, Log, TEXT("Parsed Geometry Point: X=%f, Y=%f"), P.X, P.Y);
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to parse geometry collection: %s"), *ErrorMessage);
		}

		// loop through the parsed WKT data
		for (TArray<FVector2D>& Geometry : Geometries)
		{
			TArray<FVector> WorldPoints;
			for (const FVector2D& P : Geometry)
			{
				WorldPoints.Add(FVector(P.X, P.Y, 10.f)); // raise above first file data
			}
			
			// Test the assimp triangulation method
			TArray<FVector> TriVerts;
			TArray<FIntVector> Triangles = UAsyncAssimpMeshLoader::TriangulateWktPolygon(Geometry, TriVerts);


			// Generate OBJ with double-sided wall extrusion (1 meter = 100 units)
			WktDataString = TEXT("o WKTPolygonWithWalls\n");

			int32 NumPoints = Geometry.Num();
			TArray<FVector2D> Scaled = Geometry;
			for (FVector2D& P : Scaled)
				P *= 100.0f;

			// Step 1: Add base (Z=0) and top (Z=100) vertices
			for (const FVector2D& P : Scaled)
			{
				WktDataString += FString::Printf(TEXT("v %f %f 0.0\n"), P.X, P.Y);       // base ring
			}
			for (const FVector2D& P : Scaled)
			{
				WktDataString += FString::Printf(TEXT("v %f %f 100.0\n"), P.X, P.Y);     // top ring
			}

			// Step 2: Add bottom face (original polygon winding)
			WktDataString += TEXT("f");
			for (int32 i = 1; i <= NumPoints; ++i)
			{
				WktDataString += FString::Printf(TEXT(" %d"), i);
			}
			WktDataString += TEXT("\n");

			// Step 3: Add top face (reverse winding for top)
			WktDataString += TEXT("f");
			for (int32 i = NumPoints; i > 0; --i)
			{
				WktDataString += FString::Printf(TEXT(" %d"), i + NumPoints);
			}
			WktDataString += TEXT("\n");

			// Step 4: Add side wall faces (double-sided)
			for (int32 i = 0; i < NumPoints; ++i)
			{
				int32 A = i + 1;
				int32 B = ((i + 1) % NumPoints) + 1;
				int32 ATop = A + NumPoints;
				int32 BTop = B + NumPoints;

				// Outer face (clockwise)
				WktDataString += FString::Printf(TEXT("f %d %d %d\n"), A, B, BTop);
				WktDataString += FString::Printf(TEXT("f %d %d %d\n"), A, BTop, ATop);

				// Inner face (reversed winding)
				WktDataString += FString::Printf(TEXT("f %d %d %d\n"), BTop, B, A);
				WktDataString += FString::Printf(TEXT("f %d %d %d\n"), ATop, BTop, A);
			}
		}
	}
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

bool FAssimpMeshLoaderRunnable::ParseGeometryCollectionWkt(const FString& WKTString,
	TArray<TArray<FVector2D>>& OutGeometries, FString& OutErrorMessage)
{
	FString CleanWKT = WKTString;
	CleanWKT.TrimStartAndEndInline();
	CleanWKT = CleanWKT.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(""));

	if (!CleanWKT.StartsWith(TEXT("GEOMETRYCOLLECTION"), ESearchCase::IgnoreCase))
	{
		OutErrorMessage = TEXT("WKT does not begin with GEOMETRYCOLLECTION");
		return false;
	}

	// Extract the content inside the GEOMETRYCOLLECTION (...)
	int32 OpenParen = CleanWKT.Find(TEXT("("));
	int32 CloseParen = INDEX_NONE;
	if (OpenParen == INDEX_NONE || !CleanWKT.FindLastChar(')', CloseParen) || CloseParen <= OpenParen)
	{
		OutErrorMessage = TEXT("Malformed GEOMETRYCOLLECTION WKT.");
		return false;
	}

	FString Inner = CleanWKT.Mid(OpenParen + 1, CloseParen - OpenParen - 1).TrimStartAndEnd();

	// Look for each POLYGON block
	int32 Pos = 0;
	while (true)
	{
		int32 PolygonStart = Inner.Find(TEXT("POLYGON"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Pos);
		if (PolygonStart == INDEX_NONE) break;

		int32 FirstParen = Inner.Find(TEXT("(("), ESearchCase::IgnoreCase, ESearchDir::FromStart, PolygonStart);
		int32 EndParen = Inner.Find(TEXT("))"), ESearchCase::IgnoreCase, ESearchDir::FromStart, FirstParen + 2);
		if (FirstParen == INDEX_NONE || EndParen == INDEX_NONE) break;

		FString PolygonBlock = Inner.Mid(FirstParen + 2, EndParen - FirstParen - 2);

		FString DummyError;
		TArray<FVector2D> PolygonPoints = ParseWKTData(TEXT("LINESTRING(") + PolygonBlock + TEXT(")"), DummyError);
		if (!PolygonPoints.IsEmpty())
		{
			OutGeometries.Add(PolygonPoints);
		}

		Pos = EndParen + 2;
	}

	if (OutGeometries.Num() == 0)
	{
		OutErrorMessage = TEXT("No valid POLYGON found in GEOMETRYCOLLECTION.");
		return false;
	}

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
