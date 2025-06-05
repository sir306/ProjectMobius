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

#include "HeatmapPixelTextureVisualizer.h"

#include "DynamicPixelRenderingTexture.h"
#include "HeatmapSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h" // has helper functions for procedural meshes
#include "GeomTools.h"// has helper functions for geometry
#include "Kismet/GameplayStatics.h"
#include "RuntimeMeshGeneration/RuntimeMeshBuilder.h"
#include "DatasmithRuntime.h"
#include "StaticMeshResources.h" // used for accessing vertex buffers on static meshes
#include "Rendering/PositionVertexBuffer.h" 


// Sets default values
AHeatmapPixelTextureVisualizer::AHeatmapPixelTextureVisualizer() :
	RuntimeHeatmapMeshComponent(nullptr),
	TextureWidth(2048),
	TextureHeight(2048),
	HeightDisplacement(0),
	ActorName("HeatmapPixelTextureVisualizer"),
	bLiveTrackingHeatmap(false),
	MaxAddHeight(10.0f),
	HeatmapMeshSize2D(204.8f, 204.8f),
	UVScale(0.0f, 0.0f),
	MeshOriginLocation(0.0f, 0.0f, 0.0f),
	World(nullptr),
	ScaledCircleSize(0)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// create new mesh component
	RuntimeHeatmapMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RuntimeHeatmapMeshComponent"));
	RootComponent = RuntimeHeatmapMeshComponent;

	// Build the default mesh vertices, triangles and UVs
	MeshVertices.Add(FVector(0, 0, 0));
	MeshVertices.Add(FVector(0, HeatmapMeshSize2D.Y, 0));
	MeshVertices.Add(FVector(HeatmapMeshSize2D.X, HeatmapMeshSize2D.Y, 0));
	MeshVertices.Add(FVector(HeatmapMeshSize2D.X, 0, 0));

	MeshUVs.Add(FVector2D(0, 0));
	MeshUVs.Add(FVector2D(0, 1));
	MeshUVs.Add(FVector2D(1, 1));
	MeshUVs.Add(FVector2D(1, 0));

	MeshTriangles.Add(0);
	MeshTriangles.Add(1);
	MeshTriangles.Add(2);
	MeshTriangles.Add(0);
	MeshTriangles.Add(2);
	MeshTriangles.Add(3);
	
	// Setup the default runtime mesh size
	RuntimeHeatmapMeshComponent->CreateMeshSection(0, MeshVertices, MeshTriangles, TArray<FVector>(), MeshUVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);

	// create new dynamic texture component
	//DynamicTexture = NewObject<UDynamicPixelRenderingTexture>(this, UDynamicPixelRenderingTexture::StaticClass(), FName(*(ActorName + "DynamicTexture")));

	//DynamicTexture = NewObject<UDynamicPixelRenderingTexture>(this, UDynamicPixelRenderingTexture::StaticClass(), FName(*(ActorName + "DynamicTexture")));

	HeatmapMaterialInstance = nullptr;
	VoronoiMaterialInstance = nullptr;
	
	// create the material instances
	//HeatmapMaterialInstance = CreateDefaultSubobject<UMaterialInstanceDynamic>(FName(*(ActorName + "HeatmapMaterialInstance")), true);
	//VoronoiMaterialInstance = CreateDefaultSubobject<UMaterialInstanceDynamic>(FName(*(ActorName + "VoronoiMaterialInstance")), true);

	// Load and assign the materials to the instances
	//CreateMaterialInstances();

	//RuntimeHeatmapMeshComponent->SetMaterial(0, HeatmapMaterialInstance);
	//SetupDynamicTexture();
	
}

void AHeatmapPixelTextureVisualizer::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);	
}

void AHeatmapPixelTextureVisualizer::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	//#if WITH_EDITOR
	CreateMaterialInstances();
	
	// check mesh is valid
	if(RuntimeHeatmapMeshComponent->GetProcMeshSection(0))
	{		
		HeatmapMeshSize2D = FVector2D(RuntimeHeatmapMeshComponent->GetProcMeshSection(0)->SectionLocalBox.GetSize().X, RuntimeHeatmapMeshComponent->GetProcMeshSection(0)->SectionLocalBox.GetSize().Y);
	}
	
	// Assign the Material Instance to the mesh depending on the heatmap type
	AssignMaterialInstanceToMesh();
	
	DynamicTexture = NewObject<UDynamicPixelRenderingTexture>(this, UDynamicPixelRenderingTexture::StaticClass(), FName(*(ActorName + "DynamicTexture")));
	SetupDynamicTexture();
	
	
	UpdateHeatmapMeshBounds();
	//#endif
}

void AHeatmapPixelTextureVisualizer::AssignMaterialInstanceToMesh() const
{
	if(HeatmapType)
	{
		RuntimeHeatmapMeshComponent->SetMaterial(0, HeatmapMaterialInstance);
	}
	else
	{
		RuntimeHeatmapMeshComponent->SetMaterial(0, VoronoiMaterialInstance);
	}
}

