// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 Nicholas R. Harding
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

#include "RuntimeMeshGeneration/RuntimeMeshBuilder.h"
#include "ProceduralMeshComponent.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Kismet/KismetMathLibrary.h"
#include "ProjectMobius/AsyncAssimpMeshLoader.h"
#include "DatasmithRuntime.h"
#include "DatasmithRuntimeBlueprintLibrary.h"
#include "DatasmithSceneFactory.h"
#include "DirectLink/DatasmithSceneReceiver.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInstanceConstant.h"

// Sets default values
ARuntimeMeshBuilder::ARuntimeMeshBuilder() :
	MobiusProceduralMeshComponent(nullptr),
	DatasmithMaterialsMap(TMap<TWeakObjectPtr<UStaticMeshComponent>, FDatasmithMaterials>())
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Create the ProceduralMeshComponent
	MobiusProceduralMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MobiusProceduralMeshComponent"));

	// Set the ProceduralMeshComponent as the RootComponent
	RootComponent = MobiusProceduralMeshComponent;

	MobiusProceduralMeshComponent->bRenderInMainPass = true;
	MobiusProceduralMeshComponent->bUseAsyncCooking = true;
	MobiusProceduralMeshComponent->bUseComplexAsSimpleCollision = false;
	MobiusProceduralMeshComponent->bUseComplexAsSimpleCollision = false;
	MobiusProceduralMeshComponent->bSelectable = true;
	MobiusProceduralMeshComponent->Mobility = EComponentMobility::Movable;
	MobiusProceduralMeshComponent->SetSimulatePhysics(false);
	MobiusProceduralMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	

}

// Called when the game starts or when spawned
void ARuntimeMeshBuilder::BeginPlay()
{
	Super::BeginPlay();

	// As mesh generation needs to happen when the game starts and the world is required the delegate is bound here
	if(GetWorld())
	{
		UProjectMobiusGameInstance* GameInst = GetMobiusGameInstance(GetWorld());
		if(GameInst)
		{
			GameInst->OnMeshFileChanged.AddDynamic(this, &ARuntimeMeshBuilder::UpdateMeshFileName);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Game Instance is not valid, Mesh Generation will not work"));
		}
	}
	else
	{
		// TODO: Implement error to user that mesh generator will not work
		UE_LOG(LogTemp, Error, TEXT("World is not valid, Mesh Generation will not work"));
	}
}

// Called every frame
void ARuntimeMeshBuilder::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ARuntimeMeshBuilder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// If in game mode, don't clear the mesh
	if(!GetWorld()->GetBegunPlay())
	{
		MobiusProceduralMeshComponent->ClearAllMeshSections();
	}
	
	
	// moving the mesh is causing headaches and memory issues
	
}

void ARuntimeMeshBuilder::GenerateMobiusMesh(TArray<FVector> InVertices, TArray<int32> InTriangles, TArray<FVector> InNormals)
{
	// TODO: Implement Input Validation Checks

	// Clear the previous mesh -- TBD: If we want to clear all sections or a specific one
	MobiusProceduralMeshComponent->ClearAllMeshSections();

	//bp method calls this CreateMeshSection_LinearColor

	MobiusProceduralMeshComponent->CreateMeshSection(0, InVertices, InTriangles, InNormals, TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);
}

