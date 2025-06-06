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

#include "Interfaces/AssimpInterface.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/mesh.h"
#include "assimp/material.h"
#include "assimp/texture.h"
#include "assimp/postprocess.h"
#include "Kismet/KismetMathLibrary.h"


FVector IAssimpInterface::RotateVector(const FVector VectorToRotate, const FRotator Rotation)
{
	// May need to use rotate instead of unrotate -- TODO: See the difference between the two
	return Rotation.UnrotateVector(VectorToRotate);
}

// Add default functionality here for any IAssimpInterface functions that are not pure virtual.
bool IAssimpInterface::OpenMeshFileWithAssimp(const aiScene*& Scene, const FString& PathToMesh, int32& SectionCount, FString& ErrorMessageCode)
{
	// Create a new importer
	Assimp::Importer Importer;
	
	std::string Filename(TCHAR_TO_UTF8(*PathToMesh));
	
	Scene = Importer.ReadFile(Filename,aiProcess_MakeLeftHanded |aiProcess_FlipUVs |
							  aiProcess_PreTransformVertices | aiProcess_Triangulate |
							  aiProcess_GenNormals | aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices);
	// is the scene valid
	if (!Scene)
	{
		ErrorMessageCode = Importer.GetErrorString();
		return false;
	}
	// Check to see if the scene has any meshes
	if (!Scene->HasMeshes())
	{
		ErrorMessageCode = "The scene does not have any meshes";
		return false;
	}

	// Get the fbx scale unit factor
	double factor(0.0);
	Scene->mMetaData->Get("UnitScaleFactor", factor);

	// log the scale factor
	UE_LOG(LogTemp, Warning, TEXT("Scale Factor: %f"), factor);
		
	// Get the number of meshes in the scene
	int32 MeshIndex = Scene->mNumMeshes;
	UE_LOG(LogTemp, Warning, TEXT("Mesh Index: %d"), MeshIndex);
	return true;
}

bool IAssimpInterface::OpenMeshFileGetWithAssimp(const FString PathToMesh, int32& SectionCount, FString& ErrorMessageCode,
                                                 TArray<FVector>& Vertices, TArray<int32>& Faces, TArray<FVector>& Normals,
                                                 TArray<FVector2D>& UV, TArray<FVector>& Tangents, const FRotator MeshRotationOffset)
{
	// Create a new importer
	Assimp::Importer Importer;
	
	std::string Filename(TCHAR_TO_UTF8(*PathToMesh));
	
	const aiScene* Scene = Importer.ReadFile(Filename,aiProcess_MakeLeftHanded |aiProcess_FlipUVs |
							  aiProcess_PreTransformVertices | aiProcess_Triangulate |
							  aiProcess_GenNormals | aiProcess_CalcTangentSpace);
	// is the scene valid
	if (!Scene)
	{
		ErrorMessageCode = Importer.GetErrorString();
		return false;
	}
	// Check to see if the scene has any meshes
	if (!Scene->HasMeshes())
	{
		ErrorMessageCode = "The scene does not have any meshes";
		return false;
	}

	// Get the fbx scale unit factor
	float ScaleFactor(0.0);
	Scene->mMetaData->Get("UnitScaleFactor", ScaleFactor);

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
		
	// Is offset rotation not zero
	if (MeshRotationOffset != FRotator::ZeroRotator)
	{
		for (int32 MIndex = 0; MIndex < MeshIndex; MIndex++)
		{
			for (unsigned __int32 NumVertices = 0; NumVertices < Scene->mMeshes[MIndex]->mNumVertices; NumVertices
			     ++)
			{
				Vertices.Add(RotateVector(FVector(Scene->mMeshes[MIndex]->mVertices[NumVertices].x * ScaleFactor,
				                                  Scene->mMeshes[MIndex]->mVertices[NumVertices].y * ScaleFactor,
				                                  Scene->mMeshes[MIndex]->mVertices[NumVertices].z * ScaleFactor),
				                          MeshRotationOffset));
				Faces.Add(FaceIndex);
				FaceIndex++;
			}
		}
	}
		
	else
	{
		for (int32 MIndex = 0; MIndex < MeshIndex; MIndex++)
		{
			for (unsigned __int32 NumVertices = 0; NumVertices < Scene->mMeshes[MIndex]->mNumVertices; NumVertices
			     ++)
			{
				Vertices.Add(FVector(Scene->mMeshes[MIndex]->mVertices[NumVertices].x * ScaleFactor,
				                     Scene->mMeshes[MIndex]->mVertices[NumVertices].y * ScaleFactor,
				                     Scene->mMeshes[MIndex]->mVertices[NumVertices].z * ScaleFactor));
				Faces.Add(FaceIndex);
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
				
		}
	}
	{
		//UE_LOG(LogTemp, Warning, TEXT("Key: %s"), Key.C_Str());
	}
// 	Number of Properties: 18
// Warning      LogTemp                   Key: UpAxis
// Warning      LogTemp                   Key: UpAxisSign
// Warning      LogTemp                   Key: FrontAxis
// Warning      LogTemp                   Key: FrontAxisSign
// Warning      LogTemp                   Key: CoordAxis
// Warning      LogTemp                   Key: CoordAxisSign
// Warning      LogTemp                   Key: OriginalUpAxis
// Warning      LogTemp                   Key: OriginalUpAxisSign
// Warning      LogTemp                   Key: UnitScaleFactor
// Warning      LogTemp                   Key: OriginalUnitScaleFactor
// Warning      LogTemp                   Key: AmbientColor
// Warning      LogTemp                   Key: FrameRate
// Warning      LogTemp                   Key: TimeSpanStart
// Warning      LogTemp                   Key: TimeSpanStop
// Warning      LogTemp                   Key: CustomFrameRate
// Warning      LogTemp                   Key: SourceAsset_FormatVersion
// Warning      LogTemp                   Key: SourceAsset_Generator
// Warning      LogTemp                   Key: SourceAsset_Format
		
	// Faces.Add(MIndex);
	// Vertices.Add(FVector(Scene->mMeshes[MIndex]->mVertices[2].x,
	// 	Scene->mMeshes[MIndex]->mVertices[2].y,
	// 	Scene->mMeshes[MIndex]->mVertices[2].z));
	// Vertices.Add(FVector(Scene->mMeshes[MIndex]->mVertices[0].x,
	// 	Scene->mMeshes[MIndex]->mVertices[0].y,
	// 	Scene->mMeshes[MIndex]->mVertices[0].z));
	// Vertices.Add(FVector(Scene->mMeshes[MIndex]->mVertices[1].x,
	// 	Scene->mMeshes[MIndex]->mVertices[1].y,
	// 	Scene->mMeshes[MIndex]->mVertices[1].z));

	// Check to see if the scene has any materials
	// if (!Scene->HasMaterials())
	// {
	// 	ErrorMessageCode = "The scene does not have any materials";
	// 	return false; //TODO we dont want to return false apply our own materials
	// }
	// else
	// {
	// 	Materials = Scene->mMaterials;
	// }
	//
	// // check to see if the scene has any textures
	// if (!Scene->HasTextures())
	// {
	// 	ErrorMessageCode = "The scene does not have any textures";
	// 	return false; //TODO we dont want to return false apply our own texture
	// }
	// else
	// {
	// 	Textures = Scene->mTextures;
	// }
	return true;	
}

bool IAssimpInterface::GetMeshSection(int32 SectionIndex, TArray<FVector>& Vertices, TArray<int32>& Faces, TArray<FVector>& Normals, TArray<FVector2D>& UV, TArray<FVector>& Tangents)
{
	return false;
}