// Called when the game starts or when spawned
void AHeatmapPixelTextureVisualizer::BeginPlay()
{
	Super::BeginPlay();
	
	// setup world
	World = GetWorld();
	
	// //HeatmapRenderTarget = NewObject<UTextureRenderTarget2D>(this, UTextureRenderTarget2D::StaticClass());
	// SetupDynamicTexture();
	//
	// // Assign the Material Instance to the mesh depending on the heatmap type
	// AssignMaterialInstanceToMesh();

}

// Called every frame
void AHeatmapPixelTextureVisualizer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AHeatmapPixelTextureVisualizer::InitializeHeatmap(int32 InHeatmapType, bool bIsLiveTrackingNeeded, const FVector2D& MeshSize, float NewHeightDisplacement, bool bIs3DHeatmap)
{

	// Set the heatmap type
	if(InHeatmapType == 0)  
	{
		this->HeatmapType = 0;// voronoi
	}
	else
	{
		this->HeatmapType = 1; // needs the heatmap material type
	}

	// Set tracking flag
	bLiveTrackingHeatmap = bIsLiveTrackingNeeded;

	// Update the mesh size
	HeatmapMeshSize2D = MeshSize;

	//TextureWidth = FMath::RoundUpToPowerOfTwo(MeshSize.X * 2);
	//TextureHeight = FMath::RoundUpToPowerOfTwo(MeshSize.Y * 2);

	TextureWidth = 1024;
	//TextureWidth = 256;
	TextureHeight = 1024;
	//TextureHeight = 256;

	// square the texture size to the largest of the two
	if(TextureWidth > TextureHeight)
	{
		TextureHeight = TextureWidth;
	}
	else
	{
		TextureWidth = TextureHeight;
	}

	// Because of the way things are create we need to clamp the texture to a max value 8096
	// if(TextureWidth > 1024)
	// {
	// 	TextureWidth = 1024;
	// 	TextureHeight = 1024;
	// }

	// Create the Mesh
	GenerateMeshVerticesUVsAndTriangles(MeshSize, FIntPoint(TextureWidth, TextureHeight), bIs3DHeatmap); // TODO work out a resolution for the mesh to be generated

	// Set the height displacement
	this->HeightDisplacement = NewHeightDisplacement;

	// Create new material instances
	CreateMaterialInstances();
	
	// Assign the Material Instance to the mesh depending on the heatmap type
	AssignMaterialInstanceToMesh();

	// Create a new dynamic texture and configure it
	DynamicTexture = NewObject<UDynamicPixelRenderingTexture>(this, UDynamicPixelRenderingTexture::StaticClass(), FName(*(ActorName + "DynamicTexture")));
	SetupDynamicTexture();

	

	// check subsystem not null
	if(UHeatmapSubsystem* SubSystem = GetWorld()->GetSubsystem<UHeatmapSubsystem>())
	{
		// add this actor to the subsystem
		SubSystem->AddHeatmapActor(this);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to add the Heatmap Actor to the Heatmap Subsystem"));
	}

	// Update the mesh bounds
	UpdateHeatmapMeshBounds();
}

void AHeatmapPixelTextureVisualizer::CreateMaterialInstances()
{
	// get the materials

	UMaterialInterface* VoronoiMaterial = LoadObject<UMaterial>(
		nullptr, TEXT(
			"Material'/Game/01_Dev/NickMaster/Heatmaps/Materials/RenderTargetHeatmaps/M_VoronoiMap.M_VoronoiMap'"));
	
	UMaterialInterface* HeatmapMaterial = LoadObject<UMaterial>(
		nullptr, TEXT(
			"Material'/Game/01_Dev/NickMaster/Heatmaps/Materials/RenderTargetHeatmaps/M_HeatmapRT_V2.M_HeatmapRT_V2'"));
	
	
	// Assign the materials to the instance
	// Heatmap Instance Material - by checking name we avoid renaming existing instances which is not allowed
	if(!HeatmapMaterialInstance && HeatmapMaterial || HeatmapMaterialInstance->GetName() != ActorName + "HeatmapMaterialInstance")
	{
		HeatmapMaterialInstance = UMaterialInstanceDynamic::Create(HeatmapMaterial, this, FName(*(ActorName + "HeatmapMaterialInstance")));
	}
	if(!VoronoiMaterialInstance && VoronoiMaterial || VoronoiMaterialInstance->GetName() != ActorName + "VoronoiMaterialInstance")
	{
		VoronoiMaterialInstance = UMaterialInstanceDynamic::Create(VoronoiMaterial, this, FName(*(ActorName + "VoronoiMaterialInstance")));
	}
}