void ARuntimeMeshBuilder::GetMeshDataFromFile(const FRotator MeshRotationOffset)
{
	// TEST
	//FString FilePath = "C:\\Users\\User_VR4\\Desktop\\WORK\\ProjectMobius\\ProjectMobius\\TestData\\TechnicalSchool1000People\\Technical-School-For-Lab-3D.fbx";
	//FString FilePath = "C:\\Users\\User_VR4\\Desktop\\WORK\\ProjectMobius\\ProjectMobius\\TestData\\ISO-Test-8-r25-ifc2x3-to.obj";
	//FString FilePath = "C:\\Users\\User_VR4\\Desktop\\WORK\\ProjectMobius\\ProjectMobius\\TestData\\ISO-Test-8-r25.fbx";
	//FString FilePath = "D:\\1_Work\\Mobius\\ProjectMobius\\TestData\\ISO-Test-1-3DView.fbx";
	//FString FilePath = "D:\\1_Work\\Mobius\\ProjectMobius\\TestData\\ISO-Test-1-2x3.ifc";
	int32 SectionCount = 0;
	FString ErrorMessageCode;
	TArray<FVector> MVertices;
	TArray<int32> MFaces;
	TArray<FVector> MNormals;
	TArray<FVector2D> MUV;
	TArray<FVector> MTangents;
	
	//MobiusProceduralMeshComponent->ClearAllMeshSections(); see comments below
	// this could be the memory issue as it clears and will mark as dirty and will be updated in the next frame
	// the create mesh section also does the same thing and it could be causing the issue

	if(IAssimpInterface::OpenMeshFileGetWithAssimp(MeshFileName, SectionCount, ErrorMessageCode, MVertices, MFaces, MNormals, MUV, MTangents, MeshRotationOffset))
	{
		UE_LOG(LogTemp, Warning, TEXT("Successfully opened the mesh file"));
		UE_LOG(LogTemp, Warning, TEXT("Section Count: %d"), SectionCount);
		UE_LOG(LogTemp, Warning, TEXT("Vertices Count: %d"), MVertices.Num());
		UE_LOG(LogTemp, Warning, TEXT("Faces Count: %d"), MFaces.Num());
		UE_LOG(LogTemp, Warning, TEXT("Normals Count: %d"), MNormals.Num());
		UE_LOG(LogTemp, Warning, TEXT("Tangents Count: %d"), MTangents.Num());
		UE_LOG(LogTemp, Warning, TEXT("UV Count: %d"), MUV.Num());

		// A mesh section should only be created if successful
		MobiusProceduralMeshComponent->CreateMeshSection_LinearColor(0, MVertices, MFaces, MNormals, MUV,
		                                                             TArray<FLinearColor>(),
		                                                             TArray<FProcMeshTangent>(),
		                                                             false);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to open the mesh file: %s"), *ErrorMessageCode);
	}
	// // Lambda function to rotate the vectors by 90 degrees
	// auto RotateVector = [](const FVector& InVector) -> FVector
	// {
	// 	return UKismetMathLibrary::LessLess_VectorRotator(InVector, FRotator(0.0f, 0.0f, -180.0f));
	// };
	// for(int32 Index = 0; Index < MVertices.Num(); Index++)
	// {
	// 	if(MVertices.IsValidIndex(Index))
	// 	{
	// 		MVertices[Index] = RotateVector(MVertices[Index]);
	// 	}
	// 	if(MNormals.IsValidIndex(Index))
	// 	{
	// 		MNormals[Index] = RotateVector(MNormals[Index]);
	// 	}
	// 	if(MTangents.IsValidIndex(Index))
	// 	{
	// 		MTangents[Index] = RotateVector(MTangents[Index]);
	// 	}
	// }
	
}

