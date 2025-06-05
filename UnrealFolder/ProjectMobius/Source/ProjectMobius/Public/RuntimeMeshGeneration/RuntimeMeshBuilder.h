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
#include "Interfaces/AssimpInterface.h"
#include "Interfaces/ProjectMobiusInterface.h"
#include "RuntimeMeshBuilder.generated.h"

/** Delegates */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMeshBuilt, FVector, BoundOrigins, FVector, BoundExtents);

/** Structs */
USTRUCT()
struct FDatasmithMaterials
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> MeshMaterials;

	UPROPERTY()
	bool bIsOpaque = false;
};

UCLASS()
class PROJECTMOBIUS_API ARuntimeMeshBuilder : public AActor, public IAssimpInterface, public IProjectMobiusInterface
{
	GENERATED_BODY()
	
public:	
#pragma region PUBLIC_METHODS
	// Sets default values for this actor's properties
	ARuntimeMeshBuilder();

	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called every frame
	virtual void Tick(float DeltaTime) override;

	virtual void OnConstruction(const FTransform& Transform) override;

	/**
	* Function to generate the Mobius Runtime Mesh from the given vertices, triangles, and normals
	* 
	* @param InVertices The Vertices to generate the mesh from
	* @param InTriangles The Triangles to generate the mesh from
	* @param InNormals The Normals to generate the mesh from
	* 
	*/
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Generation")
	void GenerateMobiusMesh(TArray<FVector> InVertices, TArray<int32> InTriangles, TArray<FVector> InNormals);

	/**
	 * Function to get the Mesh Data via the Assimp Interface
	 * 
	 * @param MeshRotationOffset Different Modeling software have different coordinate systems, and may require rotation
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Generation")
	void GetMeshDataFromFile(FRotator MeshRotationOffset = FRotator::ZeroRotator);
	
	/**
	 * Update the Mesh file name, this is bound to the OnMeshFileChanged Delegate in the Game Instance
	 * and will call the methods to get the mesh data and rebuild the mesh
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|UpdateMethods")
	void UpdateMeshFileName();

	/**
	 * Function to update the Mesh Data via the Async Assimp 
	 * 
	 * @param PathToMesh The Path to the Mesh to load
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|UpdateMethods")
	void AsyncUpdateMesh(const FString PathToMesh);

	/**
	 * Method to bind to the OnLoadMeshDataComplete Delegate so the generator can get the mesh data
	 * from the runnable thread
	 */
	UFUNCTION()
	void GetTheAsyncMeshData();

	/**
	 * Method to Update the material on the mesh with the given material, this is called after the mesh has been generated
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void UpdateMeshMaterial(UMaterialInstanceDynamic* InMaterial);

	// Datasmith change material methods
	/**
	 * Set the Datasmith mesh materials on the mesh to use the non modified material setting
	 *
	 * @param[bool] bUseNonModifiedMaterials if true the mesh will use the non modified materials, default is true
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithMeshToUseNonModifiedMaterials(bool bUseNonModifiedMaterials = true);


	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithMeshToTranslucentMaterials();

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithMeshToSolidMaterials();

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void UpdateDatasmithMeshOpacity(float Opacity = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void BoxDissolveDatasmithMesh(bool bDissolve = true);

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithToUseModifiedColour(bool bUseModifiedColour = true, FLinearColor NewColour = FLinearColor::White);

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithMeshToUseClearCoatMaterials(bool bUseClearCoatMaterials = true);

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithDissolveMeshSizeAndOrigin(FVector Origin, FVector Extents);

	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	void SetDatasmithToOriginalMatStyle();


#pragma endregion PUBLIC_METHODS

#pragma region PRIVATE_METHODS
private:
	/** Internal method to set the material on the mesh - TODO: this will need to be converted to be called via a delegate  */
	void SetMaterialOnMesh();

	/** Internal Method that creates and maps datasmith materials */
	void CreateDatasmithMaterials();

	TArray<TObjectPtr<UMaterialInstanceDynamic>> CreateMaterialInstances(UMaterialInterface* InMaterial, const FString& MaterialPath);

	/** Internal Method to handle Opaque Material creation of datasmith materials */
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CreateOpaqueMaterials(UMaterialInterface* InMaterial);
	