void AHeatmapPixelTextureVisualizer::SetupDynamicTexture() const
{
	// check texture is valid
	if(!DynamicTexture)
	{
		UE_LOG(LogTemp, Warning, TEXT("DynamicTexture is not valid"));
	}
	// check static mesh and material instance is valid
	if(!RuntimeHeatmapMeshComponent->GetProcMeshSection(0) || !HeatmapMaterialInstance)
	{
		return; //TODO: Add better error handling
	}
	// check if in world
	if(GetWorld() == nullptr)
	{
		return; //TODO: Add better error handling
	}
	
	DynamicTexture->InitializeTexture(TextureWidth, TextureHeight, InitialColorValue);

	// Only update and clear if we are in game mode
	if(GetWorld()->IsGameWorld())
	{
		DynamicTexture->ClearTexture();
	
		DynamicTexture->UpdateTextureRender();
	}
	
	// Assign the render target to the heatmap material instance parameter
	HeatmapMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), DynamicTexture->GetDynamicTexture());
	VoronoiMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), DynamicTexture->GetDynamicTexture());

	// Set the heatmap height displacement
	HeatmapMaterialInstance->SetScalarParameterValue(FName("HeightScale"), HeightDisplacement);
}

bool AHeatmapPixelTextureVisualizer::CheckHeatmapAndLocationValid(const FVector& AgentLocation) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Update heatmap agent locations loop check");	
	// check if the agent is withing the heatmap z bounds
	if(AgentLocation.Z <= MeshOriginLocation.Z + MaxAddHeight && MeshOriginLocation.Z <= AgentLocation.Z)
	// check if the location is higher first as in multiple floors the agent could be on a higher floor more often and will save time
	{
		return true;
	}
	else
	{
		return false;
	}

	// check if the render target and material instance is valid
	// if(!DynamicTexture && !HeatmapMaterialInstance) //TODO ==> move this to a check before we call this method and loop over all actors
	// {
	// 	return false; //TODO: Add better error handling
	// }

}

void AHeatmapPixelTextureVisualizer::UpdateHeatmap(const FVector& AgentLocation, bool bUpdateHeatmap) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Update heatmap ");
	// Check heatmap texture and location is valid
	//if (!CheckHeatmapAndLocationValid(AgentLocation)) return;
	
	FVector2D AgentsTextureLocation = ActorWorldToUV(AgentLocation);

	// Check if this is a voronoi heatmap or a standard heatmap
	if(HeatmapType)
	{
		//TODO: for now hardcode the radius as 11 - this is to be changed to a variable and be a float over integer for more precise locations
		// the color needs to be a R value of 1/6.766 = 0.1477 -- this is when we get to a density of 6.766 people per square meter it should be red
	
		// Draw a circle on the dynamic texture
		DynamicTexture->DrawCircle(AgentsTextureLocation.X, AgentsTextureLocation.Y, ScaledCircleSize, AgentColorValue);
	}
	else
	{
		// Voronoi Heatmap - this is to be implemented currently cant pass 1 point at a time
	}
	
	if(bUpdateHeatmap && HeatmapType)
	{
		// Finish plotting so update the texture
		DynamicTexture->UpdateTextureRender();
	}
}

void AHeatmapPixelTextureVisualizer::UpdateHeatmapWithMultipleAgents(const TArray<FVector>& AgentLocations)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Update heatmap agent locations with check start function");
	
	// agent count
	std::atomic<int32> ActiveAgents = 0;
	
	// Check if this is a voronoi heatmap or a standard heatmap
	if(HeatmapType == 0)
	{
		//TODO:OPTIMIZE
		TArray<FVector2d> AgentsTextureLocations;
		for (int32 i = 0; i < AgentLocations.Num(); i++)
		{
			// check heatmap texture and location is valid
			if (!CheckHeatmapAndLocationValid(AgentLocations[i])) continue;
				
			FVector2D AgentsTextureLocation = ActorWorldToUV(AgentLocations[i]);
				
			if(AgentsTextureLocation != FVector2D::Zero())
			{
				AgentsTextureLocations.Add(AgentsTextureLocation);
			}
		}
	
		// ParallelFor to speed up the filtering process
		//ParallelFor(AgentLocations.Num(), [&](int32 i)
			
		DynamicTexture->BuildVoronoiDiagram(AgentsTextureLocations);
	}
	else
	{
		if(bLiveTrackingHeatmap)
		{
			// clear the dynmaic texture
			DynamicTexture->ClearTexture(); // TODO: create a buffer that represents clear so no need to keep doing loops
		}
	
		
		
		// Update the heatmap with the agent locations
		ParallelFor(AgentLocations.Num(), [&](int32 i)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_STR("Update heatmap agent locations loop");
			if(CheckHeatmapAndLocationValid(AgentLocations[i]))
			{
				// Update Agent Count with the number of agents being rendered
				ActiveAgents.fetch_add(1, std::memory_order_relaxed);
				
				UpdateHeatmap(AgentLocations[i], false);
			}
			//UpdateHeatmap(AgentLocations[i], false);
		}, EParallelForFlags::BackgroundPriority); // as this parallel for is updating the texture we need to make sure it is done on the rendering thread and it calls process thread when idle 
	
		//DynamicTexture->ConvertTextureToRGBTexture();
	
		
		// if there is no agents then no blurring is needed
		if (AgentLocations.Num()>0)
		{
			DynamicTexture->OpenCVGaussianBlur();
		}
		
		//DynamicTexture->ConvertTextureToRGBTexture();
	}
	// TODO: this convert not needed as was part of user study test -> it should be converted into a material logic not cpu logic
	//DynamicTexture->ConvertTextureToRGBTexture();
	// Finish plotting so update the texture
	DynamicTexture->UpdateTextureRender();
	
	// Update Agent Count with the number of agents being rendered
	NumberOfAgentsOnHeatmap = ActiveAgents.load();
	
}