void ARuntimeMeshBuilder::UpdateMeshFileName()
{
	// Get the game instance
	UProjectMobiusGameInstance* GameInst = GetMobiusGameInstance(GetWorld());

	// Assign the new file path to the mesh file name
	MeshFileName = GameInst->GetSimulationMeshFilePath();

	// for memory issues, we need to clear all the mesh sections first then get the new mesh data
	MobiusProceduralMeshComponent->ClearAllMeshSections();
	if (RuntimeDatasmithAnchor)
	{
		RuntimeDatasmithAnchor->Destroy();
		RuntimeDatasmithAnchor = nullptr;
	}
	DatasmithMaterialsMap.Empty();
	bIsDatasmithAsset = false;

	// As we are now able to use Datasmith assets we need to check if the file is a .udatasmith file
	if(MeshFileName.Contains(".udatasmith") || MeshFileName.Contains(".ifc"))
	{
		// check world is valid
		if(!CheckStillInWorld())
		{
			UE_LOG(LogTemp, Error, TEXT("World is not valid"));
			return;
		}

		// set the flag to indicate this is a datasmith file
		bIsDatasmithAsset = true;
		
		// spawn a runtime datasmtih actor to load the mesh
		RuntimeDatasmithAnchor = GetWorld()->SpawnActor<ADatasmithRuntimeActor>();
		
		if(MeshFileName.Contains(".udatasmith"))
		{
			// is the runtime datasmith anchor valid
			if(RuntimeDatasmithAnchor == nullptr)
			{
				UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
				return;
			}

			// import options
			FDatasmithRuntimeImportOptions ImportOptions;// TODO: set the import options

			// For thesis, I don't want collisions
			ImportOptions.BuildCollisions = ECollisionEnabled::Type::NoCollision;
			ImportOptions.CollisionType = ECollisionTraceFlag::CTF_UseSimpleAsComplex;
			ImportOptions.TessellationOptions.bUseCADKernel = true;
			
			RuntimeDatasmithAnchor->ImportOptions = ImportOptions;

			// import the mesh data into the anchor
			RuntimeDatasmithAnchor->LoadFile(MeshFileName);

			// Async task to check if the scene is loaded
			Async(EAsyncExecution::ThreadPool, [this]()
			      {
				      FPlatformProcess::Sleep(5.0f);
				      // log building and receiving
				      UE_LOG(LogTemp, Warning, TEXT("Building: %d, Receiving: %d"), RuntimeDatasmithAnchor->bBuilding, RuntimeDatasmithAnchor->IsReceiving());
				      while (RuntimeDatasmithAnchor->bBuilding || RuntimeDatasmithAnchor->IsReceiving())
				      {
					      // log building and receiving
					      UE_LOG(LogTemp, Warning, TEXT("Building: %d, Receiving: %d"), RuntimeDatasmithAnchor->bBuilding, RuntimeDatasmithAnchor->IsReceiving());
					      // sleep for 0.05 seconds
					      FPlatformProcess::Sleep(0.05f);
				      }
				      // log building and receiving
				      UE_LOG(LogTemp, Warning, TEXT("Building: %d, Receiving: %d"), RuntimeDatasmithAnchor->bBuilding, RuntimeDatasmithAnchor->IsReceiving());

			      }, [this]()
			      {
				      AsyncTask(ENamedThreads::GameThread,[this] {
					      CreateDatasmithMaterials();
				      });
				
			      });

			
			

			// wait for the scene to be loaded for now delay for 5 seconds
			//FTimerHandle TimerHandle;
			//GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &ARuntimeMeshBuilder::TestDatasmithMaterialSetup, 15.0f, false);
		}
		else
		{
			// import options
			FDatasmithRuntimeImportOptions ImportOptions;// TODO: set the import options

			//ImportOptions.TessellationOptions.bUseCADKernel = true;
			
			RuntimeDatasmithAnchor->ImportOptions = ImportOptions;

			// // construct new object UDatasmithSceneElement
			// UDatasmithSceneElement* DatasmithSceneElement = NewObject<UDatasmithSceneElement>();
			//
			// UDatasmithSceneElement* ConstructedScene = DatasmithSceneElement->ConstructDatasmithSceneFromFile(MeshFileName);
			//
			// auto Imported = ConstructedScene->ImportScene("/Game/");
			//
			// auto CreatedScene = FDatasmithSceneFactory::CreateScene(*Imported.Scene.GetName());
			//
			// RuntimeDatasmithAnchor->SetScene(CreatedScene);
			// RuntimeDatasmithAnchor->ApplyNewScene();
			// RuntimeDatasmithAnchor->MarkComponentsRenderStateDirty();
		}
		
		
	}
	// not a datasmith file so we can load the mesh as normal
	else
	{
		//GetMeshDataFromFile(FRotator(0.0f, 0.0f, 90.0f));
		AsyncUpdateMesh(MeshFileName);
	}
}

void ARuntimeMeshBuilder::AsyncUpdateMesh(const FString PathToMesh)
{
	// As this is game thread dependent we need to ensure this is called on the game thread and return if not
	if(!IsInGameThread())
	{
		UE_LOG(LogTemp, Error, TEXT("AsyncUpdateMesh must be called on the game thread and after the game has started"));
		return;
	}

	// set the mesh being loaded flag
	bMeshBeingBuilt = true;
	
	FString FilePath = "C:\\Users\\User_VR4\\Desktop\\WORK\\ProjectMobius\\ProjectMobius\\TestData\\TechnicalSchool1000People\\Technical-School-For-Lab-3D.fbx";
	AsyncAssimpLoader = NewObject<UAsyncAssimpMeshLoader>();
	// Create the runnable
	AsyncAssimpLoader->MeshLoaderRunnable = new FAssimpMeshLoaderRunnable(PathToMesh);
	AsyncAssimpLoader->MeshLoaderRunnable->OnLoadMeshDataComplete.AddDynamic(this, &ARuntimeMeshBuilder::GetTheAsyncMeshData);
}