	/** Internal Method to handle Translucent Material creation of datasmith materials */
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CreateTranslucentMaterials(UMaterialInterface* InMaterial, bool bIsOpaque = false);
	
#pragma endregion PRIVATE_METHODS

#pragma region PUBLIC_PROPERTIES_AND_COMPONENTS
public:
	/** The Procedural Mesh Component used for generating meshes at runtime or on construction */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Component")
	class UProceduralMeshComponent* MobiusProceduralMeshComponent;

	/** Holds the filename to the mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|MeshData")
	FString MeshFileName = "C:\\Users\\User_VR4\\Desktop\\WORK\\ProjectMobius\\ProjectMobius\\TestData\\TechnicalSchool1000People\\Technical-School-For-Lab-3D.fbx";

	/** Async Loader for assimp */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshGenerator|AsyncLoader")
	TObjectPtr<class UAsyncAssimpMeshLoader> AsyncAssimpLoader = nullptr;

	/** To avoid material setting on mesh being generated we set this bool flag */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Material")
	bool bMeshBeingBuilt = false;

	/** Delegate we use to broadcast when the model has been built */
	UPROPERTY(EditAnywhere, BlueprintAssignable, Category = "MeshGenerator|Delegates")
	FOnMeshBuilt OnMeshBuilt;

	/** For Datasmith assets we need to store the imported content to a Runtime Datasmith actor */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Datasmith")
	TObjectPtr<class ADatasmithRuntimeActor> RuntimeDatasmithAnchor;

	/** For Datasmith we need to store the materials variations in a Map as a datasmith actor is made up of multiple
	 * meshes and materials, and we should only loop search once then only loop over this map */
	UPROPERTY(EditAnywhere, Category = "MeshGenerator|Datasmith")
	TMap<TWeakObjectPtr<UStaticMeshComponent>, FDatasmithMaterials> DatasmithMaterialsMap;

	/** This bool is used for working out whether this is a datasmith asset or using the procedural mesh component */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Datasmith")
	bool bIsDatasmithAsset = false;
	
	/*
	* Array to store the Procedural Meshes UV0 to Generate 
	* - These are stored as 2D Vectors Structures, length must be the same as the length of vertices array 
	*/
	//UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshGenerator|MeshData")
	//TArray<FVector2D> UV0;//TODO: find struct

	/*
	* Array to store the Procedural Meshes Vertex Colors to Generate
	* - These are stored as Linear Colour Structures, length must be the same as the length of vertices array 
	*/

	/*
	* Array to store the Procedural Meshes Tangents to Generate 
	* - These are stored as Proc Mesh Tangent Structures, 
	* length must be the same as the length of vertices array 
	*/
#pragma endregion PUBLIC_PROPERTIES_AND_COMPONENTS

protected:

private:
	/** TODO: We eventually want to get the mesh material and apply our materials to it as a mask or material function to it
	 * Material Instance Dynamic to apply to the Procedural Mesh Component after a mesh has been generated and set with
	 * the UI from user input.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshGenerator|Material", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMaterialInstanceDynamic> MobiusMaterialInstanceDynamic = nullptr;

public:
#pragma region GETTERS_SETTERS
	/** Getters */
	/** Get the Material Instance for the mesh */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	FORCEINLINE UMaterialInstanceDynamic* GetMobiusMaterialInstanceDynamic() const { return MobiusMaterialInstanceDynamic; }

	/**
	 * Get the DatasmithMaterialsMap
	 */
	FORCEINLINE TMap<TWeakObjectPtr<UStaticMeshComponent>, FDatasmithMaterials> GetDatasmithMaterialsMap() const { return DatasmithMaterialsMap; }
	
	/** Setters */
	/** Set the Material Instance for the mesh */
	UFUNCTION(BlueprintCallable, Category = "MeshGenerator|Material")
	FORCEINLINE void SetMobiusMaterialInstanceDynamic(UMaterialInstanceDynamic* InMobiusMaterialInstanceDynamic) { MobiusMaterialInstanceDynamic = InMobiusMaterialInstanceDynamic; }
#pragma endregion GETTERS_SETTERS

};