void AHeatmapPixelTextureVisualizer::UpdateHeatmapWithMultipleAgents_NoCheck(const TArray<FVector>& AgentLocations)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Update heatmap agent locations no check start function");
	// check if the agent locations are empty
	if(AgentLocations.Num() == 0)
	{
		return;
	}

	// agent count
	std::atomic<int32> ActiveAgents = 0;
	
	// Check if this is a voronoi heatmap or a standard heatmap
	if(HeatmapType == 0)
	{
		//TODO:OPTIMIZE
		TArray<FVector2d> AgentsTextureLocations;
		for (int32 i = 0; i < AgentLocations.Num(); i++)
		{
			FVector2D AgentsTextureLocation = ActorWorldToUV(AgentLocations[i]);
				
			AgentsTextureLocations.Add(AgentsTextureLocation); // removed zero vector check
		}

		// ParallelFor to speed up the filtering process
		//ParallelFor(AgentLocations.Num(), [&](int32 i)
			
		DynamicTexture->BuildVoronoiDiagram(AgentsTextureLocations);
	}
	else
	{
		if(bLiveTrackingHeatmap)
		{
			// clear the dynmaic texture
			DynamicTexture->ClearTexture(); // TODO: create a buffer that represents clear so no need to keep doing loops
		}

		
		
		// Update the heatmap with the agent locations
		ParallelFor(AgentLocations.Num(), [&](int32 i)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_STR("Update heatmap agent locations loop no check");
			// Update Agent Count with the number of agents being rendered
			ActiveAgents.fetch_add(1, std::memory_order_relaxed);
			
			UpdateHeatmap(AgentLocations[i], false);
			//UpdateHeatmap(AgentLocations[i], false);
		}, EParallelForFlags::BackgroundPriority); // as this parallel for is updating the texture we need to make sure it is done on the rendering thread and it calls process thread when idle 

		//DynamicTexture->ConvertTextureToRGBTexture();

		
		// if there is no agents then no blurring is needed
		if (AgentLocations.Num()>0)
		{
			DynamicTexture->OpenCVGaussianBlur();
		}
		
		//DynamicTexture->ConvertTextureToRGBTexture();
	}
	// TODO: this convert not needed as was part of user study test -> it should be converted into a material logic not cpu logic
	//DynamicTexture->ConvertTextureToRGBTexture();
	// Finish plotting so update the texture
	DynamicTexture->UpdateTextureRender();

	// Update Agent Count with the number of agents being rendered
	NumberOfAgentsOnHeatmap = ActiveAgents.load();
}

void AHeatmapPixelTextureVisualizer::UpdateHeatmapAgentCount(const TArray<FVector>& AgentLocations)
{
	// check if the agent locations are empty
	if(AgentLocations.Num() == 0)
	{
		return;
	}

	// agent count
	std::atomic<int32> ActiveAgents = 0;
	
	// Update the heatmap with the agent locations
	ParallelFor(AgentLocations.Num(), [&](int32 i)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Update heatmap agent locations loop no check");
		// Update Agent Count with the number of agents being rendered
		ActiveAgents.fetch_add(1, std::memory_order_relaxed);
		
	}, EParallelForFlags::BackgroundPriority); // as this parallel for is updating the texture we need to make sure it is done on the rendering thread and it calls process thread when idle 

	// Update Agent Count with the number of agents being rendered
	NumberOfAgentsOnHeatmap = ActiveAgents.load();
}

FVector2D AHeatmapPixelTextureVisualizer::ActorWorldToUV(const FVector& EntityWorldLocation) const
{
	// Convert world location to a relative position based on the mesh origin (which is the bottom left)
	FVector RelativeLocation = EntityWorldLocation - MeshOriginLocation;
    
	// Scale the relative position into UV pixel space using your precomputed UVScale.
	// This converts world units to texture pixel units.
	FVector2D UV;
	UV.X = RelativeLocation.X * UVScale.X;
	UV.Y = RelativeLocation.Y * UVScale.Y;
    
	// Convert the pixel-space UV to normalized [0,1] space by dividing by the texture dimensions.
	float NormX = UV.X / TextureWidth;
	float NormY = UV.Y / TextureHeight;
    
	// Determine which dimension of the mesh is larger.
	// If the mesh is wider than tall, we need to compress the Y coordinate.
	bool bAdjustY = (HeatmapMeshSize2D.X >= HeatmapMeshSize2D.Y);
	// Compute the ratio as the smaller mesh dimension divided by the larger.
	float ratio = bAdjustY ? (HeatmapMeshSize2D.Y / HeatmapMeshSize2D.X) 
						   : (HeatmapMeshSize2D.X / HeatmapMeshSize2D.Y);
    
	// Apply a center-based aspect ratio correction about the midpoint (0.5, 0.5) in normalized space.
	if (bAdjustY)
	{
		NormY = (NormY - 0.5f) * ratio + 0.5f;
	}
	else
	{
		NormX = (NormX - 0.5f) * ratio + 0.5f;
	}
    
	// Convert the corrected normalized UVs back to pixel space.
	UV.X = NormX * TextureWidth;
	UV.Y = NormY * TextureHeight;
    
	return UV;
}

