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

#include "Actors/HeatmapPixelTextureVisualizer.h"
#include "Subsystems/HeatmapSubsystem.h"

#include "DynamicPixelRenderingTexture.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h" // has helper functions for procedural meshes
#include "GeomTools.h"// has helper functions for geometry
#include "Kismet/GameplayStatics.h"
#include "BuildingGenerator/RuntimeMeshBuilder.h"
#include "DatasmithRuntime.h"
#include "StaticMeshResources.h" // used for accessing vertex buffers on static meshes
#include "Rendering/PositionVertexBuffer.h" 
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Subsystems/MobiusCustomLoggerSubsystem.h"
#include "Subsystems/TimeDilationSubSystem.h"
#include "Diagnostics/TrajectoryCaptureRecorder.h"
#include "Engine/Engine.h"
#include "HAL/PlatformTime.h"


// Sets default values
AHeatmapPixelTextureVisualizer::AHeatmapPixelTextureVisualizer() :
	RuntimeHeatmapMeshComponent(nullptr),
	TextureWidth(1),
	TextureHeight(1),
	HeightDisplacement(0),
	ActorName("HeatmapPixelTextureVisualizer"),
	bLiveTrackingHeatmap(true),
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
	
	// Aggregate bounds across every section so the dense tiled path (N sections) reports whole-mesh size,
	// not a single tile's bounds.
	if (RuntimeHeatmapMeshComponent->GetNumSections() > 0)
	{
		FBox Agg(ForceInit);
		const int32 NumSections = RuntimeHeatmapMeshComponent->GetNumSections();
		for (int32 i = 0; i < NumSections; ++i)
		{
			if (const FProcMeshSection* Sec = RuntimeHeatmapMeshComponent->GetProcMeshSection(i))
			{
				Agg += Sec->SectionLocalBox;
			}
		}
		if (Agg.IsValid)
		{
			HeatmapMeshSize2D = FVector2D(Agg.GetSize().X, Agg.GetSize().Y);
		}
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
	if (!RuntimeHeatmapMeshComponent)
	{
		return;
	}
	// Trajectory mode only replaces the banded surface. The voronoi material has no band chain at all —
	// it saturates the raw channel — so it needs no trajectory variant and stays as-is.
	UMaterialInstanceDynamic* Target = HeatmapType ? HeatmapMaterialInstance.Get() : VoronoiMaterialInstance.Get();
	if (bTrajectoryHeatmap && HeatmapType && TrajectoryMaterialInstance)
	{
		Target = TrajectoryMaterialInstance.Get();
	}
	if (!Target)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Heatmap Setup Error"),
				FText::FromString(HeatmapType ? "Heatmap material missing" : "Voronoi material missing"),
				FText::FromString(HeatmapType ? "Heatmap material instance is not available." : "Voronoi material instance is not available."),
				FText::FromString("HeatmapPixelTextureVisualizer"));
		}
		return;
	}
	// Tiled dense path emits N sections; apply MID to every one so batched draws share the same instance.
	const int32 NumSections = RuntimeHeatmapMeshComponent->GetNumSections();
	for (int32 i = 0; i < NumSections; ++i)
	{
		RuntimeHeatmapMeshComponent->SetMaterial(i, Target);
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

	// Same graph as the standard heatmap, but with the five band edges lifted out of the custom node's
	// HLSL into scalar parameters. The trajectory surface measures passage count, not Fruin density, so
	// it cannot use the density edges baked into M_HeatmapRT_V2 — see FHeatmapLOSBands.
	UMaterialInterface* TrajectoryMaterial = LoadObject<UMaterial>(
		nullptr, TEXT(
			"Material'/Game/01_Dev/NickMaster/Heatmaps/Materials/RenderTargetHeatmaps/M_HeatmapRT_Trajectory.M_HeatmapRT_Trajectory'"));


	// Assign the materials to the instance
	// Heatmap Instance Material - by checking name we avoid renaming existing instances which is not allowed
	const FString HeatmapInstanceName = ActorName + "HeatmapMaterialInstance";
	if(!HeatmapMaterialInstance || HeatmapMaterialInstance->GetName() != HeatmapInstanceName)
	{
		if (!HeatmapMaterial)
		{
			if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
			{
				Feedback->ReportError(
					FText::FromString("Heatmap Setup Error"),
					FText::FromString("Heatmap material missing"),
					FText::FromString("Failed to load the heatmap material asset."),
					FText::FromString("HeatmapPixelTextureVisualizer"));
			}
			return;
		}
		HeatmapMaterialInstance = UMaterialInstanceDynamic::Create(HeatmapMaterial, this, FName(*(ActorName + "HeatmapMaterialInstance")));
	}
	const FString VoronoiInstanceName = ActorName + "VoronoiMaterialInstance";
	if(!VoronoiMaterialInstance || VoronoiMaterialInstance->GetName() != VoronoiInstanceName)
	{
		if (!VoronoiMaterial)
		{
			if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
			{
				Feedback->ReportError(
					FText::FromString("Heatmap Setup Error"),
					FText::FromString("Voronoi material missing"),
					FText::FromString("Failed to load the voronoi material asset."),
					FText::FromString("HeatmapPixelTextureVisualizer"));
			}
			return;
		}
		VoronoiMaterialInstance = UMaterialInstanceDynamic::Create(VoronoiMaterial, this, FName(*(ActorName + "VoronoiMaterialInstance")));
	}
	const FString TrajectoryInstanceName = ActorName + "TrajectoryMaterialInstance";
	if(!TrajectoryMaterialInstance || TrajectoryMaterialInstance->GetName() != TrajectoryInstanceName)
	{
		if (!TrajectoryMaterial)
		{
			if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
			{
				Feedback->ReportError(
					FText::FromString("Heatmap Setup Error"),
					FText::FromString("Trajectory material missing"),
					FText::FromString("Failed to load the trajectory heatmap material asset."),
					FText::FromString("HeatmapPixelTextureVisualizer"));
			}
			return;
		}
		TrajectoryMaterialInstance = UMaterialInstanceDynamic::Create(TrajectoryMaterial, this, FName(*(ActorName + "TrajectoryMaterialInstance")));
	}
	ApplyTrajectoryLOSBands();
}

