// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "assimp/scene.h"
#include "UObject/Interface.h"
#include "AssimpInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UAssimpInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class PROJECTMOBIUS_API IAssimpInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	/**
	 * Custom Method to rotate an FVector by a specified FRotator so that the mesh is oriented correctly
	 * depending on the mesh file -- We do it as we are importing to increase performance but may require another
	 * method to rotate the stored mesh in the future as mesh import files could be large and may be better to rotate
	 * existing mesh data in the procedural mesh component
	 *
	 * @param VectorToRotate - The vector to rotate
	 * @param Rotation - The rotation to apply to the vector
	 * @return - The rotated vector
	 */
	UFUNCTION()
	virtual FVector RotateVector(FVector VectorToRotate, FRotator Rotation);
	
	/**
	 * Open a mesh file using the Assimp library
	 *
	 * @param PathToMesh - The path to the mesh file
	 * @param SectionCount - The number of sections in the mesh file
	 * @param ErrorMessageCode - The error message code if the mesh file could not be opened
	 * @return - Whether the mesh file was opened successfully
	 *  
	 */
	static bool OpenMeshFileWithAssimp(const aiScene*& Scene, const FString& PathToMesh, int32& SectionCount, FString& ErrorMessageCode);

	UFUNCTION()
	virtual bool OpenMeshFileGetWithAssimp(FString PathToMesh, int32& SectionCount, FString& ErrorMessageCode,
		TArray<FVector>& Vertices, TArray<int32>& Faces, TArray<FVector>& Normals, TArray<FVector2D>& UV,
		TArray<FVector>& Tangents, FRotator MeshRotationOffset = FRotator::ZeroRotator);

	/**
	 * Get a mesh section from the specified section index
	 *
	 * @param SectionIndex - The index of the section to get
	 * @param Vertices - The vertices of the mesh section
	 * @param Faces - The faces of the mesh section
	 * @param Normals - The normals of the mesh section
	 * @param UV - The UVs of the mesh section
	 * @param Tangents - The tangents of the mesh section
	 * @return - Whether the mesh section was retrieved successfully
	 */
	UFUNCTION()
	virtual bool GetMeshSection(int32 SectionIndex, TArray<FVector>& Vertices, TArray<int32>& Faces, TArray<FVector>& Normals, TArray<FVector2D>& UV, TArray<FVector>& Tangents);

	/** */
};