void AHeatmapPixelTextureVisualizer::UpdateHeatmapTextureRender() const
{
	DynamicTexture->UpdateTextureRender();
}

void AHeatmapPixelTextureVisualizer::ClearTexture()
{
	DynamicTexture->ClearTexture();
}

void AHeatmapPixelTextureVisualizer::UpdateMeshSize(const FVector2D& NewMeshSize)
{
	// check if the mesh is valid
	if(!RuntimeHeatmapMeshComponent->GetProcMeshSection(0))
	{
		return;
	}
	
	// update the mesh size
	HeatmapMeshSize2D = NewMeshSize;

	// update the mesh vertices
	MeshVertices.Empty();
	MeshVertices.Add(FVector(0, 0, 0));
	MeshVertices.Add(FVector(0, HeatmapMeshSize2D.Y, 0));
	MeshVertices.Add(FVector(HeatmapMeshSize2D.X, HeatmapMeshSize2D.Y, 0));
	MeshVertices.Add(FVector(HeatmapMeshSize2D.X, 0, 0));

	// update the mesh
	RuntimeHeatmapMeshComponent->UpdateMeshSection(0, MeshVertices, TArray<FVector>(), MeshUVs, TArray<FColor>(), TArray<FProcMeshTangent>());
}

void AHeatmapPixelTextureVisualizer::UpdateHeatmapType(bool bIsStandardHeatmap, bool bIsLiveTrackingNeeded)
{
	if(bIsStandardHeatmap)
	{
		HeatmapType = 1;
		
	}
	else
	{
		HeatmapType = 0;
	}

	AssignMaterialInstanceToMesh();

	// Set the live tracking flag
	bLiveTrackingHeatmap = bIsLiveTrackingNeeded;

	// Voronoi has no height displacement
	if(bIsStandardHeatmap)
	{
		// Set the heatmap height displacement
		HeatmapMaterialInstance->SetScalarParameterValue(FName("HeightScale"), HeightDisplacement);
	}

	// We clear the texture so no residual color is left
	ClearTexture();
	
	// Update the heatmap texture render
	UpdateHeatmapTextureRender();
}

void AHeatmapPixelTextureVisualizer::UpdateHeatmapMeshBounds()
{
	// Setup mesh transform
	MeshTransform = RuntimeHeatmapMeshComponent->GetComponentTransform();

	// Set the world location of the mesh origin
	MeshOriginLocation = MeshTransform.GetLocation();

	// calculate the scale of the uv - this is so if we use a texture to the power of 2 and size that is not we can the location to the texture accordingly
	UVScale = FVector2D(TextureWidth / HeatmapMeshSize2D.X, TextureHeight / HeatmapMeshSize2D.Y);
	
	// Using the UV scale, calculate the circle size
	ScaledCircleSize = CircleRadius * FMath::Min(UVScale.X, UVScale.Y);
	UE_LOG(LogTemp, Log, TEXT("UVScale: (%f, %f), ScaledCircleSize: %d"), UVScale.X, UVScale.Y, ScaledCircleSize);

}