void AHeatmapPixelTextureVisualizer::ApplyTrajectoryLOSBands()
{
	if (TrajectoryMaterialInstance)
	{
		TrajectoryMaterialInstance->SetScalarParameterValue(FName("LOS_A_Band"), TrajectoryLOSBands.BandA);
		TrajectoryMaterialInstance->SetScalarParameterValue(FName("LOS_B_Band"), TrajectoryLOSBands.BandB);
		TrajectoryMaterialInstance->SetScalarParameterValue(FName("LOS_C_Band"), TrajectoryLOSBands.BandC);
		TrajectoryMaterialInstance->SetScalarParameterValue(FName("LOS_D_Band"), TrajectoryLOSBands.BandD);
		TrajectoryMaterialInstance->SetScalarParameterValue(FName("LOS_E_Band"), TrajectoryLOSBands.BandE);
	}

	// The PNG export colourises on the CPU. Give it the same edges or the saved image and the in-world
	// render disagree — which is exactly the discrepancy that hid this bug in the first place.
	if (TrajectoryAccumulationTexture)
	{
		TrajectoryAccumulationTexture->SetLOSBands(TrajectoryLOSBands);
	}
}

void AHeatmapPixelTextureVisualizer::SetupDynamicTexture()
{
	// check texture is valid
	if(!DynamicTexture)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Heatmap Setup Error"),
				FText::FromString("Dynamic texture missing"),
				FText::FromString("Dynamic texture component is not available."),
				FText::FromString("HeatmapPixelTextureVisualizer"));
		}
		UE_LOG(LogTemp, Warning, TEXT("DynamicTexture is not valid"));
		return;
	}
	// check static mesh and material instance is valid (any section will do — dense path may emit N)
	if(!RuntimeHeatmapMeshComponent || RuntimeHeatmapMeshComponent->GetNumSections() == 0 || !HeatmapMaterialInstance || !VoronoiMaterialInstance)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Heatmap Setup Error"),
				FText::FromString("Heatmap resources missing"),
				FText::FromString("Heatmap mesh or material instances are not ready."),
				FText::FromString("HeatmapPixelTextureVisualizer"));
		}
		return;
	}
	// check if in world
	if(GetWorld() == nullptr)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Heatmap Setup Error"),
				FText::FromString("World not available"),
				FText::FromString("Cannot initialize the heatmap without a valid world."),
				FText::FromString("HeatmapPixelTextureVisualizer"));
		}
		return;
	}
	
	DynamicTexture->InitializeTexture(TextureWidth, TextureHeight, InitialColorValue);
	if (!TrajectoryAccumulationTexture)
	{
		TrajectoryAccumulationTexture = NewObject<UDynamicPixelRenderingTexture>(this, TEXT("TrajectoryAccumulationTexture"));
	}
	TrajectoryAccumulationTexture->InitializeTexture(TextureWidth, TextureHeight, InitialColorValue);
	ApplyTrajectoryLOSBands();

	// Only update and clear if we are in game mode
	if(GetWorld()->IsGameWorld())
	{
		DynamicTexture->ClearTexture();
		TrajectoryAccumulationTexture->ClearTexture();
	
		DynamicTexture->UpdateTextureRender();
	}
	
	// Assign the render target to the heatmap material instance parameter
	HeatmapMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), DynamicTexture->GetDynamicTexture());
	VoronoiMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), DynamicTexture->GetDynamicTexture());

	// Set the heatmap height displacement
	HeatmapMaterialInstance->SetScalarParameterValue(FName("HeightScale"), HeightDisplacement);
	if (TrajectoryMaterialInstance)
	{
		// Point the trajectory instance at its own accumulation buffer, not the density one.
		TrajectoryMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), TrajectoryAccumulationTexture->GetDynamicTexture());
		TrajectoryMaterialInstance->SetScalarParameterValue(FName("HeightScale"), HeightDisplacement);
	}
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
}

void AHeatmapPixelTextureVisualizer::UpdateHeatmap(const FVector& AgentLocation, bool bUpdateHeatmap) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Update heatmap ");
	if (!DynamicTexture)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Heatmap Update Error"),
				FText::FromString("Dynamic texture missing"),
				FText::FromString("Heatmap texture is not available for updates."),
				FText::FromString("HeatmapPixelTextureVisualizer"));
		}
		return;
	}
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
	if (!DynamicTexture)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Heatmap Update Error"),
				FText::FromString("Dynamic texture missing"),
				FText::FromString("Heatmap texture is not available for updates."),
				FText::FromString("HeatmapPixelTextureVisualizer"));
		}
		return;
	}

	if (bTrajectoryHeatmap)
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