void ARuntimeMeshBuilder::GetTheAsyncMeshData()
{
	// TODO:check valid data
	//AsyncAssimpLoader->MeshLoaderRunnable

	if(AsyncAssimpLoader == nullptr || AsyncAssimpLoader->MeshLoaderRunnable == nullptr)// important to check if the runnable is valid
	{
		/** TODO: if we get here and the runnable is not valid or the async loader is not valid we need to handle this
			As this means a thread has started but the object has lost its reference and we need to clean up the thread
			as we know the name of the thread we can look for it this way and clean it up
		*/
		return;
	}

	
	// A mesh section should only be created if successful
	MobiusProceduralMeshComponent->CreateMeshSection_LinearColor(0, AsyncAssimpLoader->MeshLoaderRunnable->Vertices,
	                                                             AsyncAssimpLoader->MeshLoaderRunnable->Faces,
	                                                             AsyncAssimpLoader->MeshLoaderRunnable->Normals,
	                                                             AsyncAssimpLoader->MeshLoaderRunnable->UV,
	                                                             TArray<FLinearColor>(),
	                                                             TArray<FProcMeshTangent>(),
	                                                             false);

	// The loader is no longer needed so we can stop the thread
	AsyncAssimpLoader->MeshLoaderRunnable->Stop();
	
	// nullptr the runnable to free up memory
	AsyncAssimpLoader->MeshLoaderRunnable = nullptr;

	// if the material property is set then we want to apply our material to the mesh
	if(MobiusMaterialInstanceDynamic != nullptr)
	{
		MobiusProceduralMeshComponent->SetMaterial(0, MobiusMaterialInstanceDynamic);
	}

	// Mesh has been built so we can set the flag to false
	bMeshBeingBuilt = false;

	// The origin we want to broadcast is the smallest location of the mesh bounds as the mesh generator for the heatmap
	// works from left to right and bottom to top
	FVector HeatmapOrigin = MobiusProceduralMeshComponent->Bounds.Origin - MobiusProceduralMeshComponent->Bounds.BoxExtent;

	// Broadcast that the mesh has been built
	OnMeshBuilt.Broadcast(HeatmapOrigin, MobiusProceduralMeshComponent->Bounds.BoxExtent);
}

void ARuntimeMeshBuilder::UpdateMeshMaterial(UMaterialInstanceDynamic* InMaterial)
{
	// Check input is valid
	if(InMaterial == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Material is not valid"));
		return;
	}
	MobiusMaterialInstanceDynamic = InMaterial;

	
	// Mesh no longer being built so we can set the material
	SetMaterialOnMesh();
}

void ARuntimeMeshBuilder::SetDatasmithMeshToUseNonModifiedMaterials(bool bUseNonModifiedMaterials)
{
	// log called
	UE_LOG(LogTemp, Warning, TEXT("SetDatasmithMeshToUseNonModifiedMaterials Called"));
	
	// check if the datasmith anchor is valid and that the map is not empty
	if(RuntimeDatasmithAnchor == nullptr || DatasmithMaterialsMap.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("Datasmith Anchor is not valid or the Datasmith Materials Map is empty"));
		return;
	}

	// loop over the map and set the materials to use the non modified materials
	for(auto& DatasmithMaterial : DatasmithMaterialsMap)
	{
		// loop over the materials and set the material
		for(auto Material : DatasmithMaterial.Value.MeshMaterials)
		{
			// set the material to use the non modified material
			Material->SetScalarParameterValue(FName("Use Modified Material"), bUseNonModifiedMaterials ? 0.0f : 1.0f);
		}

		// get num materials on the mesh component and set the material to use the non modified material
		for (int32 Index = 0; Index < DatasmithMaterial.Key->GetNumMaterials(); Index++)
		{
			
			UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(DatasmithMaterial.Key->GetMaterial(Index));

			if (Mat != nullptr)
			{
				Mat->SetScalarParameterValue(FName("Use Modified Material"), bUseNonModifiedMaterials ? 0.0f : 1.0f);
			}
		}
		
	}
	UE_LOG(LogTemp, Warning, TEXT("SetDatasmithMeshToUseNonModifiedMaterials Finished"));
}