void AHeatmapPixelTextureVisualizer::BuildGridMeshPlane(const FVector2D& MeshSize, bool bIsStandardHeatmap)
{
	// Clear mesh section
	RuntimeHeatmapMeshComponent->ClearMeshSection(0);

	FIntPoint NumTriangles = FIntPoint(2);// if the heatmap is standard we only need 2 triangles (till we get to clipping)

	if(!bIsStandardHeatmap)
	{
		// Calculate the number of triangles
		NumTriangles = FIntPoint(MeshSize.X / 25, MeshSize.Y / 25);
	}
	
	// Clear any existing vertices, UVs and triangles
	MeshVertices.Empty();
	MeshUVs.Empty();
	MeshTriangles.Empty();

	// Because grid mesh building is a heavy task we need to do it off the game thread
	AsyncTask(ENamedThreads::AnyThread, [this, NumTriangles, MeshSize, bIsStandardHeatmap]()
	{
		UKismetProceduralMeshLibrary::CreateGridMeshWelded(NumTriangles.X, NumTriangles.Y, MeshTriangles, MeshVertices, MeshUVs, 25);

		// Update the mesh
		RuntimeHeatmapMeshComponent->CreateMeshSection(0, MeshVertices, MeshTriangles, TArray<FVector>(), MeshUVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	});
	
}

void AHeatmapPixelTextureVisualizer::UpdateHeatmapCVDSettings(EColorVisionDeficiency ColourDeficiency,
                                                              float DeficiencyLevel, bool bCorrectDeficiency, bool bSimulateColourCorrectionWithDeficiency)
{
	DynamicTexture->UpdateHeatmapCVDSettings(ColourDeficiency, DeficiencyLevel, bCorrectDeficiency, bSimulateColourCorrectionWithDeficiency);
}

void AHeatmapPixelTextureVisualizer::SaveHeatmapToPNG() const
{
	// File name is name + Created date time + .png
	FString SafeTimestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString FileName = FString::Printf(TEXT("Heatmap/%s_%s.png"), *ActorName, *SafeTimestamp);
	DynamicTexture->SaveDynamicTextureToPNG(FileName);
}

void AHeatmapPixelTextureVisualizer::SaveHeatmapToPNG(const FString& CurrentTimeString) const
{
	// File name is name + Created date time + .png
	FString SafeTimestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString FileName = TEXT("Heatmap/") + ActorName + TEXT("_SimTime_") +
		CurrentTimeString + TEXT("_Created_") + *SafeTimestamp + TEXT(".png");
	DynamicTexture->SaveDynamicTextureToPNG(FileName);
}

FVector2d AHeatmapPixelTextureVisualizer::GenerateSquareCellSize(const FIntPoint& NumberOfTriangles,
                                                                 const FVector2D& MeshSize)
{
	FVector2D CellSize(0, 0);

	CellSize.X = MeshSize.X / NumberOfTriangles.X;
	CellSize.Y = MeshSize.Y / NumberOfTriangles.Y;

	return CellSize;
}

FIntPoint AHeatmapPixelTextureVisualizer::CalculateNumberOfTriangles(const FVector2D& MeshSize,
                                                                     const FIntPoint& TextureSize)
{
	FIntPoint NumTriangles(0, 0);

	// in the case of 3d heatmaps we need to calculate the number of triangles needed
	// ue will give the mesh size in cm so we need to make a resolution of 4 triangles per meter - this is a good resolution for a heatmap

	NumTriangles.X = FMath::CeilToInt32(MeshSize.X / 25); // TODO: working in meters so 100 is 1 meter so divide by 25 to get 4 triangles per meter
	NumTriangles.Y = FMath::CeilToInt32(MeshSize.Y / 25);
	//TODO: this is a fix triangle size based on being to a 10 tenth of the mesh  size - this is as resolution goes up so does the number of triangles required

	return NumTriangles;
}

void AHeatmapPixelTextureVisualizer::CreateMeshVertexsAndUVs(const FIntPoint NumTriangles, const FVector2D CellSize)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Generate Mesh Vertices and UVs");
	// Clear any existing vertices and UVs
	MeshVertices.Empty();
	MeshUVs.Empty();

	// Clear any existing vertices and UVs
	// Assume the texture is square. If the mesh is wider than tall, adjust the Y UVs.
	bool bAdjustY = HeatmapMeshSize2D.X >= HeatmapMeshSize2D.Y;
	// The aspect ratio here is the ratio of the smaller dimension to the larger.
	float AspectRatio = bAdjustY ? (HeatmapMeshSize2D.Y / HeatmapMeshSize2D.X) : (HeatmapMeshSize2D.X / HeatmapMeshSize2D.Y);
	
	// Generate vertices and UVs
	for (int32 y = 0; y < NumTriangles.Y; y++)
	{
		for (int32 x = 0; x < NumTriangles.X; x++)
		{
			// Create vertex position (Z is fixed to 0.1)
			FVector Vertex = FVector(x * CellSize.X, y * CellSize.Y, 0.1f);
            
			// Base UV coordinates mapped over the full [0,1] range
			float UVx = static_cast<float>(x) / (NumTriangles.X - 1);
			float UVy = static_cast<float>(y) / (NumTriangles.Y - 1);

			// Adjust the UVs to account for non-square mesh dimensions:
			if (bAdjustY)
			{
				// For a wider-than-tall mesh, scale the Y component.
				// This makes the effective vertical UV range equal to the mesh's aspect ratio.
				// The offset centers the texture vertically.
				UVy = UVy * AspectRatio + (1.0f - AspectRatio) * 0.5f;
			}
			else
			{
				// For a taller-than-wide mesh, you could similarly adjust UVx:
				UVx = UVx * AspectRatio + (1.0f - AspectRatio) * 0.5f;
			}
			
			FVector2d UV(UVx, UVy);
			MeshVertices.Add(Vertex);
			MeshUVs.Add(UV);
		}
	}
}