void AHeatmapPixelTextureVisualizer::UpdateHeatmapWithTrajectorySegments(const TArray<FHeatmapTrajectorySegment>& Segments)
{
	if (!DynamicTexture || !TrajectoryAccumulationTexture || !bTrajectoryHeatmap || Segments.IsEmpty())
	{
		return;
	}

	FLinearColor PathColor = AgentColorValue * TrajectorySampleWeight;

#if !UE_BUILD_SHIPPING
	FTrajectoryCaptureRecorder& Capture = FTrajectoryCaptureRecorder::Get();
	const bool bCapturing = Capture.IsTargetFloor(FloorID);
	float CaptureSimTime = 0.0f;
	if (bCapturing)
	{
		if (const UWorld* CaptureWorld = GetWorld())
		{
			if (const UTimeDilationSubSystem* Time = CaptureWorld->GetSubsystem<UTimeDilationSubSystem>())
			{
				CaptureSimTime = Time->GetCurrentSimTime();
			}
		}
	}
#endif

	for (const FHeatmapTrajectorySegment& Segment : Segments)
	{
		const float Length = FVector::Dist(Segment.Start, Segment.End);
		if (Length <= KINDA_SMALL_NUMBER)
		{
#if !UE_BUILD_SHIPPING
			// Degenerate segments are recorded as not-drawn so the raster stream reconciles 1:1 with
			// the filter stream; an unexplained row count difference would otherwise look like loss.
			if (bCapturing)
			{
				Capture.RecordRaster(CaptureSimTime, FloorID, Segment.Start, Segment.End,
					FIntPoint(-1, -1), FIntPoint(-1, -1), /*bDrawn*/ false);
			}
#endif
			continue;
		}

		const FVector2D StartTexturePoint = ActorWorldToUV(Segment.Start);
		const FVector2D EndTexturePoint = ActorWorldToUV(Segment.End);
		const int32 StartX = FMath::Clamp(FMath::RoundToInt(StartTexturePoint.X), 0, TextureWidth - 1);
		const int32 StartY = FMath::Clamp(FMath::RoundToInt(StartTexturePoint.Y), 0, TextureHeight - 1);
		const int32 EndX = FMath::Clamp(FMath::RoundToInt(EndTexturePoint.X), 0, TextureWidth - 1);
		const int32 EndY = FMath::Clamp(FMath::RoundToInt(EndTexturePoint.Y), 0, TextureHeight - 1);

		// A raster line has no sampling gaps: every pixel traversed by an agent segment receives
		// one additive count. The first visit is seeded at the palette's visible floor because the
		// material derives its output alpha from the red channel and otherwise hides low values.
		TrajectoryAccumulationTexture->DrawLineWithMinimumRed(StartX, EndX, StartY, EndY, PathColor,
			TrajectoryMinimumVisibleValue, TrajectoryLineBrushRadius);

#if !UE_BUILD_SHIPPING
		if (bCapturing)
		{
			// Post-clamp texels: if a segment's world position falls outside the mesh, this is where
			// it silently collapses onto the texture edge, and only the recorded texels reveal it.
			Capture.RecordRaster(CaptureSimTime, FloorID, Segment.Start, Segment.End,
				FIntPoint(StartX, StartY), FIntPoint(EndX, EndY), /*bDrawn*/ true);
		}
#endif
	}

	// The trajectory material binds directly to this raw texture while the mode is active.
	// No copy, blur, or other post-process can alter the sampled path values.
	TrajectoryAccumulationTexture->UpdateTextureRender();
}

#if !UE_BUILD_SHIPPING
FIntPoint AHeatmapPixelTextureVisualizer::WorldToTexelForTesting(const FVector& WorldLocation) const
{
	// Deliberately duplicates the coordinate maths in UpdateHeatmapWithTrajectorySegments so a test
	// asserts against the same texel the rasteriser wrote. If that conversion changes, this must too.
	const FVector2D TexturePoint = ActorWorldToUV(WorldLocation);
	return FIntPoint(
		FMath::Clamp(FMath::RoundToInt(TexturePoint.X), 0, TextureWidth - 1),
		FMath::Clamp(FMath::RoundToInt(TexturePoint.Y), 0, TextureHeight - 1));
}
#endif

void AHeatmapPixelTextureVisualizer::UpdateHeatmapWithMultipleAgents_NoCheck(const TArray<FVector>& AgentLocations)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Update heatmap agent locations no check start function");
	if (!DynamicTexture)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Heatmap Update Error"),
				FText::FromString("Dynamic texture missing"),
				FText::FromString("Heatmap texture is not available for updates."),
				FText::FromString("HeatmapPixelTextureVisualizer"));
		}
		return;
	}
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
	if (!DynamicTexture)
	{
		return;
	}
	DynamicTexture->UpdateTextureRender();
}

void AHeatmapPixelTextureVisualizer::ClearTexture()
{
	if (!DynamicTexture)
	{
		return;
	}
	DynamicTexture->ClearTexture();
	if (TrajectoryAccumulationTexture)
	{
		TrajectoryAccumulationTexture->ClearTexture();
	}
}