void ARuntimeMeshBuilder::SetDatasmithMeshToTranslucentMaterials()
{
	UE_LOG(LogTemp, Error, TEXT("SetDatasmithMeshToTranslucentMaterials Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		// get num materials on the mesh component and set the material to use the non modified material
		for (int32 Index = 0; Index < DatasmithMaterialMap.Key->GetNumMaterials(); Index++)
		{
			// set the material to use the translucent material
			DatasmithMaterialMap.Key->SetMaterial(Index,DatasmithMaterialMap.Value.MeshMaterials[Index * 2 + 1]);
		}
	}
	UE_LOG(LogTemp, Error, TEXT("SetDatasmithMeshToTranslucentMaterials Finished"));
}

void ARuntimeMeshBuilder::SetDatasmithMeshToSolidMaterials()
{
	UE_LOG(LogTemp, Error, TEXT("SetDatasmithMeshToSolidMaterials Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		// get num materials on the mesh component and set the material to use the non modified material
		for (int32 Index = 0; Index < DatasmithMaterialMap.Key->GetNumMaterials(); Index++)
		{
			// set the material to use the translucent material
			DatasmithMaterialMap.Key->SetMaterial(Index,DatasmithMaterialMap.Value.MeshMaterials[Index * 2]);
		}
	}
}

void ARuntimeMeshBuilder::UpdateDatasmithMeshOpacity(float Opacity)
{
	UE_LOG(LogTemp, Warning, TEXT("UpdateDatasmithMeshOpacity Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the opacity amount
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		for (auto Material : DatasmithMaterialMap.Value.MeshMaterials)
		{
			// set the opacity amount
			Material->SetScalarParameterValue(FName("OpacityAmount"), Opacity);
		}
	}
}

void ARuntimeMeshBuilder::BoxDissolveDatasmithMesh(bool bDissolve)
{
	UE_LOG(LogTemp, Warning, TEXT("BoxDissolveDatasmithMesh Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		for (auto Material : DatasmithMaterialMap.Value.MeshMaterials)
		{
			// set the material to use the translucent material
			Material->SetScalarParameterValue(FName("Box Dissolve?"), bDissolve ? 1.0f : 0.0f);
		}
	}
}

void ARuntimeMeshBuilder::SetDatasmithToUseModifiedColour(bool bUseModifiedColour, FLinearColor NewColour)
{
	UE_LOG(LogTemp, Warning, TEXT("SetDatasmithToUseModifiedColour Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		for (auto Material : DatasmithMaterialMap.Value.MeshMaterials)
		{
			// set the material shading to use the modified colour material or not
			Material->SetScalarParameterValue(FName("Use Modified Colour"), bUseModifiedColour ? 1.0f : 0.0f);

			// set the modified colour on the material
			Material->SetVectorParameterValue(FName("NewColour"), NewColour);
		}
	}
}

void ARuntimeMeshBuilder::SetDatasmithMeshToUseClearCoatMaterials(bool bUseClearCoatMaterials)
{
	UE_LOG(LogTemp, Warning, TEXT("SetDatasmithMeshToUseClearCoatMaterials Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		for (auto Material : DatasmithMaterialMap.Value.MeshMaterials)
		{
			// set the material shading to use the clear coat material or not
			Material->SetScalarParameterValue(FName("Default Lit Shading"), bUseClearCoatMaterials ? 0.0f : 1.0f);
		}
	}
}

void ARuntimeMeshBuilder::SetDatasmithDissolveMeshSizeAndOrigin(FVector Origin, FVector Extents)
{
	UE_LOG(LogTemp, Warning, TEXT("SetDatasmithDissolveMeshSizeAndOrigin Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	FLinearColor BoxLocation = FLinearColor(Origin.X, Origin.Y, Origin.Z, 1.0f);
	FLinearColor BoxBounds = FLinearColor(Extents.X, Extents.Y, Extents.Z, 1.0f);

	// loop over the map and set the box dissolve parameters
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		for (auto Material : DatasmithMaterialMap.Value.MeshMaterials)
		{
			// set the material shading to use the clear coat material or not
			Material->SetVectorParameterValue(FName("Box Dissolve Location"), BoxLocation);
			Material->SetVectorParameterValue(FName("Box Dissolve Bounds"), BoxBounds);
		}
	}
}

void ARuntimeMeshBuilder::SetDatasmithToOriginalMatStyle()
{
	UE_LOG(LogTemp, Warning, TEXT("SetDatasmithToOriginalMatStyle Called"));
	// Check datasmith anchor is valid
	if(RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// loop over the map and set the mesh to use the translucent materials
	for(auto& DatasmithMaterialMap : DatasmithMaterialsMap)
	{
		for (auto Material : DatasmithMaterialMap.Value.MeshMaterials)
		{
			// set the material shading to use the modified colour material or not
			Material->SetScalarParameterValue(FName("Use Modified Colour"), 1.0f);
			Material->SetScalarParameterValue(FName("Use Modified Material"), 0.0f);

			// set the material shading to use the clear coat material or not
			if(DatasmithMaterialMap.Value.bIsOpaque)
			{
				Material->SetScalarParameterValue(FName("Default Lit Shading"), 1.0f);
			}
			else
			{
				Material->SetScalarParameterValue(FName("Default Lit Shading"), 0.0f);
			}
		}

		// if is opaque then set the material to use the opaque material
		if(DatasmithMaterialMap.Value.bIsOpaque)
		{
			for (int32 Index = 0; Index < DatasmithMaterialMap.Key->GetNumMaterials(); Index++)
			{
				// set the material to use the opaque material
				DatasmithMaterialMap.Key->SetMaterial(Index,DatasmithMaterialMap.Value.MeshMaterials[Index * 2]);
			}
		}
		else
		{
			for (int32 Index = 0; Index < DatasmithMaterialMap.Key->GetNumMaterials(); Index++)
			{
				// set the material to use the translucent material
				DatasmithMaterialMap.Key->SetMaterial(Index,DatasmithMaterialMap.Value.MeshMaterials[Index * 2 + 1]);
			}
		}
	}
}

void ARuntimeMeshBuilder::SetMaterialOnMesh()
{
	// As the mesh may not exist we check if it does
	if(MobiusProceduralMeshComponent != nullptr)
	{
		MobiusProceduralMeshComponent->SetMaterial(0, MobiusMaterialInstanceDynamic);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Procedural Mesh Component is not valid"));
	}
}

void ARuntimeMeshBuilder::CreateDatasmithMaterials()
{
	// check datasmith anchor is valid
	if (RuntimeDatasmithAnchor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Runtime Datasmith Anchor is not valid"));
		return;
	}
	
	// get the datasmith components
	auto DataComps = RuntimeDatasmithAnchor->GetComponents();

	// log number of data components
	UE_LOG(LogTemp, Warning, TEXT("Number of Data Components: %d"), DataComps.Num());

	// if the data comps are not valid throw error and return
	if (DataComps.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("Data Components are not valid"));
		return;
	}

	// For my thesis we don't want to edit certain meshes materials and some of the meshes need to be removed like the placeholders for people
	FString MeshesToIgnore[] = {"Starting_Ground","Starting_Landscape"};
	FString MeshesToRemove[] = {"Entourage"};

	int DESTROY_ME_INDEX = -1;

	// loop over the data components
	for (auto DataComp: DataComps)
	{
		DESTROY_ME_INDEX++;
		
		// cast to scene component
		auto SceneComp = Cast<USceneComponent>(DataComp);

		if (SceneComp != nullptr)
		{
			// Array to store the children components
			TArray<USceneComponent*> ChildrenComps;

			// get the children components 
			SceneComp->GetChildrenComponents(true, ChildrenComps);

			int32 i = 0;

			// loop over the children components
			for (auto ChildComp : ChildrenComps)
			{
				DESTROY_ME_INDEX++;
				// delete cardboard cutouts of people
				if (ChildComp != nullptr && (ChildComp->GetName().Contains("Entourage") || ChildComp->GetName().Contains("Starting_")))
				{
					UE_LOG(LogTemp, Error, TEXT("Child Component: %s"), *ChildComp->GetName());
					TArray<USceneComponent*> ChildrenToDelete;
					ChildComp->GetChildrenComponents(true, ChildrenToDelete);
					for (auto Child : ChildrenToDelete)
					{
						UE_LOG(LogTemp, Error, TEXT("Child: %s"), *Child->GetName());
						Child->DestroyComponent();
					}
					ChildComp->DestroyComponent(false);
					continue;
				}
				// delete intersecting pad
				if (ChildComp != nullptr && ChildComp->GetName().Contains("Pads_Pad_Pad_1"))
				{
					UE_LOG(LogTemp, Error, TEXT("Child Component: %s"), *ChildComp->GetName());
					ChildComp->DestroyComponent(false);
					continue;
				}
				
				
				if (i <= 2 && i > 0)
				{
					UE_LOG(LogTemp, Error, TEXT("Child Component: %s"), *ChildComp->GetName());
					TArray<USceneComponent*> ChildrenToDelete;
					ChildComp->GetChildrenComponents(true, ChildrenToDelete);
					for (auto Child : ChildrenToDelete)
					{
						UE_LOG(LogTemp, Error, TEXT("Child: %s"), *Child->GetName());
						Child->DestroyComponent();
					}
					ChildComp->DestroyComponent(false);
					i++;
					continue;
				}
				
				if (i == 0)
				{
					i++;
				}
				
				// cast child to mesh component
				auto MeshComp = Cast<UStaticMeshComponent>(ChildComp);

				// if the mesh component is not valid then continue
				if (MeshComp == nullptr || MeshComp->IsBeingDestroyed())
				{
					continue;
				}
				// DESTROY_ME_INDEX at 387 is the Pad_Pad_1 mesh //TODO check if it matches on other pc
				if (MeshComp->GetName().Contains("Entourage") || MeshComp->GetName().Contains("Pads_Pad_Pad_1") || MeshComp->GetReadableName().Contains("Pads_Pad_Pad_1"))
				{
					UE_LOG(LogTemp, Error, TEXT("Mesh to remove: %s, DESTROY INDEX: %i"), *MeshComp->GetReadableName(), DESTROY_ME_INDEX);
					MeshComp->DestroyComponent(true);
					continue;
				}
				
				// create new struct to store the materials
				FDatasmithMaterials DatasmithMaterials;

				// THESIS, The mesh doesn't need to cast shadows
				MeshComp->CastShadow = false;

				// log the num of materials
				UE_LOG(LogTemp, Warning, TEXT("Num Materials: %d"), MeshComp->GetNumMaterials());

				// loop over each material
				for (int32 Index = 0; Index < MeshComp->GetNumMaterials(); Index++)
				{
					// get the material
					auto Material = MeshComp->GetMaterial(Index);

					// get the materials parent material
					auto ParentMaterial = Material->GetMaterial();

					// Array to store the new material instances
					TArray<TObjectPtr<UMaterialInstanceDynamic>> MaterialInstances;

					// if the parent material equals M_TMStdOpaque then we can set with the new material otherwise return
					if (ParentMaterial->GetName() == "M_TMStdOpaque") // solid variants - ie walls
					{
						MaterialInstances.Append(CreateOpaqueMaterials(Material));
						MaterialInstances.Append(CreateTranslucentMaterials(Material, true));
						DatasmithMaterials.bIsOpaque = true;
					}
					else if (ParentMaterial->GetName() == "M_TMStdTranslucentNEW") // translucent variants - ie windows
					{
						MaterialInstances.Append(CreateOpaqueMaterials(Material));
						MaterialInstances.Append(CreateTranslucentMaterials(Material));
					}
					else
					{
						// TODO: Implement other material types
						continue;
					}
					
					DatasmithMaterials.MeshMaterials.Append(MaterialInstances);

					// on initial load we want the materials to be there default datasmith look
					if (DatasmithMaterials.bIsOpaque)
					{
						// set the dynamic material on the mesh component
						MeshComp->SetMaterial(Index, DatasmithMaterials.MeshMaterials[Index * 2]);
					}
					else
					{
						// set the dynamic material on the mesh component
						MeshComp->SetMaterial(Index, DatasmithMaterials.MeshMaterials[Index * 2 + 1]);
					}
						
					// Add Material and mesh to the map
					DatasmithMaterialsMap.Add(MeshComp, DatasmithMaterials);
					
				}

				
			}
		}
	}
	// DEBUG Testing
	// SetDatasmithMeshToUseNonModifiedMaterials(false);
	//
	// UpdateDatasmithMeshOpacity(0.8f);
	//
	// BoxDissolveDatasmithMesh(true);
	//
	// SetDatasmithMeshToSolidMaterials();
	// SetDatasmithMeshToTranslucentMaterials();
	// SetDatasmithMeshToUseClearCoatMaterials();
	// SetDatasmithToUseModifiedColour(true);
	
	// Min and Max bound values for the mesh
	FVector3f StartPos(MAX_flt, MAX_flt, MAX_flt);
	FVector3f EndPos(-MAX_flt, -MAX_flt, -MAX_flt);

	// Find the bounds of the mesh
	// get all the meshes from the datasmith anchor
	for (auto& Tuple : DatasmithMaterialsMap)
	{
		auto Mesh = Tuple.Key;

		if (Mesh == nullptr || !Mesh->GetStaticMesh())
		{
			continue;
		}

		UStaticMesh* StaticMesh = Mesh->GetStaticMesh();
		if (!StaticMesh->GetRenderData() || 
			!StaticMesh->GetRenderData()->IsInitialized() ||
			StaticMesh->GetRenderData()->LODResources.Num() == 0)
		{
			continue;
		}

		// Get the local to world transform
		FTransform LocalToWorldTransform = Mesh->GetComponentTransform();
		
		// Get the vertices from the mesh
		const FPositionVertexBuffer& VertexBuffer = Mesh->GetStaticMesh()->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer;
				
		for(uint32 Index = 0;  Index < VertexBuffer.GetNumVertices(); Index++)
		{
			// Get the local vertex
			FVector3f LocalVertex = VertexBuffer.VertexPosition(Index);
			
			// Transform the local vertex to world space
			FVector WorldVertex = LocalToWorldTransform.TransformPosition(FVector(LocalVertex));

			// Find if this Vertex is bigger or smaller than the current bounds
			StartPos = FVector3f(
				FMath::Min(StartPos.X, WorldVertex.X),
				FMath::Min(StartPos.Y, WorldVertex.Y),
				FMath::Min(StartPos.Z, WorldVertex.Z)
			);
			EndPos = FVector3f(
				FMath::Max(EndPos.X, WorldVertex.X),
				FMath::Max(EndPos.Y, WorldVertex.Y),
				FMath::Max(EndPos.Z, WorldVertex.Z)
			);
		}
	}

	// Create bounds
	FVector Start(StartPos);
	FVector End(EndPos);
	FBox Bounds(Start, End);

	// log the start and end positions
	UE_LOG(LogTemp, Warning, TEXT("Start: %s, End: %s"), *Start.ToString(), *End.ToString());
	UE_LOG(LogTemp, Warning, TEXT("StartPos: %s, EndPos: %s"), *StartPos.ToString(), *EndPos.ToString());

	// //FVector T1 = (Start+End);
	// FVector T1 = End/2;
	// DrawDebugPoint(GetWorld(), T1, 10.0f, FColor::Blue, false, 150.0f);
	// FVector T2 = Start/2;
	// DrawDebugPoint(GetWorld(), T2, 10.0f, FColor::Orange, false, 150.0f);
	//
	// // center of the bounds
	// FVector Center = Bounds.GetCenter();
	// DrawDebugPoint(GetWorld(), Center, 10.0f, FColor::Purple, false, 150.0f);
	//
	// // Draw points on the start and end of the bounds
	// DrawDebugPoint(GetWorld(), Start, 10.0f, FColor::Green, false, 150.0f);
	// DrawDebugPoint(GetWorld(), End, 10.0f, FColor::Green, false, 150.0f);
	//
	// // draw the bounds
	// DrawDebugBox(GetWorld(), Bounds.GetCenter(), Bounds.GetExtent(), FColor::Red, false, 150.0f, 0, 5.0f);

	// The origin we want to broadcast is the smallest location of the mesh bounds as the mesh generator for the heatmap
	// works from left to right and bottom to top
	FVector HeatmapOrigin = Bounds.GetCenter() - Bounds.GetExtent();

	// broadcast the mesh has been built
	OnMeshBuilt.Broadcast(HeatmapOrigin, Bounds.GetExtent());
}

TArray<TObjectPtr<UMaterialInstanceDynamic>> ARuntimeMeshBuilder::CreateMaterialInstances(UMaterialInterface* InMaterial, const FString& MaterialPath)
{
	TArray<TObjectPtr<UMaterialInstanceDynamic>> MaterialInstances;

	TArray<FMaterialParameterInfo> ScalarParams;
	TArray<FMaterialParameterInfo> VectorParams;
	TArray<FMaterialParameterInfo> TextureParams;
	TArray<FGuid> ScalarGuids;
	TArray<FGuid> VectorGuids;
	TArray<FGuid> TextureGuids;

	InMaterial->GetAllScalarParameterInfo(ScalarParams, ScalarGuids);
	InMaterial->GetAllVectorParameterInfo(VectorParams, VectorGuids);
	InMaterial->GetAllTextureParameterInfo(TextureParams, TextureGuids);

	UMaterialInstanceDynamic* DynamicMaterial = nullptr;
	auto* LoadedMaterial = LoadObject<UMaterialInstanceConstant>(nullptr, *MaterialPath);

	if (LoadedMaterial)
	{
		DynamicMaterial = UMaterialInstanceDynamic::Create(LoadedMaterial, this);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load the material: %s"), *MaterialPath);
		return MaterialInstances;
	}

	for (const auto& Param : ScalarParams)
	{
		float Value = 0.0f;
		if (InMaterial->GetScalarParameterValue(Param.Name, Value))
		{
			DynamicMaterial->SetScalarParameterValue(Param.Name, Value);
		}
	}

	for (const auto& Param : VectorParams)
	{
		FLinearColor Value;
		if (InMaterial->GetVectorParameterValue(Param.Name, Value))
		{
			DynamicMaterial->SetVectorParameterValue(Param.Name, Value);
		}
	}

	for (const auto& Param : TextureParams)
	{
		UTexture* Value = nullptr;
		if (InMaterial->GetTextureParameterValue(Param.Name, Value))
		{
			DynamicMaterial->SetTextureParameterValue(Param.Name, Value);
		}
	}

	MaterialInstances.Add(DynamicMaterial);
	return MaterialInstances;
}

TArray<TObjectPtr<UMaterialInstanceDynamic>> ARuntimeMeshBuilder::CreateOpaqueMaterials(UMaterialInterface* InMaterial)
{
	static const FString OpaqueMaterialPath = TEXT("MaterialInstanceConstant'/Game/01_Dev/RuntimeMeshGenerator/DatasmithMasterMaterials/MI_DatasmithOpaqueMasked.MI_DatasmithOpaqueMasked'");
	return CreateMaterialInstances(InMaterial, OpaqueMaterialPath);
}

TArray<TObjectPtr<UMaterialInstanceDynamic>> ARuntimeMeshBuilder::CreateTranslucentMaterials(UMaterialInterface* InMaterial, bool bIsOpaque)
{
	const FString TranslucentMaterialPath = bIsOpaque
		                                        ? TEXT("MaterialInstanceConstant'/Game/01_Dev/RuntimeMeshGenerator/DatasmithMasterMaterials/MI_DatasmithTranslucent.MI_DatasmithTranslucent'")
		                                        : TEXT("MaterialInstanceConstant'/Game/01_Dev/RuntimeMeshGenerator/DatasmithMasterMaterials/WindowsGlass/MI_DatasmithTranslucent.MI_DatasmithTranslucent'");

	return CreateMaterialInstances(InMaterial, TranslucentMaterialPath);
}