void AHeatmapPixelTextureVisualizer::GenerateMeshTrianglesInQuadMapping(const FIntPoint NumTriangles, TArray<FBox3d> Quads)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Generate Triangles in Quad Mapping");
	// Clear any existing triangles
	MeshTriangles.Empty();
	
	for (int32 y = 0; y < NumTriangles.Y - 1; y++)
	{
		for (int32 x = 0; x < NumTriangles.X - 1; x++)
		{
			// Calculate the 6 indices for the 2 triangles
			int32 Index0 = x + y * NumTriangles.X;
			int32 Index1 = Index0 + NumTriangles.X;
			int32 Index2 = Index0 + 1;
			int32 Index3 = Index1;
			int32 Index4 = Index1 + 1;
			int32 Index5 = Index2;

			
			if(Quads.Num()>0)
			{
				// loop over the quads and see if the triangle is within the quad
				for(FBox3d Quad : Quads)
				{
					// as the vertices are in local space we need to convert to global space
					FVector Vert_0 = MeshVertices[Index0] + MeshOriginLocation;
					FVector Vert_1 = MeshVertices[Index1] + MeshOriginLocation;
					FVector Vert_2 = MeshVertices[Index2] + MeshOriginLocation;
					FVector Vert_3 = MeshVertices[Index3] + MeshOriginLocation;
					
					// check if triangle vertices are within or on the quad bounds
					if(Quad.IsInsideOrOn(Vert_0) || Quad.IsInsideOrOn(Vert_1) ||
						Quad.IsInsideOrOn(Vert_2) || Quad.IsInsideOrOn(Vert_3))
					{
						// Add the triangle indices - As we want uniform boxes we will say that the quad is valid
						MeshTriangles.Add(Index0);
						MeshTriangles.Add(Index1);
						MeshTriangles.Add(Index2);
						MeshTriangles.Add(Index3);
						MeshTriangles.Add(Index4);
						MeshTriangles.Add(Index5);
						break; // found a valid quad so break
					}
				}
			}
			else
			{
				// Should have a valid quad but for now we will just add the triangles

				// Add the triangle indices
				MeshTriangles.Add(Index0);
				MeshTriangles.Add(Index1);
				MeshTriangles.Add(Index2);
				MeshTriangles.Add(Index3);
				MeshTriangles.Add(Index4);
				MeshTriangles.Add(Index5);
			}
			
		}
	}
	// Log the number of triangles
	UE_LOG(LogTemp, Warning, TEXT("Number of Triangles: %d"), MeshTriangles.Num());
}