void AHeatmapPixelTextureVisualizer::UpdateMeshSize(const FVector2D& NewMeshSize)
{
	// check if the mesh is valid (any section)
	if (!RuntimeHeatmapMeshComponent || RuntimeHeatmapMeshComponent->GetNumSections() == 0)
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

	// Drop any tiled sections and rebuild as a single 4-vert plane. This path only runs for the simple
	// (non-dense) case; dense heatmaps should retrigger GenerateMeshVerticesUVsAndTriangles instead.
	if (TileEmitTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TileEmitTickerHandle);
		TileEmitTickerHandle.Reset();
	}
	RuntimeHeatmapMeshComponent->ClearAllMeshSections();
	Tiles.Reset();
	PendingTileEmitIndex = 0;

	const double PushStart = FPlatformTime::Seconds();
	RuntimeHeatmapMeshComponent->CreateMeshSection(0, MeshVertices, MeshTriangles, TArray<FVector>(), MeshUVs,
	                                               TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	const double PushDurationMs = (FPlatformTime::Seconds() - PushStart) * 1000.0;
	if (UMobiusCustomLoggerSubsystem* StartupLogger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr)
	{
		StartupLogger->EnqueueLogMessage(FString::Printf(
			TEXT("[Heatmap %s floor=%d] UpdateMeshSize single-section rebuild verts=%d tris=%d in %.2f ms"),
			*ActorName, FloorID, MeshVertices.Num(), MeshTriangles.Num() / 3, PushDurationMs));
	}
	AssignMaterialInstanceToMesh();
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
		if (TrajectoryMaterialInstance)
		{
			// The trajectory instance is the standard graph too, so it needs the same displacement.
			TrajectoryMaterialInstance->SetScalarParameterValue(FName("HeightScale"), HeightDisplacement);
		}
	}

	// Visual type changes do not discard accumulated data. A live map is refreshed immediately
	// below from the subsystem's latest agent locations.
	if (bLiveTrackingHeatmap && !bTrajectoryHeatmap)
	{
		if (UWorld* CurrentWorld = GetWorld())
		{
			if (UHeatmapSubsystem* HeatmapSubsystem = CurrentWorld->GetSubsystem<UHeatmapSubsystem>())
			{
				HeatmapSubsystem->RefreshHeatmapFromLatestLocations(this);
			}
		}
	}
}

void AHeatmapPixelTextureVisualizer::SetTrajectoryHeatmapEnabled(bool bEnabled)
{
	if (bTrajectoryHeatmap == bEnabled)
	{
		return;
	}

	if (bEnabled)
	{
		bLiveTrackingBeforeTrajectory = bLiveTrackingHeatmap;
		bTrajectoryHeatmap = true;
		bLiveTrackingHeatmap = false;

		// Entering trajectory mode starts a visibly empty, new played-session history.
		ClearTexture();
		if (TrajectoryAccumulationTexture)
		{
			HeatmapMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), TrajectoryAccumulationTexture->GetDynamicTexture());
			VoronoiMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), TrajectoryAccumulationTexture->GetDynamicTexture());
			if (TrajectoryMaterialInstance)
			{
				TrajectoryMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), TrajectoryAccumulationTexture->GetDynamicTexture());
				// HeightScale is only pushed to the standard instance when a standard heatmap is selected,
				// so the trajectory instance has to be given it here or a 3D trajectory surface renders flat.
				TrajectoryMaterialInstance->SetScalarParameterValue(FName("HeightScale"), HeightDisplacement);
			}
			ApplyTrajectoryLOSBands();
			TrajectoryAccumulationTexture->UpdateTextureRender();
		}

		// Swap the banded surface over to the trajectory band edges.
		AssignMaterialInstanceToMesh();
		return;
	}

	// Leaving trajectory mode discards its view, restores the previous normal mode and immediately
	// paints the most recent simulation locations so no extra widget toggle is required.
	bTrajectoryHeatmap = bEnabled;
	bLiveTrackingHeatmap = bLiveTrackingBeforeTrajectory;
	ClearTexture();
	HeatmapMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), DynamicTexture->GetDynamicTexture());
	VoronoiMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), DynamicTexture->GetDynamicTexture());
	// Put the density-banded surface back on the mesh before repainting it with density data.
	AssignMaterialInstanceToMesh();
	UpdateHeatmapTextureRender();

	if (UWorld* CurrentWorld = GetWorld())
	{
		if (UHeatmapSubsystem* HeatmapSubsystem = CurrentWorld->GetSubsystem<UHeatmapSubsystem>())
		{
			HeatmapSubsystem->RefreshHeatmapFromLatestLocations(this);
		}
	}
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
	// Large floor meshes can convert a 20 cm path footprint to a fractional texel. Keep one
	// texel minimum so a trajectory remains visible without inflating its world-space radius.
	ScaledTrajectoryCircleSize = FMath::Max(1, FMath::RoundToInt(TrajectoryCircleRadius * FMath::Min(UVScale.X, UVScale.Y)));
	UE_LOG(LogTemp, Log, TEXT("UVScale: (%f, %f), ScaledCircleSize: %d"), UVScale.X, UVScale.Y, ScaledCircleSize);

}

void AHeatmapPixelTextureVisualizer::BuildGridMeshPlane(const FVector2D& MeshSize, bool bIsStandardHeatmap)
{
	// Drop every section (dense path may have emitted N). Simple BuildGridMeshPlane only ever produces one.
	if (TileEmitTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TileEmitTickerHandle);
		TileEmitTickerHandle.Reset();
	}
	RuntimeHeatmapMeshComponent->ClearAllMeshSections();
	Tiles.Reset();
	PendingTileEmitIndex = 0;

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

	// Because grid mesh building is a heavy task we need to do it off the game thread.
	// Capture via TWeakObjectPtr so a destroyed actor (e.g. file switch mid-build)
	// doesn't leave the worker dereferencing freed members.
	TWeakObjectPtr<AHeatmapPixelTextureVisualizer> WeakThis(this);
	AsyncTask(ENamedThreads::AnyThread, [WeakThis, NumTriangles, MeshSize, bIsStandardHeatmap]()
	{
		AHeatmapPixelTextureVisualizer* Self = WeakThis.Get();
		if (!Self || !IsValid(Self->RuntimeHeatmapMeshComponent)) return;

		UKismetProceduralMeshLibrary::CreateGridMeshWelded(NumTriangles.X, NumTriangles.Y, Self->MeshTriangles, Self->MeshVertices, Self->MeshUVs, 25);

		// Update the mesh
		const double PushStart = FPlatformTime::Seconds();
		Self->RuntimeHeatmapMeshComponent->CreateMeshSection(0, Self->MeshVertices, Self->MeshTriangles, TArray<FVector>(), Self->MeshUVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		const double PushDurationMs = (FPlatformTime::Seconds() - PushStart) * 1000.0;
		if (UMobiusCustomLoggerSubsystem* BuildLog = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr)
		{
			BuildLog->EnqueueLogMessage(FString::Printf(
				TEXT("[Heatmap %s floor=%d] BuildGridMeshPlane CreateMeshSection verts=%d tris=%d in %.2f ms (off-GT)"),
				*Self->ActorName, Self->FloorID, Self->MeshVertices.Num(), Self->MeshTriangles.Num() / 3, PushDurationMs));
		}
		Self->AssignMaterialInstanceToMesh();
	});

}

void AHeatmapPixelTextureVisualizer::UpdateHeatmapCVDSettings(EColorVisionDeficiency ColourDeficiency,
                                                              float DeficiencyLevel, bool bCorrectDeficiency, bool bSimulateColourCorrectionWithDeficiency)
{
	if (!DynamicTexture)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Heatmap Update Error"),
				FText::FromString("Dynamic texture missing"),
				FText::FromString("Heatmap texture is not available for CVD updates."),
				FText::FromString("HeatmapPixelTextureVisualizer"));
		}
		return;
	}
	DynamicTexture->UpdateHeatmapCVDSettings(ColourDeficiency, DeficiencyLevel, bCorrectDeficiency, bSimulateColourCorrectionWithDeficiency);
}

void AHeatmapPixelTextureVisualizer::SaveHeatmapToPNG() const
{
	if (!DynamicTexture)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Heatmap Save Error"),
				FText::FromString("Dynamic texture missing"),
				FText::FromString("Heatmap texture is not available for export."),
				FText::FromString("HeatmapPixelTextureVisualizer"));
		}
		return;
	}
	// File name is name + Created date time + .png
	FString SafeTimestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString FileName = FString::Printf(TEXT("Heatmap/%s_%s.png"), *ActorName, *SafeTimestamp);
	DynamicTexture->SaveDynamicTextureToPNG(FileName);
}

void AHeatmapPixelTextureVisualizer::SaveHeatmapToPNG(const FString& CurrentTimeString) const
{
	if (!DynamicTexture)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Heatmap Save Error"),
				FText::FromString("Dynamic texture missing"),
				FText::FromString("Heatmap texture is not available for export."),
				FText::FromString("HeatmapPixelTextureVisualizer"));
		}
		return;
	}
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