void AHeatmapPixelTextureVisualizer::GenerateMeshVerticesUVsAndTriangles(const FVector2D& MeshSize,
                                                                         const FIntPoint& TextureSize, bool bIs3DHeatmap)
{
	// Update the mesh size
	HeatmapMeshSize2D = MeshSize;
	
	// clear the previous mesh section
	RuntimeHeatmapMeshComponent->ClearMeshSection(0);

	// Number of required triangles
	FIntPoint NumTriangles = FIntPoint(MeshSize.X / 250, MeshSize.Y / 250);

	if(bIs3DHeatmap)
	{
		// Calculate the number of triangles
		NumTriangles = FIntPoint(MeshSize.X / 25, MeshSize.Y / 25);
	}
	// Generate the square cell size
	FVector2D CellSize = GenerateSquareCellSize(NumTriangles, MeshSize);

	ARuntimeMeshBuilder* MeshBuilder = nullptr;
	// if in world get the runtime mesh builder so we can query the mesh for the triangles
	if(World)
	{
		// get actors
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(World, ARuntimeMeshBuilder::StaticClass(), FoundActors);
	 
		if(FoundActors.Num() >0)
		{
			// get the first actor
			MeshBuilder = Cast<ARuntimeMeshBuilder>(FoundActors[0]);
		}
	}

	
	Async(EAsyncExecution::ThreadPool, [this, NumTriangles, CellSize, MeshBuilder]()
	      {
		      //TODO: ADD A EMPTY AND NULL CHECKS
		
		      // Generate the quads to restrict the triangle generation to areas needed
		      TArray<FBox3d> Quads = FindAllQuads(MeshBuilder);
	
		      // Generate the vertices and UVs
		      CreateMeshVertexsAndUVs(NumTriangles, CellSize);
	
		      // Generate the Triangles for this square
		      GenerateMeshTrianglesInQuadMapping(NumTriangles, Quads);
	
	
	      },
	      [this]
	      {
		      AsyncTask(ENamedThreads::GameThread, [this]()
		      {
			      // Generate the mesh section
			      RuntimeHeatmapMeshComponent->CreateMeshSection_LinearColor(0, MeshVertices, MeshTriangles, TArray<FVector>(), MeshUVs,  TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		      });
	      });
	

	
	
	// // Generate the quads to restrict the triangle generation to areas needed
	// TArray<FBox3d> Quads = FindAllQuads(MeshBuilder);
	//
	// // Generate the vertices and UVs
	// CreateMeshVertexsAndUVs(NumTriangles, CellSize);
	// 	
	// // Generate the Triangles for this square
	// GenerateMeshTrianglesInQuadMapping(NumTriangles, Quads);
	//
	// // Generate the mesh section
	// RuntimeHeatmapMeshComponent->CreateMeshSection_LinearColor(0, MeshVertices, MeshTriangles, TArray<FVector>(), MeshUVs,  TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	
}

TArray<FBox3d> AHeatmapPixelTextureVisualizer::FindAllQuads(ARuntimeMeshBuilder* MeshBuilder) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Find All Quads");
	TArray<FBox3d> Quads;
	
	// Our starting pos is spawn point
	FVector StartPos = GetActorLocation();

	// Our cube march step size need to be the same as the desired quad size
	float StepSize = 650.0f; // TODO: This is a hardcoded value for now for demo 500 ensures the building is covered - its too large for the mesh and has overflow around the edges 
	// bounding box to use for the cube march
	FBox3d MarchingBox = FBox3d(FVector(StartPos.X, StartPos.Y, StartPos.Z -(125.0f)), FVector(StartPos.X + StepSize, StartPos.Y + StepSize, StartPos.Z +(125.0f)));
	// Confirm the box is correct, draw debug box
	//DrawDebugBox(GetWorld(), MarchingBox.GetCenter(), MarchingBox.GetExtent(), FColor::Red, false, 1000.0f, 0, 1.0f);
	
	// The Whole mesh bounds
	FBox3d MeshBounds = FBox3d(FVector(StartPos.X, StartPos.Y, 0.0f), FVector(StartPos.X+HeatmapMeshSize2D.X , StartPos.Y + HeatmapMeshSize2D.Y , 500.0f));

	//DrawDebugBox(GetWorld(), MeshBounds.GetCenter(), MeshBounds.GetExtent(), FColor::Blue, false, 1000.0f, 0, 4.0f);
	
	if(MeshBuilder)
	{
		UE_LOG(LogTemp, Warning, TEXT("Starting Mesh Triangle Generation"));
		// create size for array
		TArray<FVector> ValidVertices;

		// if the mesh builder is using datasmith then we need to loop over all the meshes and get the vertices
		if (MeshBuilder->bIsDatasmithAsset)
		{
			// get all the meshes from the datasmith anchor
			for (auto& Tuple : MeshBuilder->GetDatasmithMaterialsMap())
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
				
				// get the local to world transform
				FTransform LocalToWorldTransform = Mesh->GetComponentTransform();
		
				// Get the vertices from the mesh
				FPositionVertexBuffer& VertexBuffer = Mesh->GetStaticMesh()->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer;
				
				for(uint32 Index = 0;  Index < VertexBuffer.GetNumVertices(); Index++)
				{
					// Get the local vertex
					FVector3f LocalVertex = VertexBuffer.VertexPosition(Index);
			
					// Transform the local vertex to world space
					FVector WorldVertex = LocalToWorldTransform.TransformPosition(FVector(LocalVertex));

					// while working out the algorithm to work out the mesh perimeter we will just loop over vertices that have a z value of 0 +/- 100
					if(WorldVertex.Z <= StartPos.Z + StepSize && WorldVertex.Z >= StartPos.Z - StepSize)
					{
						FVector FVertex = FVector(WorldVertex.X, WorldVertex.Y, WorldVertex.Z);
						
						ValidVertices.Add(FVertex);
					}
				}
				
			}

			// Log how many vertices found
			UE_LOG(LogTemp, Warning, TEXT("Found Vertices: %d"), ValidVertices.Num());
		}
		else // is a procedural mesh
		{
			// loop over the mesh vertices
			for(FProcMeshVertex VertexStruct : MeshBuilder->MobiusProceduralMeshComponent->GetProcMeshSection(0)->ProcVertexBuffer)
			{
				// while working out the algorithm to work out the mesh perimeter we will just loop over vertices that have a z value of 0 +/- 100
				if(VertexStruct.Position.Z <= StartPos.Z + StepSize && VertexStruct.Position.Z >= StartPos.Z - StepSize)
				{
					ValidVertices.Add(VertexStruct.Position);
				}
			}
		}
		
		// loop over the marching box dimensions with the step size
		for(int32 x = 0; x <= MeshBounds.Max.X *2 + StepSize*2; x+=StepSize)
		{
			for(int32 y = 0; y <= MeshBounds.Max.Y*2+ StepSize*2; y+=StepSize)
			{
				// Define the marching box
				MarchingBox = FBox3d(FVector(StartPos.X + x, StartPos.Y + y, MarchingBox.Min.Z), FVector(StartPos.X + x + StepSize, StartPos.Y + y + StepSize, MarchingBox.Max.Z));
				//DrawDebugBox(GetWorld(), MarchingBox.GetCenter(), MarchingBox.GetExtent(), FColor::Black, false, 1000.0f, 0, 1.0f);
				
				// loop over the mesh vertices
				for(FVector VertexStruct : ValidVertices)
				{
					if(MarchingBox.IsInsideOrOn(VertexStruct))
					{
						// Add the box to the array
						Quads.Add(MarchingBox);
						//DrawDebugBox(GetWorld(), MarchingBox.GetCenter(), MarchingBox.GetExtent(), FColor::Green, false, 1000.0f, 0, 5.0f);
						//UE_LOG(LogTemp, Warning, TEXT("Found a vertex in the box"));
						//FoundVerts++;
						break;// we found one so need to keep looking
					}
				}
				
			}
		}
		// Log how many boxes found
		UE_LOG(LogTemp, Warning, TEXT("Found Quads: %d"), Quads.Num());
	}

	return Quads;
	
	
	
	// TODO: Too many loops - need to refactor
	
	 
	
	 
	
}