void AHeatmapPixelTextureVisualizer::BuildTileBuffers(int32 TileX0, int32 TileY0, int32 TileX1, int32 TileY1,
                                                      const FIntPoint& NumTriangles, const FVector2D& CellSize,
                                                      const TArray<FBox3d>& Quads, FHeatmapTile& Out) const
{
	// UV aspect correction matches the legacy CreateMeshVertexsAndUVs derivation so world-space UV math
	// remains unchanged across the tile boundaries — no seams in the dynamic-texture sampling.
	const bool bAdjustY = HeatmapMeshSize2D.X >= HeatmapMeshSize2D.Y;
	const float AspectRatio = bAdjustY ? (HeatmapMeshSize2D.Y / HeatmapMeshSize2D.X)
	                                   : (HeatmapMeshSize2D.X / HeatmapMeshSize2D.Y);

	// Global-grid-index -> local-tile-vert-index. Only verts referenced by kept quads are materialised.
	TMap<int32, int32> GlobalToLocal;
	GlobalToLocal.Reserve((TileX1 - TileX0 + 1) * (TileY1 - TileY0 + 1));

	auto AddOrGetVert = [&](int32 gx, int32 gy) -> int32
	{
		const int32 GlobalIdx = gx + gy * NumTriangles.X;
		if (const int32* Existing = GlobalToLocal.Find(GlobalIdx))
		{
			return *Existing;
		}

		FVector Vertex(gx * CellSize.X, gy * CellSize.Y, 0.1f);
		float UVx = static_cast<float>(gx) / (NumTriangles.X - 1);
		float UVy = static_cast<float>(gy) / (NumTriangles.Y - 1);
		if (bAdjustY)
		{
			UVy = UVy * AspectRatio + (1.0f - AspectRatio) * 0.5f;
		}
		else
		{
			UVx = UVx * AspectRatio + (1.0f - AspectRatio) * 0.5f;
		}

		const int32 LocalIdx = Out.Verts.Add(Vertex);
		Out.UVs.Add(FVector2D(UVx, UVy));
		GlobalToLocal.Add(GlobalIdx, LocalIdx);
		return LocalIdx;
	};

	// Walk every quad cell in the tile range. Preserve the quad-intersect filter so ignored regions
	// (outside the building footprint) still produce zero geometry.
	for (int32 y = TileY0; y < TileY1; ++y)
	{
		for (int32 x = TileX0; x < TileX1; ++x)
		{
			bool bKeep = Quads.Num() == 0;
			if (!bKeep)
			{
				const FVector V0 = FVector(x * CellSize.X, y * CellSize.Y, 0.1f) + MeshOriginLocation;
				const FVector V1 = FVector(x * CellSize.X, (y + 1) * CellSize.Y, 0.1f) + MeshOriginLocation;
				const FVector V2 = FVector((x + 1) * CellSize.X, y * CellSize.Y, 0.1f) + MeshOriginLocation;
				const FVector V3 = FVector((x + 1) * CellSize.X, (y + 1) * CellSize.Y, 0.1f) + MeshOriginLocation;
				for (const FBox3d& Quad : Quads)
				{
					if (Quad.IsInsideOrOn(V0) || Quad.IsInsideOrOn(V1) ||
					    Quad.IsInsideOrOn(V2) || Quad.IsInsideOrOn(V3))
					{
						bKeep = true;
						break;
					}
				}
			}
			if (!bKeep)
			{
				continue;
			}

			// Matches legacy index pattern: (Idx0,Idx1,Idx2) + (Idx1,Idx4,Idx2) where
			// Idx0=(x,y), Idx1=(x,y+1), Idx2=(x+1,y), Idx4=(x+1,y+1).
			const int32 L0 = AddOrGetVert(x, y);
			const int32 L1 = AddOrGetVert(x, y + 1);
			const int32 L2 = AddOrGetVert(x + 1, y);
			const int32 L3 = AddOrGetVert(x + 1, y + 1);

			Out.Tris.Add(L0);
			Out.Tris.Add(L1);
			Out.Tris.Add(L2);
			Out.Tris.Add(L1);
			Out.Tris.Add(L3);
			Out.Tris.Add(L2);
		}
	}
}

void AHeatmapPixelTextureVisualizer::GenerateMeshVerticesUVsAndTriangles(const FVector2D& MeshSize,
                                                                         const FIntPoint& TextureSize, bool bIs3DHeatmap)
{
	// Update the mesh size
	HeatmapMeshSize2D = MeshSize;

	// Do NOT ClearAllMeshSections here: SetupDynamicTexture runs synchronously right after this call
	// and gates on GetNumSections() > 0. Dropping the default constructor-emitted section 0 would make
	// the dynamic texture bail with "Heatmap resources missing". Clearing is deferred to the GT emit
	// continuation just before the tiles are pushed, so the old sections remain live until replaced.
	// Cancel any in-flight emit from a previous call so its stale tiles don't stomp this generation.
	if (TileEmitTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TileEmitTickerHandle);
		TileEmitTickerHandle.Reset();
	}
	Tiles.Reset();
	PendingTileEmitIndex = 0;

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

	// Validate data before spawning the threaded task
	const bool bValidMeshBuilder = MeshBuilder != nullptr;
	const bool bValidTriangles  = NumTriangles.X > 0 && NumTriangles.Y > 0;

	if(!bValidMeshBuilder)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Heatmap Mesh Error"),
				FText::FromString("Runtime mesh builder missing"),
				FText::FromString("GenerateMeshVerticesUVsAndTriangles requires a valid RuntimeMeshBuilder."),
				FText::FromString("HeatmapPixelTextureVisualizer"));
		}
		UE_LOG(LogTemp, Error, TEXT("GenerateMeshVerticesUVsAndTriangles: Invalid MeshBuilder"));
	}

	if(!bValidTriangles)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Heatmap Mesh Error"),
				FText::FromString("Invalid heatmap triangle count"),
				FText::FromString("Heatmap triangle dimensions must be greater than zero."),
				FText::FromString("HeatmapPixelTextureVisualizer"));
		}
		UE_LOG(LogTemp, Error, TEXT("GenerateMeshVerticesUVsAndTriangles: Invalid NumTriangles (%d, %d)"), NumTriangles.X, NumTriangles.Y);
	}

	if(!bValidMeshBuilder || !bValidTriangles)
	{
		// Early exit on invalid data
		return;
	}

	if(MeshVertices.Num() > 0 || MeshUVs.Num() > 0 || MeshTriangles.Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("GenerateMeshVerticesUVsAndTriangles: Clearing pre-existing mesh data"));
		MeshVertices.Empty();
		MeshUVs.Empty();
		MeshTriangles.Empty();
	}

	// Snap tile size to >=4 cells; user-visible UPROPERTY clamps at 4 already but a direct member poke could slip through.
	const int32 LocalTileSize = FMath::Max(4, GridTileSize);

	// Capture via TWeakObjectPtr so a destroyed actor doesn't leave the
	// worker / GT continuation dereferencing freed members during a file switch.
	TWeakObjectPtr<AHeatmapPixelTextureVisualizer> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, NumTriangles, CellSize, MeshBuilder, LocalTileSize]()
	      {
		      AHeatmapPixelTextureVisualizer* Self = WeakThis.Get();
		      if (!Self) return;

		      // Generate the quads to restrict the triangle generation to areas needed
		      const TArray<FBox3d> Quads = Self->FindAllQuads(MeshBuilder);

		      // Number of quad cells (vertex grid is NumTriangles.X x NumTriangles.Y).
		      const int32 QuadsX = FMath::Max(0, NumTriangles.X - 1);
		      const int32 QuadsY = FMath::Max(0, NumTriangles.Y - 1);

		      TArray<FHeatmapTile> LocalTiles;
		      if (Self->bEnableMultiSectionBatching && LocalTileSize > 0)
		      {
			      // Tile the quad grid; tiles with zero kept triangles are discarded so empty regions cost nothing.
			      const int32 NumTileCols = FMath::DivideAndRoundUp(QuadsX, LocalTileSize);
			      const int32 NumTileRows = FMath::DivideAndRoundUp(QuadsY, LocalTileSize);
			      LocalTiles.Reserve(NumTileCols * NumTileRows);

			      for (int32 y0 = 0; y0 < QuadsY; y0 += LocalTileSize)
			      {
				      const int32 y1 = FMath::Min(y0 + LocalTileSize, QuadsY);
				      for (int32 x0 = 0; x0 < QuadsX; x0 += LocalTileSize)
				      {
					      const int32 x1 = FMath::Min(x0 + LocalTileSize, QuadsX);
					      FHeatmapTile Tile;
					      Self->BuildTileBuffers(x0, y0, x1, y1, NumTriangles, CellSize, Quads, Tile);
					      if (Tile.Tris.Num() > 0)
					      {
						      LocalTiles.Emplace(MoveTemp(Tile));
					      }
				      }
			      }
		      }
		      else
		      {
			      // Legacy single-section fallback for the rollback flag.
			      FHeatmapTile Whole;
			      Self->BuildTileBuffers(0, 0, QuadsX, QuadsY, NumTriangles, CellSize, Quads, Whole);
			      if (Whole.Tris.Num() > 0)
			      {
				      LocalTiles.Emplace(MoveTemp(Whole));
			      }
		      }

		      // Hand the built tiles over to the actor for the GT emit stage.
		      if (AHeatmapPixelTextureVisualizer* Alive = WeakThis.Get())
		      {
			      Alive->Tiles = MoveTemp(LocalTiles);
		      }
	      },
	      [WeakThis]
	      {
		      AsyncTask(ENamedThreads::GameThread, [WeakThis]()
		      {
			      AHeatmapPixelTextureVisualizer* Self = WeakThis.Get();
			      if (!Self || !IsValid(Self->RuntimeHeatmapMeshComponent)) return;

			      // Clear existing sections then hand tile emit to the staggered pump. Emitting every
			      // tile in one tick burns FScene_AddPrimitive on the GT (bigger grids = bigger hitch);
			      // the pump spreads the cost across frames. Material apply runs once in FinalizeTileEmit
			      // so tiles picked up before the final flush may render unlit for a frame or two —
			      // acceptable trade for eliminating the spike.
			      if (UMobiusCustomLoggerSubsystem* KickoffLog = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr)
			      {
				      KickoffLog->EnqueueLogMessage(FString::Printf(
					      TEXT("[Heatmap %s floor=%d] Tile emit kickoff tiles=%d"),
					      *Self->ActorName, Self->FloorID, Self->Tiles.Num()));
			      }
			      Self->RuntimeHeatmapMeshComponent->ClearAllMeshSections();
			      Self->PendingTileEmitIndex = 0;
			      Self->TileEmitStartTime = FPlatformTime::Seconds();

			      if (Self->TileEmitTickerHandle.IsValid())
			      {
				      FTSTicker::GetCoreTicker().RemoveTicker(Self->TileEmitTickerHandle);
				      Self->TileEmitTickerHandle.Reset();
			      }

			      if (Self->Tiles.Num() == 0)
			      {
				      Self->FinalizeTileEmit();
				      return;
			      }

			      Self->TileEmitTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				      FTickerDelegate::CreateUObject(Self, &AHeatmapPixelTextureVisualizer::EmitNextTileSection),
				      0.0f);
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

bool AHeatmapPixelTextureVisualizer::EmitNextTileSection(float /*DeltaTime*/)
{
	if (!IsValid(RuntimeHeatmapMeshComponent))
	{
		Tiles.Empty();
		PendingTileEmitIndex = 0;
		TileEmitTickerHandle.Reset();
		return false;
	}

	// Pick the material up-front so we can set it per section as each tile goes live, rather than
	// shipping tiles with default material until FinalizeTileEmit runs one pass at the end.
	UMaterialInstanceDynamic* TargetMaterial = HeatmapType ? HeatmapMaterialInstance.Get() : VoronoiMaterialInstance.Get();

	UMobiusCustomLoggerSubsystem* StartupLogger =
		GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;

	const int32 TilesThisTick = FMath::Max(1, SectionsEmittedPerTick);
	for (int32 Pushed = 0; Pushed < TilesThisTick && PendingTileEmitIndex < Tiles.Num(); ++Pushed)
	{
		const int32 SectionIdx = PendingTileEmitIndex++;
		FHeatmapTile& T = Tiles[SectionIdx];

		const int32 TileVerts = T.Verts.Num();
		const int32 TileTris  = T.Tris.Num() / 3;
		const double PushStart = FPlatformTime::Seconds();

		RuntimeHeatmapMeshComponent->CreateMeshSection_LinearColor(
			SectionIdx, T.Verts, T.Tris, TArray<FVector>(), T.UVs,
			TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);

		if (TargetMaterial)
		{
			RuntimeHeatmapMeshComponent->SetMaterial(SectionIdx, TargetMaterial);
		}

		const double PushDurationMs = (FPlatformTime::Seconds() - PushStart) * 1000.0;
		if (StartupLogger)
		{
			StartupLogger->EnqueueLogMessage(FString::Printf(
				TEXT("[Heatmap %s floor=%d] CreateMeshSection section=%d verts=%d tris=%d in %.2f ms"),
				*ActorName, FloorID, SectionIdx, TileVerts, TileTris, PushDurationMs));
		}
		UE_LOG(LogTemp, Log,
			TEXT("[Heatmap %s floor=%d] CreateMeshSection section=%d verts=%d tris=%d in %.2f ms"),
			*ActorName, FloorID, SectionIdx, TileVerts, TileTris, PushDurationMs);

		// Free the per-tile CPU buffers now that the component owns the data.
		T.Verts.Empty();
		T.Tris.Empty();
		T.UVs.Empty();
	}

	if (PendingTileEmitIndex >= Tiles.Num())
	{
		FinalizeTileEmit();
		return false;
	}

	return true;
}

void AHeatmapPixelTextureVisualizer::FinalizeTileEmit()
{
	const int32 EmittedSections = Tiles.Num();

	Tiles.Empty();
	PendingTileEmitIndex = 0;
	TileEmitTickerHandle.Reset();

	if (IsValid(RuntimeHeatmapMeshComponent))
	{
		AssignMaterialInstanceToMesh();
	}

	const double DurationMs = (FPlatformTime::Seconds() - TileEmitStartTime) * 1000.0;
	if (UMobiusCustomLoggerSubsystem* StartupLogger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr)
	{
		StartupLogger->EnqueueLogMessage(FString::Printf(
			TEXT("[Heatmap %s floor=%d] Tile emit finished sections=%d in %.2f ms"),
			*ActorName, FloorID, EmittedSections, DurationMs));
	}
	UE_LOG(LogTemp, Log, TEXT("[Heatmap %s floor=%d] Tile emit finished sections=%d in %.2f ms"),
		*ActorName, FloorID, EmittedSections, DurationMs);
}

void AHeatmapPixelTextureVisualizer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Kill the tile emit pump before the actor goes away — the ticker holds a UObject binding
	// to `this` and would fire again post-teardown otherwise.
	if (TileEmitTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TileEmitTickerHandle);
		TileEmitTickerHandle.Reset();
	}
	Tiles.Empty();
	PendingTileEmitIndex = 0;

	Super::EndPlay(EndPlayReason);
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
		// const double StartTime = FPlatformTime::Seconds();
		// UE_LOG(LogTemp, Warning, TEXT("Starting Mesh Triangle Generation"));
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
			// Building mesh is now batched into N sections; walk every one so quad detection sees the full mesh.
			UProceduralMeshComponent* BuildingComp = MeshBuilder->MobiusProceduralMeshComponent;
			if (BuildingComp)
			{
				const int32 NumSections = BuildingComp->GetNumSections();
				for (int32 s = 0; s < NumSections; ++s)
				{
					const FProcMeshSection* Sec = BuildingComp->GetProcMeshSection(s);
					if (!Sec) continue;
					for (const FProcMeshVertex& VertexStruct : Sec->ProcVertexBuffer)
					{
						if (VertexStruct.Position.Z <= StartPos.Z + StepSize && VertexStruct.Position.Z >= StartPos.Z - StepSize)
						{
							ValidVertices.Add(VertexStruct.Position);
						}
					}
				}
			}
		}
		
		// build a simple spatial hash to avoid scanning every vertex per step
		TMap<FIntPoint, FBox3d> QuadMap;
		for (const FVector& Vertex : ValidVertices)
		{
			const int32 CellX = FMath::FloorToInt((Vertex.X - StartPos.X) / StepSize);
			const int32 CellY = FMath::FloorToInt((Vertex.Y - StartPos.Y) / StepSize);
			const FIntPoint Key(CellX, CellY);
			if (!QuadMap.Contains(Key))
			{
				FVector Min(StartPos.X + CellX * StepSize,
				            StartPos.Y + CellY * StepSize,
				            MarchingBox.Min.Z);
				FVector Max(StartPos.X + (CellX + 1) * StepSize,
				            StartPos.Y + (CellY + 1) * StepSize,
				            MarchingBox.Max.Z);
				QuadMap.Add(Key, FBox3d(Min, Max));
			}
		}

		for (const TPair<FIntPoint, FBox3d>& Pair : QuadMap)
		{
			Quads.Add(Pair.Value);
		}

		// Log how many boxes found
		// UE_LOG(LogTemp, Warning, TEXT("Found Quads: %d"), Quads.Num());
		// const double EndTime = FPlatformTime::Seconds();
		// UE_LOG(LogTemp, Warning, TEXT("FindAllQuads took %.2f ms"), (EndTime - StartTime) * 1000.0);
	}

	return Quads;
	
	
	
	// Algorithm refactored to use spatial hashing
	
	 
	
	 
	
}
