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
#include "Misc/FileHelper.h"   // trajectory canonical CSV + metadata sidecar
#include "Misc/Paths.h"


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

UMaterialInstanceDynamic* AHeatmapPixelTextureVisualizer::SelectSurfaceMaterial() const
{
	// Trajectory mode only replaces the banded surface. The voronoi material has no band chain at all —
	// it saturates the raw channel — so it needs no trajectory variant and stays as-is.
	if (bTrajectoryHeatmap && HeatmapType && TrajectoryMaterialInstance)
	{
		return TrajectoryMaterialInstance.Get();
	}
	return HeatmapType ? HeatmapMaterialInstance.Get() : VoronoiMaterialInstance.Get();
}

void AHeatmapPixelTextureVisualizer::AssignMaterialInstanceToMesh() const
{
	if (!RuntimeHeatmapMeshComponent)
	{
		return;
	}
	UMaterialInstanceDynamic* Target = SelectSurfaceMaterial();
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

	// DENSITY render-target size only. The trajectory surface no longer derives its resolution from
	// these: FTrajectoryField fixes cm/texel (D2) and the accumulation texture is sized from the
	// resulting grid in EnsureTrajectoryFieldSized. Deliberately left at 1024 rather than driven from
	// the grid, because UVScale — and through it ScaledCircleSize, the density agent disc — is computed
	// from TextureWidth/TextureHeight, so changing them here would silently retune the density surface.
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

void AHeatmapPixelTextureVisualizer::ApplyTrajectoryLOSBands() const
{
	// D9 — NORMALISATION. The band edges are pushed in normalised red-channel units (byte/255) and both
	// consumers compare in exactly that space: the material samples the red channel, and the PNG
	// colouriser divides the stored byte by 255 (UpdateTextureToLOSColour). So the two are symmetric
	// because they receive the identical struct, not because two conversions were matched by hand.
	//
	// The alternative — keeping the edges in canonical person/m and pushing the encode's normalisation
	// scale to the material as a sixth parameter — was rejected: FHeatmapLOSBands clamps its fields to
	// [0,1] so canonical edges cannot even be stored, and a new scalar parameter means editing
	// M_HeatmapRT_Trajectory, which this change is not allowed to do. The canonical equivalents are
	// reconstructed for the export sidecar instead (WriteTrajectoryCanonicalExport), where the
	// auto-exposure scale that relates the two is recorded alongside them.
	//
	// The parameter names still read LOS_*; see TrajectoryLOSBands for why they cannot be renamed yet.
	//
	// MODE-SELECTED since 2026-08-04: Usage and Exposure have different reference densities, so they need
	// different normalised edges. Pushing one set for both mis-bands whichever mode it was not fitted to.
	const FHeatmapLOSBands& Bands = (TrajectoryMapMode == ETrajectoryMapMode::RouteExposure)
		? TrajectoryExposureLOSBands
		: TrajectoryLOSBands;

	if (TrajectoryMaterialInstance)
	{
		TrajectoryMaterialInstance->SetScalarParameterValue(FName("LOS_A_Band"), Bands.BandA);
		TrajectoryMaterialInstance->SetScalarParameterValue(FName("LOS_B_Band"), Bands.BandB);
		TrajectoryMaterialInstance->SetScalarParameterValue(FName("LOS_C_Band"), Bands.BandC);
		TrajectoryMaterialInstance->SetScalarParameterValue(FName("LOS_D_Band"), Bands.BandD);
		TrajectoryMaterialInstance->SetScalarParameterValue(FName("LOS_E_Band"), Bands.BandE);

		// Pushed here rather than anywhere else so the revert switch travels with the edges it softens: a
		// band set and its boundary treatment are one visual contract, and splitting them across two push
		// sites is how one of them ends up stale. Setting this to 0 restores the hard comparison chain
		// exactly — see TrajectoryBandEdgeSoftness. The CPU PNG colouriser deliberately does NOT antialias:
		// an exported image is read per-texel and a blended boundary pixel would belong to no band at all.
		TrajectoryMaterialInstance->SetScalarParameterValue(
			FName("BandEdgeSoftness"), FMath::Max(0.0f, TrajectoryBandEdgeSoftness));
	}

	// The PNG export colourises on the CPU. Give it the same edges or the saved image and the in-world
	// render disagree — which is exactly the discrepancy that hid this bug in the first place.
	if (TrajectoryAccumulationTexture)
	{
		TrajectoryAccumulationTexture->SetLOSBands(Bands);
	}
}

void AHeatmapPixelTextureVisualizer::RefreshTrajectoryCrossingBands()
{
	// D9 / 2026-08-10 — DERIVE the Route Usage band edges from the STROKE WIDTH, not from the cell.
	//
	// An edge means "N + 0.5 crossings". One crossing deposits one cell side of person-metres into the
	// CANONICAL array — but the encode reads the PRESENTATION array, and BuildKernel's splat is
	// mass-conserving, not dilating. Widening the stroke past one cell therefore divides a crossing's mass
	// across the kernel by exactly cellSide / width. Folding that compensation into the edge cancels the
	// cell side outright:
	//
	//     edge_N = (N + 0.5) / (cellSide x Reference) x (cellSide / width) = (N + 0.5) / (width x Reference)
	//
	// So the CELL is pure sampling resolution and the WIDTH is the physical quantity a count refers to:
	// "person-passes through a 45 cm-wide corridor centred here". That is the statement we actually want,
	// and it is what lets the cell be dialled finer for a smoother silhouette without every colour
	// silently changing meaning. Banding against the cell is what sent the whole map back to cyan whenever
	// the grid was refined — at a 15 cm cell under a 45 cm stroke, band B would have demanded 3x the real
	// crossings to light up.
	//
	// The numbers do NOT move at the shipping pair: width 45 cm reproduces exactly the edges the 45 cm
	// cell used to produce, so M_HeatmapRT_Trajectory's committed values (2de28478) remain correct and
	// T-BAND-6 still gates them. What changed is only WHICH property they track.
	//
	// TrajectoryDisplayPathWidthCm, not the field's config copy: SetDisplayPathWidthCm can move the
	// kernel after Initialise, and the actor property is the one both this and BuildKernel end up on.
	// It is UPROPERTY-clamped at 0.1 cm, so unlike the old cell-size argument it can never be handed the
	// zero an unsized field reports — TrajectoryCrossings' degenerate-input fallback is now unreachable
	// from here, which is why this is safe to call before Initialise has ever run.
	//
	// This deliberately OVERWRITES the TrajectoryLOSBands UPROPERTY. The edges are a computed contract,
	// not a preference: a value typed into the details panel cannot know the reference density, and
	// honouring it would reintroduce the very defect this replaces — band meaning drifting per building.
	// Route Exposure keeps its own set (TrajectoryExposureLOSBands): different quantity, different
	// reference, not touched here.
	// D-E — the 0/1 edge is derived from the kernel that draws the stroke. This is the ONE input here that
	// legitimately depends on the cell size: the band EDGES are a statement about crossings and stay on
	// the width, but "is this cell part of the stroke at all" is a question about how the stroke was
	// rasterised, and that is a function of width AND cell. Using the EFFECTIVE cm/texel, not the
	// requested one, for the same reason the grid does — D2b and D-A both move it.
	const float EffectiveCmPerTexel = (TrajectoryField.GetEffectiveCmPerTexel() > 0.0f)
		? TrajectoryField.GetEffectiveCmPerTexel()
		: TrajectoryWorldCmPerTexel;
	const float RouteThresholdCrossings =
		FTrajectoryField::DeriveRouteThresholdCrossings(TrajectoryDisplayPathWidthCm, EffectiveCmPerTexel);

	TrajectoryLOSBands = FHeatmapLOSBands::TrajectoryCrossings(
		TrajectoryDisplayPathWidthCm / 100.0f, // stroke width cm -> metres
		TrajectoryField.GetConfig().ReferenceUsageDensity,
		RouteThresholdCrossings);

	// ---- Route Exposure, banded in transit-equivalents (SPEC §5.2) -------------------------------------
	//
	// Exposure needs its OWN derivation and cannot borrow the usage edges: different quantity, different
	// reference, and its anchor t0 = cell / v_free is a per-CELL time, so unlike the crossing edges these
	// legitimately move with the cell size.
	//
	// THE ROUTE THRESHOLD CONVERTS, and the conversion is exact rather than a re-fit. DeriveRouteThreshold-
	// Crossings returns the cut in crossings, and a single pass deposits (width / cell) crossings spread
	// across the kernel's rows — so dividing by that recovers the dimensionless CUT FRACTION of the kernel's
	// marginal. For exposure the natural unit is already one transit per free-speed pass (that is what t0
	// means), so the same fraction IS the threshold in transits, with nothing left to tune.
	const float WidthInCells = (EffectiveCmPerTexel > 0.0f)
		? (TrajectoryDisplayPathWidthCm / EffectiveCmPerTexel)
		: 0.0f;
	const float RouteThresholdTransits = (WidthInCells > 0.0f)
		? (RouteThresholdCrossings / WidthInCells)
		: 0.0f;

	TrajectoryExposureLOSBands = FHeatmapLOSBands::TrajectoryTransits(
		EffectiveCmPerTexel / 100.0f, // effective cell side cm -> metres
		TrajectoryField.GetConfig().ReferenceExposureDensity,
		FHeatmapLOSBands::FreeWalkSpeedSFPE,
		RouteThresholdTransits);

	// Push immediately rather than leaving it to the ApplyTrajectoryLOSBands() at the end of
	// EnsureTrajectoryFieldSized: that call sits inside the texture-resize branch and is skipped whenever
	// the render target is already the right square, which would leave both consumers banding against the
	// previous grid's edges. Both targets are null-checked inside, so calling before they exist is
	// harmless and the later sites re-push the same struct.
	ApplyTrajectoryLOSBands();
}

void AHeatmapPixelTextureVisualizer::SetTrajectoryMapMode(ETrajectoryMapMode NewMode)
{
	if (TrajectoryMapMode == NewMode)
	{
		return;
	}

	TrajectoryMapMode = NewMode;
	TrajectoryField.SetPresentationMode(NewMode);
	// The canonical arrays are untouched — only the presentation cache is rebuilt. Force a full texture
	// rewrite: every byte can change, since the new mode has its own reference density.
	TrajectoryPreviousRed.Reset();
	// The two modes carry different band edges (different reference densities), so the edges must be
	// re-pushed on every switch or the new mode renders against the old mode's thresholds.
	ApplyTrajectoryLOSBands();
	RefreshTrajectoryDisplay();
}

void AHeatmapPixelTextureVisualizer::EnsureTrajectoryFieldSized()
{
	if (!RuntimeHeatmapMeshComponent || HeatmapMeshSize2D.X <= 0.0 || HeatmapMeshSize2D.Y <= 0.0)
	{
		return;
	}

	// The mesh's verts run from the component origin in +X/+Y (see BuildTileBuffers), so the component
	// location is the grid's MINIMUM corner — which is what FTrajectoryField::Initialise wants. Passing
	// the centre here would shift the whole field by half a floor and nothing would flag it.
	const FVector MeshWorldOrigin = RuntimeHeatmapMeshComponent->GetComponentTransform().GetLocation();
	const FVector2D OriginCm(MeshWorldOrigin.X, MeshWorldOrigin.Y);

	const bool bAlreadySized = TrajectoryField.IsValid()
		&& TrajectoryFieldExtentCm.Equals(HeatmapMeshSize2D, 0.01)
		&& TrajectoryFieldOriginCm.Equals(OriginCm, 0.01)
		&& FMath::IsNearlyEqual(TrajectoryFieldSizedCmPerTexel, TrajectoryWorldCmPerTexel)
		&& TrajectoryFieldSizedMaxGridDim == TrajectoryMaxGridDim;
	if (bAlreadySized)
	{
		// Initialise() resets every accumulator, so re-running it on an unchanged floor would erase the
		// accumulated surface. Only the display width is cheap to re-apply.
		TrajectoryField.SetDisplayPathWidthCm(TrajectoryDisplayPathWidthCm);

		// Re-derive the bands here too. The effective cell size cannot have changed on this branch, so
		// this is idempotent — but it is a divide and four clamps, and paying it removes the need to prove
		// a negative every time this function is touched. bAlreadySized is judged on the REQUESTED
		// cm/texel, so anything that resets TrajectoryLOSBands while leaving the field valid (actor
		// re-construction, a details-panel edit, a level reload) would otherwise strand the surface on the
		// quantile placeholder with no way back until the floor changed.
		RefreshTrajectoryCrossingBands();
		return;
	}

	FTrajectoryFieldConfig Config;
	Config.WorldCmPerTexel = TrajectoryWorldCmPerTexel;
	Config.DisplayPathWidthCm = TrajectoryDisplayPathWidthCm;
	Config.MaxGridDim = TrajectoryMaxGridDim;

	// =================================================================================================
	// D-C / 2026-08-10 — PHASE THE FIELD'S CELL LATTICE ONTO THE RENDER'S TEXEL LATTICE.
	//
	// D-A and D-B fixed the SCALE and the constant term. This fixes the one that was left, and it is the
	// one the owner could still see: the drawn stroke sitting beside the agent, "inconsistent to
	// orientation".
	//
	// THE DEFECT. HeatmapMeshUV letterboxes the minor axis by a REAL-valued margin of
	// 0.5 * S * (1 - minorExtent / majorExtent) texels. The field can only write at INTEGER texels, so
	// whatever fraction that margin carries is a permanent sub-texel offset between where a cell is
	// written and where it is sampled. On the real 4548.9 x 3977.4 floor the ideal margin is 14.3224
	// texels: the fractional 0.3224 put 32% OF THE FLOOR in the wrong texel. Rotate the same floor 90 deg
	// and the error moves to the other axis, because it lives on whichever axis is MINOR — that is exactly
	// the orientation inconsistency, and it is also why floors whose extents happen to divide evenly
	// (5000 x 3000) looked perfect and made the fault seem intermittent.
	//
	// THE FIX. The cell lattice's PHASE is a free choice — nothing requires cell 0 to start at the mesh's
	// minimum corner. Shifting the field's origin by the fractional remainder re-phases the lattice onto
	// the render's, and then write and sample agree by construction rather than by luck:
	//
	//     cell + offset == floor(world / cm + idealMargin)   for every world position, exactly
	//
	// Measured over 3000 floors from 5 m to 200 m per side, both orientations: disagreement goes from
	// ~32% of sampled positions to float-boundary noise.
	//
	// WHY NOT FIX THE UV INSTEAD. The mesh UV is SHARED with the density surface, which has no ceil
	// quantisation and is aligned by construction today. Re-phasing the UV would fix trajectory and break
	// density. The origin is trajectory-only, so it is the correct thing to move.
	//
	// ROUND, NOT FLOOR, and the reason is narrow. On the exact path either works and produces the SAME
	// lattice — changing the offset by one texel is cancelled exactly by the origin shift, so the image is
	// identical. Round earns its place on the two DEGRADED paths below (the clamp, and the near-square
	// fallback), where the residual cannot be absorbed: round leaves at most half a texel, symmetric,
	// while floor would leave up to a full texel always displaced the same way — which is the very class
	// of one-directional bias this whole thread is removing.
	// =================================================================================================
	const FTrajectoryLatticePhase Phase = PlanTrajectoryLatticePhase(HeatmapMeshSize2D, Config);
	const FIntPoint PlannedOffset = Phase.TexelOffset;
	Config.ExtraGridCells = Phase.ExtraGridCells;

	// The PHASED origin goes to the field; TrajectoryFieldOriginCm below keeps the RAW mesh origin,
	// because that is what the bAlreadySized comparison is fed on the next call. Storing the phased value
	// there would make the comparison fail every frame and re-Initialise — wiping the accumulators — on a
	// floor that had not changed at all.
	const FVector2D PhasedOriginCm = OriginCm + Phase.OriginShiftCm;

	TrajectoryField.Initialise(HeatmapMeshSize2D, PhasedOriginCm, Config);
	// Initialise resets the presentation selection, so re-assert the actor's mode: it decides which
	// quantity the incremental splat maintains, and a mismatch would cost a full rebuild on first encode.
	TrajectoryField.SetPresentationMode(TrajectoryMapMode);
	RefreshTrajectoryCrossingBands();

	TrajectoryFieldExtentCm = HeatmapMeshSize2D;
	TrajectoryFieldOriginCm = OriginCm;
	TrajectoryFieldSizedCmPerTexel = TrajectoryWorldCmPerTexel;
	TrajectoryFieldSizedMaxGridDim = TrajectoryMaxGridDim;
	TrajectoryPreviousRed.Reset();

	if (!TrajectoryField.IsValid())
	{
		return;
	}

	// Square + letterboxed: the mesh UVs centre the minor axis, so the field sits inside a max(W,H)
	// texture. See TrajectoryTextureSize for the derivation.
	//
	// D-B/D-C: the offset is NOT recomputed here. It was decided above, together with the origin shift
	// and the pad cells that make it safe, and the three are one indivisible answer — recomputing any of
	// them in isolation is how they would drift apart. The pad only ever grows the MINOR axis and only up
	// to the major, so the square side is unchanged by it.
	const FIntPoint Dims = TrajectoryField.GetGridDims();
	const int32 SquareSide = FMath::Max(Dims.X, Dims.Y);
	TrajectoryTexelOffset = PlannedOffset;

	if (TrajectoryTextureSize == SquareSide && TrajectoryAccumulationTexture
		&& FMath::RoundToInt32(TrajectoryAccumulationTexture->GetDynamicTextureSize().X) == SquareSide)
	{
		return;
	}
	TrajectoryTextureSize = SquareSide;

	if (!TrajectoryAccumulationTexture)
	{
		return; // SetupDynamicTexture creates it and calls back through UpdateHeatmapMeshBounds.
	}

	// TF_NEAREST on the TRAJECTORY surface. Owner ruling 2026-08-10, reversing the 2026-08-05 ruling for
	// this surface only — the density surface keeps TF_Bilinear (see SetupDynamicTexture).
	//
	// The 2026-08-05 reasoning was sound for a CONTINUOUS field: this texture carries a scalar in RED, the
	// material bands it with `RVal < BAND` AFTER sampling, so bilinear interpolates the value and the band
	// lookup still returns a discrete colour. No mush — each boundary just becomes a smooth sub-texel
	// iso-contour instead of a texel staircase.
	//
	// That reasoning did not survive the crossing-count contract as it stood when this was written, because
	// the stroke was then exactly ONE texel wide — width and cell were locked equal, so the kernel was the
	// identity. Interpolating a one-texel line toward its zero neighbours means the sampled value holds the
	// true count only at the texel centre: for a 2-crossing stroke at byte 51 the sample falls below the
	// band-C floor (0.15) at just ±0.25 texel, so roughly 75% of the drawn width renders one or more bands
	// LOW. A surface whose whole claim is "this colour means N people walked here" cannot show the wrong N
	// across three quarters of itself. Smoothing a measurement someone is asked to read as a number is a
	// lie, however pretty.
	//
	// ⚠️ THAT ARGUMENT IS NOW WEAKER, and honesty requires saying so rather than leaving the ruling looking
	// better supported than it is. Since the 2026-08-10 decoupling the stroke is width/cell = 45/20 ≈ 2.25
	// texels, so a stroke's INTERIOR texels are surrounded by same-valued neighbours and bilinear would
	// leave them alone; only the one-texel rim would soften, and softening a rim is exactly the smooth
	// silhouette the owner asked for. The "75% reads low" figure belongs to the one-texel stroke and does
	// NOT transfer. TF_Nearest is kept because the owner ruled for it and because the rim of a thin stroke
	// is still a large fraction of a thin stroke — not because the original arithmetic still applies.
	//
	// 📌 REVISIT (owner note, 2026-08-10): the smooth iso-contour look may be wanted back. The old caveat
	// against it — "a wider stroke re-engages the mass-conserving splat and stops the bands being literal"
	// — has been REMOVED as a constraint: RefreshTrajectoryCrossingBands now derives the edges from the
	// width, which compensates the splat exactly, so width and filtering are no longer chained to the band
	// contract. This is also the surface the band-edge antialiasing research
	// (trajectoryFix\RESEARCH_PROMPT_BandEdgeAntialiasing_2026-08-10.md) is about; its "classify each
	// supersample, THEN average the colours" hypothesis would give a smooth silhouette with no band ever
	// reading low, which is the outcome both rulings were reaching for.
	TrajectoryAccumulationTexture->InitializeTexture(SquareSide, SquareSide, InitialColorValue, TF_Nearest);
	ApplyTrajectoryLOSBands();

	// InitializeTexture builds a brand new UTexture2D, so every material instance still pointing at the
	// old one would sample a dead resource. Only rebind while the trajectory surface is the one on show;
	// otherwise SetTrajectoryHeatmapEnabled does it on entry.
	if (bTrajectoryHeatmap)
	{
		UTexture2D* Rebound = TrajectoryAccumulationTexture->GetDynamicTexture();
		if (HeatmapMaterialInstance)
		{
			HeatmapMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), Rebound);
		}
		if (VoronoiMaterialInstance)
		{
			VoronoiMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), Rebound);
		}
		if (TrajectoryMaterialInstance)
		{
			TrajectoryMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), Rebound);
		}
	}
}

void AHeatmapPixelTextureVisualizer::RefreshTrajectoryDisplay() const
{
	if (!TrajectoryAccumulationTexture || !TrajectoryField.IsValid() || TrajectoryTextureSize <= 0)
	{
		return;
	}
	if (FMath::RoundToInt32(TrajectoryAccumulationTexture->GetDynamicTextureSize().X) != TrajectoryTextureSize)
	{
		return; // texture not yet resized to the grid; the next EnsureTrajectoryFieldSized fixes it
	}

	const FIntPoint Dims = TrajectoryField.GetGridDims();
	const int32 CellCount = Dims.X * Dims.Y;

	// D-F: nothing deposited and nothing to repaint means the whole refresh is skippable — the texture
	// already holds the right image. This is the case a paused or sparse playback sits in most of the time,
	// and it used to cost three full-grid walks and a multi-megabyte memset at ~10 Hz regardless.
	const bool bHasCarrier = TrajectoryPreviousRed.Num() == CellCount;
	const FIntRect PendingDirty = TrajectoryField.GetDirtyRect();
	const bool bNothingDirty = PendingDirty.Min.X >= PendingDirty.Max.X
		|| PendingDirty.Min.Y >= PendingDirty.Max.Y;
	if (bHasCarrier && bNothingDirty)
	{
		return;
	}

	TrajectoryField.EncodeToDisplay(TrajectoryMapMode, TrajectoryEncodedBGRA);
	if (TrajectoryEncodedBGRA.Num() < CellCount * FTrajectoryField::BytesPerPixel)
	{
		return;
	}

	const bool bFullRewrite = TrajectoryPreviousRed.Num() != CellCount;
	if (bFullRewrite)
	{
		// The previous encode is unknown (first refresh, a resize, a mode switch, or a D8 clear), and on a
		// mode switch a cell that had a value may now be zero. Memset the carrier once and then write only
		// the cells that carry a value, rather than pushing several million zero writes through
		// SetPixelColor.
		TrajectoryAccumulationTexture->ClearTexture();
		TrajectoryPreviousRed.Reset();
		TrajectoryPreviousRed.AddZeroed(CellCount);
	}

	// D-F: only walk what changed. A full rewrite has to visit everything (the memset above means every
	// non-zero cell must be re-pushed); otherwise the field's dirty rect bounds it to the cells an agent
	// actually touched since the last refresh. On a stadium-sized floor that is the difference between
	// ~17 M cell visits per refresh and a few thousand — and the old cost was a function of FLOOR AREA, so
	// an almost-empty building paid exactly as much as a full one.
	const FIntRect Dirty = TrajectoryField.GetDirtyRect();
	const int32 ScanX0 = bFullRewrite ? 0 : FMath::Clamp(Dirty.Min.X, 0, Dims.X);
	const int32 ScanY0 = bFullRewrite ? 0 : FMath::Clamp(Dirty.Min.Y, 0, Dims.Y);
	const int32 ScanX1 = bFullRewrite ? Dims.X : FMath::Clamp(Dirty.Max.X, 0, Dims.X);
	const int32 ScanY1 = bFullRewrite ? Dims.Y : FMath::Clamp(Dirty.Max.Y, 0, Dims.Y);

	for (int32 CellY = ScanY0; CellY < ScanY1; ++CellY)
	{
		for (int32 CellX = ScanX0; CellX < ScanX1; ++CellX)
		{
			const int32 CellIndex = CellY * Dims.X + CellX;
			const uint8 Red = TrajectoryEncodedBGRA[CellIndex * FTrajectoryField::BytesPerPixel
				+ FTrajectoryField::ChannelOffsetR];
			// After the memset above, "unchanged" for a full rewrite means "still zero".
			if (bFullRewrite ? Red == 0 : TrajectoryPreviousRed[CellIndex] == Red)
			{
				continue;
			}
			TrajectoryPreviousRed[CellIndex] = Red;

			// +0.5/255 defeats COLOR_TO_BYTE's truncating cast: Red/255.0f can land a hair under the
			// integer and store Red-1, which would put a cell one route-intensity band too low.
			const FLinearColor CellColour((static_cast<float>(Red) + 0.5f) / 255.0f, 0.0f, 0.0f, 1.0f);
			TrajectoryAccumulationTexture->SetPixelColor(CellX + TrajectoryTexelOffset.X,
				CellY + TrajectoryTexelOffset.Y, CellColour, /*bAddColourToExisting*/ false);
		}
	}

	// Auto-exposure rescales as the maximum cell grows, so the band edges are only meaningful next to the
	// scale that produced them. Re-push them every refresh; five scalar writes at flush rate is free.
	ApplyTrajectoryLOSBands();

	// D-F: the region has been consumed. Cleared AFTER the upload, not before the loop — an early return
	// between the two would otherwise drop a deposit's cells on the floor permanently, and the field would
	// stay correct while the texture silently lost a stroke.
	TrajectoryField.ClearDirtyRect();

	// The trajectory material binds directly to this raw texture while the mode is active.
	// No copy, blur, or other post-process can alter the sampled values.
	TrajectoryAccumulationTexture->UpdateTextureRender();
}

bool AHeatmapPixelTextureVisualizer::TrajectoryWorldToCell(const FVector& WorldLocation, FIntPoint& OutCell) const
{
	OutCell = FIntPoint(-1, -1);
	if (!TrajectoryField.IsValid())
	{
		return false;
	}

	// A0 fix (A8 finding): defer to the field's own rule instead of re-deriving it with FloorToInt32.
	// A bare floor() is upper-index-owns, which contradicts the ratified lower-index rule the DDA deposits
	// with: a point exactly on a grid line resolved one cell too high here, and a point on the grid's far
	// edge was rejected outright where the field's closed box accepts it into the last cell. Since this
	// feeds WorldToTexelForTesting, any Tier B expectation built on a boundary-aligned world coordinate was
	// silently pointing at the wrong texel - and boundary coordinates are exactly what a test picks.
	return TrajectoryField.WorldToCell(FVector2D(WorldLocation.X, WorldLocation.Y), OutCell);
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
	
	// TF_Bilinear for the same reason as the trajectory texture above: the density surface also carries its
	// value in RED and bands it in-material, so interpolating the scalar smooths the band edges without
	// inventing colours. This is the fix for the pixelated density heatmap (ruling A0-72) -- the Gaussian
	// blur was never the problem, it runs on every populated frame; point sampling was drawing every texel
	// as a hard square regardless of how smooth the underlying values were.
	DynamicTexture->InitializeTexture(TextureWidth, TextureHeight, InitialColorValue, TF_Bilinear);
	if (!TrajectoryAccumulationTexture)
	{
		TrajectoryAccumulationTexture = NewObject<UDynamicPixelRenderingTexture>(this, TEXT("TrajectoryAccumulationTexture"));
	}
	// The trajectory carrier is square and sized from the canonical grid, not from TextureWidth. Sizing
	// runs here only when the field is already known; InitializeHeatmap calls SetupDynamicTexture before
	// UpdateHeatmapMeshBounds, so on the first pass this falls back to the density size and
	// EnsureTrajectoryFieldSized re-creates it a moment later.
	{
		const int32 TrajectorySide = TrajectoryTextureSize > 0 ? TrajectoryTextureSize : TextureWidth;
		// TF_Nearest, matching the other trajectory site in EnsureTrajectoryFieldSized. The two surfaces
		// now DIVERGE deliberately — density stays bilinear, trajectory does not — because trajectory
		// carries a discrete crossing count and interpolation makes ~75% of a one-texel stroke read a band
		// low. Full reasoning at the other call site. Keep the two trajectory sites in step with each
		// other; do NOT "restore consistency" by matching the density texture.
		TrajectoryAccumulationTexture->InitializeTexture(TrajectorySide, TrajectorySide, InitialColorValue, TF_Nearest);
		TrajectoryPreviousRed.Reset();
	}
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
	if (!TrajectoryAccumulationTexture || !bTrajectoryHeatmap || Segments.IsEmpty())
	{
		return;
	}

	// Lazily size on first use: a floor can start receiving segments before anything else has asked for
	// the mesh bounds. Cheap — EnsureTrajectoryFieldSized no-ops once the extent and origin match.
	if (!TrajectoryField.IsValid())
	{
		EnsureTrajectoryFieldSized();
		if (!TrajectoryField.IsValid())
		{
			return;
		}
	}

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
		// Z is dropped, not projected: the field is a plan-view surface and the floor filter in
		// UHeatmapSubsystem::UpdateHeatmapsWithTrajectorySegments has already decided this segment
		// belongs to this storey.
		const FVector2D StartCm(Segment.Start.X, Segment.Start.Y);
		const FVector2D EndCm(Segment.End.X, Segment.End.Y);

#if !UE_BUILD_SHIPPING
		// Only sampled while a capture is armed, so the disarmed cost stays at one branch.
		const double MetresBefore = bCapturing ? TrajectoryField.GetTotalPersonMetres() : 0.0;
		const double SecondsBefore = bCapturing ? TrajectoryField.GetTotalPersonSeconds() : 0.0;
#endif

		// Every segment is offered, including zero-length ones. A stationary agent deposits its
		// Delta-t into the containing cell and no person-metres, which is the whole point of Route
		// Exposure: the old degenerate-length early-out is why every queue read zero.
		TrajectoryField.DepositSegment(StartCm, EndCm, Segment.DeltaSeconds);

#if !UE_BUILD_SHIPPING
		if (bCapturing)
		{
			// Grid cells, never clamped: an off-floor sample records (-1,-1) instead of being welded onto
			// the border row, which is how the old clamp hid edge pile-up. bDrawn now means "some mass
			// actually landed in the grid", read off the accumulators rather than assumed.
			FIntPoint StartCell(-1, -1);
			FIntPoint EndCell(-1, -1);
			TrajectoryWorldToCell(Segment.Start, StartCell);
			TrajectoryWorldToCell(Segment.End, EndCell);
			const bool bDeposited = TrajectoryField.GetTotalPersonMetres() != MetresBefore
				|| TrajectoryField.GetTotalPersonSeconds() != SecondsBefore;
			Capture.RecordRaster(CaptureSimTime, FloorID, Segment.Start, Segment.End,
				StartCell, EndCell, bDeposited);
		}
#endif
	}

	RefreshTrajectoryDisplay();
}

#if !UE_BUILD_SHIPPING
FIntPoint AHeatmapPixelTextureVisualizer::WorldToTexelForTesting(const FVector& WorldLocation) const
{
	// Returns the ACCUMULATION-TEXTURE texel, i.e. the grid cell plus the letterbox offset, so a test can
	// feed it straight to GetRawPixelRed. (-1,-1) for anything off-grid: GetRawPixelChannel reads an
	// out-of-bounds coordinate as 0, so a mistaken location shows up as "untouched" rather than as a
	// neighbouring cell's value.
	FIntPoint Cell;
	if (!TrajectoryWorldToCell(WorldLocation, Cell))
	{
		return FIntPoint(-1, -1);
	}
	return Cell + TrajectoryTexelOffset;
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

	// D8 — the canonical field is the accumulation, so zeroing only the render target would leave the
	// mass behind and the next flush would repaint it. Clear() keeps the dims, config and mode, and also
	// zeroes the audit counters so post-rewind dropped mass is not attributed to pre-rewind geometry.
	TrajectoryField.Clear();
	// Empty forces the next refresh to rewrite every cell rather than diff against a stale encode.
	TrajectoryPreviousRed.Reset();
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
		// Size the field and its carrier before the material is pointed at the texture, or the MID binds
		// the pre-resize UTexture2D and the surface renders as whatever the density path last left.
		EnsureTrajectoryFieldSized();
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
	UE_LOG(LogTemp, Log, TEXT("UVScale: (%f, %f), ScaledCircleSize: %d"), UVScale.X, UVScale.Y, ScaledCircleSize);

	// The trajectory surface deliberately does NOT go through UVScale. UVScale maps the floor onto a
	// fixed 1024 texture, so its texel world size floats with building size — the D2 defect. The field
	// owns its own addressing at a fixed cm/texel and sizes its carrier texture from that.
	EnsureTrajectoryFieldSized();
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

void AHeatmapPixelTextureVisualizer::ExportTrajectorySurface(const FString& RelativeBaseName) const
{
	if (!TrajectoryAccumulationTexture || !TrajectoryField.IsValid())
	{
		return;
	}

	// Re-encode first: the buffer may hold a previous mode, and the sidecar's normalisation figure has to
	// be the one that produced the PNG that ships beside it.
	RefreshTrajectoryDisplay();

	// SaveDynamicTextureToPNG colourises a COPY of the stored buffer with the band edges pushed by
	// ApplyTrajectoryLOSBands, so the saved image bands exactly as the material does (D3 / AC12).
	TrajectoryAccumulationTexture->SaveDynamicTextureToPNG(RelativeBaseName + TEXT(".png"));

	WriteTrajectoryCanonicalExport(RelativeBaseName);
}

void AHeatmapPixelTextureVisualizer::WriteTrajectoryCanonicalExport(const FString& RelativeBaseName) const
{
	if (!TrajectoryField.IsValid())
	{
		return;
	}

	const FIntPoint Dims = TrajectoryField.GetGridDims();
	const TArray<float>& CellPersonMetres = TrajectoryField.GetCanonical(ETrajectoryMapMode::RouteUsage);
	const TArray<float>& CellPersonSeconds = TrajectoryField.GetCanonical(ETrajectoryMapMode::RouteExposure);
	if (CellPersonMetres.Num() != Dims.X * Dims.Y || CellPersonSeconds.Num() != Dims.X * Dims.Y)
	{
		return;
	}

	// ---- canonical CSV: one row per cell carrying any mass at all ------------------------------------
	FString Csv = TEXT("cell_x,cell_y,person_metres,person_seconds\n");
	Csv.Reserve(1 << 20);
	int32 NonZeroCells = 0;
	for (int32 CellY = 0; CellY < Dims.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < Dims.X; ++CellX)
		{
			const int32 CellIndex = CellY * Dims.X + CellX;
			const float Metres = CellPersonMetres[CellIndex];
			const float Seconds = CellPersonSeconds[CellIndex];
			// Either quantity qualifies: a stationary agent has seconds and no metres, and dropping those
			// rows would delete exactly the queues Route Exposure exists to show.
			if (Metres == 0.0f && Seconds == 0.0f)
			{
				continue;
			}
			++NonZeroCells;
			Csv.Appendf(TEXT("%d,%d,%.9g,%.9g\n"), CellX, CellY, Metres, Seconds);
		}
	}

	const FString SavedDir = FPaths::ProjectSavedDir();
	FFileHelper::SaveStringToFile(Csv, *(SavedDir / RelativeBaseName + TEXT(".csv")));

	// ---- metadata sidecar (MobiusPerf/analysis/METADATA_SCHEMA.md, schema_version 1) -----------------
	const float CmPerTexel = TrajectoryField.GetEffectiveCmPerTexel();
	const float CellAreaM2 = TrajectoryField.GetCellAreaSquareMetres();
	const float EncodeScale = TrajectoryField.GetLastEncodeScale();
	const bool bUsage = TrajectoryMapMode == ETrajectoryMapMode::RouteUsage;

	// Grid clipping (D7, counted by the field) plus the floor filter upstream in the subsystem. Both are
	// geometry, so both belong in dropped_*; leaving the subsystem's share out would make the four-bucket
	// identity fail for a reason that has nothing to do with this class.
	double FloorFilteredCm = 0.0;
	double FloorFilteredSeconds = 0.0;
	if (const UWorld* CurrentWorld = GetWorld())
	{
		if (const UHeatmapSubsystem* HeatmapSubsystem = CurrentWorld->GetSubsystem<UHeatmapSubsystem>())
		{
			HeatmapSubsystem->GetDroppedTrajectoryMass(this, FloorFilteredCm, FloorFilteredSeconds);
		}
	}
	const double DroppedMetres = TrajectoryField.GetDroppedPersonMetres() + FloorFilteredCm / 100.0;
	const double DroppedSeconds = TrajectoryField.GetDroppedPersonSeconds() + FloorFilteredSeconds;

	// Band edges are stored normalised to the red channel, so canonical equivalents are reconstructed
	// through the encode: byte = Density * EncodeScale and canonical = Density * cell area, hence
	// canonical_edge = edge * 255 * cell_area / EncodeScale. Zero scale means nothing was deposited yet,
	// in which case there is no defensible canonical edge and 0 is emitted rather than a fabricated one.
	auto CanonicalBandEdge = [EncodeScale, CellAreaM2](float NormalisedEdge) -> double
	{
		if (EncodeScale <= 0.0f)
		{
			return 0.0;
		}
		return static_cast<double>(NormalisedEdge) * 255.0 * static_cast<double>(CellAreaM2)
			/ static_cast<double>(EncodeScale);
	};

	// Prose only. The scale that converts these edges back to bytes is a machine-readable field
	// (encode_scale_bytes_per_display_unit) — burying a load-bearing float in this string would put it
	// beyond field_stats.py and band_fit.py, which parse the sidecar rather than read it.
	const FString BandProvenance = FString::Printf(
		TEXT("provisional (D9): quantile fit (p50/p75/p90/p97 of occupied cells) of the TechSchool ")
		TEXT("1000-agent 30 s canonical capture 20260804_212520 (28,822 occupied cells), via ")
		TEXT("MobiusPerf/analysis/band_fit.py QUANTILE proposal; first edge is a half-byte no-data ")
		TEXT("threshold, not a fitted value. Normalised against a FIXED reference density of %.9g %s ")
		TEXT("(byte 255), so these edges are absolute and comparable across captures - divide by ")
		TEXT("encode_scale_bytes_per_display_unit to recover bytes. Superseded the pre-2026-08-04 set, ")
		TEXT("which was replay-calibrated against the deleted seed-and-brush rasteriser and therefore ")
		TEXT("bore no relation to person-metres. One building, one dataset - re-fit on more. %s"),
		TrajectoryField.GetLastEncodeReferenceDensity(),
		bUsage ? TEXT("person/m") : TEXT("person*s/m^2"),
		*FDateTime::Now().ToString(TEXT("%Y-%m-%d")));

	FString Meta;
	Meta.Reserve(2048);
	Meta += TEXT("{\n");
	Meta += TEXT("  \"schema_version\": 1,\n");
	Meta.Appendf(TEXT("  \"mode\": \"%s\",\n"), bUsage ? TEXT("RouteUsage") : TEXT("RouteExposure"));
	Meta.Appendf(TEXT("  \"quantity\": \"%s\",\n"), bUsage ? TEXT("person-metres") : TEXT("person-seconds"));
	// person-seconds per square metre; written as UTF-8 middle dot + superscript two by the file writer.
	Meta.Appendf(TEXT("  \"display_unit\": \"%s\",\n"), bUsage ? TEXT("person/m") : TEXT("person·s/m²"));
	Meta.Appendf(TEXT("  \"effective_cm_per_texel\": %.9g,\n"), CmPerTexel);
	Meta.Appendf(TEXT("  \"grid_dims\": [%d, %d],\n"), Dims.X, Dims.Y);
	Meta.Appendf(TEXT("  \"floor_origin_cm\": [%.9g, %.9g],\n"),
		TrajectoryFieldOriginCm.X, TrajectoryFieldOriginCm.Y);
	// Schema keeps this as grid_dims * effective_cm_per_texel rather than the mesh extent: the grid is
	// ceil()ed up to whole cells, so the two differ by up to one cell and only this one addresses a cell.
	Meta.Appendf(TEXT("  \"floor_extent_cm\": [%.9g, %.9g],\n"),
		static_cast<double>(Dims.X) * CmPerTexel, static_cast<double>(Dims.Y) * CmPerTexel);
	Meta.Appendf(TEXT("  \"cell_area_m2\": %.9g,\n"), CellAreaM2);
	// The actor property, not GetConfig()'s copy: SetDisplayPathWidthCm can move the kernel after
	// Initialise, and this is the value that was last pushed to it.
	Meta.Appendf(TEXT("  \"display_path_width_cm\": %.9g,\n"), TrajectoryDisplayPathWidthCm);
	// Two DIFFERENT scales, and a reader needs both to invert a PNG byte back to a canonical value:
	//   normalisation_scale                  canonical (person-m / person-s) -> display unit, = 1/cell area
	//   encode_scale_bytes_per_display_unit  display unit -> the 0-255 red channel
	Meta.Appendf(TEXT("  \"normalisation_scale\": %.9g,\n"),
		CellAreaM2 > 0.0f ? 1.0 / static_cast<double>(CellAreaM2) : 0.0);
	Meta.Appendf(TEXT("  \"encode_scale_bytes_per_display_unit\": %.9g,\n"), EncodeScale);
	// Per-export auto-exposure maximum, NOT a fixed reference: two exports with different values here are
	// on different byte scales and their PNGs must not be compared pixel-for-pixel.
	Meta.Appendf(TEXT("  \"encode_max_display_unit\": %.9g,\n"), TrajectoryField.GetLastEncodeMaxDensity());
	// Mode-selected, like ApplyTrajectoryLOSBands: exporting an Exposure field with Usage's edges would
	// put a wrong-by-the-reference-ratio band set in the sidecar and silently mislead the refit.
	const FHeatmapLOSBands& ExportBands = bUsage ? TrajectoryLOSBands : TrajectoryExposureLOSBands;
	Meta.Appendf(TEXT("  \"band_edges\": [%.9g, %.9g, %.9g, %.9g, %.9g],\n"),
		CanonicalBandEdge(ExportBands.BandA), CanonicalBandEdge(ExportBands.BandB),
		CanonicalBandEdge(ExportBands.BandC), CanonicalBandEdge(ExportBands.BandD),
		CanonicalBandEdge(ExportBands.BandE));
	Meta.Appendf(TEXT("  \"band_provenance\": \"%s\",\n"), *BandProvenance);
	Meta.Appendf(TEXT("  \"total_person_metres\": %.9g,\n"), TrajectoryField.GetTotalPersonMetres());
	Meta.Appendf(TEXT("  \"total_person_seconds\": %.9g,\n"), TrajectoryField.GetTotalPersonSeconds());
	Meta.Appendf(TEXT("  \"dropped_person_metres\": %.9g,\n"), DroppedMetres);
	Meta.Appendf(TEXT("  \"dropped_person_seconds\": %.9g,\n"), DroppedSeconds);
	Meta.Appendf(TEXT("  \"rejected_person_metres\": %.9g,\n"), TrajectoryField.GetRejectedPersonMetres());
	Meta.Appendf(TEXT("  \"rejected_person_seconds\": %.9g,\n"), TrajectoryField.GetRejectedPersonSeconds());
	// No negligible_person_seconds by design: a stationary segment's seconds ARE deposited, so they show
	// up in total_person_seconds (or dropped_person_seconds if the cell was off-grid).
	Meta.Appendf(TEXT("  \"negligible_person_metres\": %.9g\n"), TrajectoryField.GetNegligiblePersonMetres());
	Meta += TEXT("}\n");

	FFileHelper::SaveStringToFile(Meta, *(SavedDir / RelativeBaseName + TEXT(".meta.json")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	UE_LOG(LogTemp, Log, TEXT("Trajectory export %s: %d/%d cells, %.6g person-m, %.6g person-s"),
		*RelativeBaseName, NonZeroCells, Dims.X * Dims.Y,
		TrajectoryField.GetTotalPersonMetres(), TrajectoryField.GetTotalPersonSeconds());
}

void AHeatmapPixelTextureVisualizer::SaveHeatmapToPNG() const
{
	// Trajectory mode exports the field, not the density render target: same presentation on disk as on
	// screen, plus the canonical CSV and its sidecar.
	if (bTrajectoryHeatmap && TrajectoryField.IsValid() && TrajectoryAccumulationTexture)
	{
		ExportTrajectorySurface(FString::Printf(TEXT("Heatmap/%s_Trajectory_%s"),
			*ActorName, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
		return;
	}

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
	if (bTrajectoryHeatmap && TrajectoryField.IsValid() && TrajectoryAccumulationTexture)
	{
		ExportTrajectorySurface(FString::Printf(TEXT("Heatmap/%s_Trajectory_SimTime_%s_Created_%s"),
			*ActorName, *CurrentTimeString, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
		return;
	}

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

	// Divide by (count - 1), NOT count. NumberOfTriangles is the VERTEX grid size (see the comment in
	// GenerateMeshVerticesUVsAndTriangles), and BuildTileBuffers places vertex gx at gx * CellSize for
	// gx = 0 .. count-1. So the mesh spans (count - 1) cells, and dividing by count made it exactly one
	// CellSize SHORTER than MeshSize while its UVs still spanned the whole texture 0..1. The texture
	// covering the full field extent was therefore compressed onto a slightly smaller mesh, displacing the
	// image by `fraction_across_mesh * CellSize`: zero at the origin corner, growing to a full cell at the
	// far corner. On a 200 m carrier that is up to 25 cm, i.e. 2.5 texels at 10 cm/texel -- the owner's
	// "about two pixels", world-fixed and position-dependent, reported 2026-08-05 (rulings A0-56/A0-60).
	//
	// No conservation test could catch it: sums are position-blind, the same blind spot that hid the row
	// orientation question. The field itself was measured correct (A0-51, A0-58).
	CellSize.X = MeshSize.X / FMath::Max(1, NumberOfTriangles.X - 1);
	CellSize.Y = MeshSize.Y / FMath::Max(1, NumberOfTriangles.Y - 1);

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

AHeatmapPixelTextureVisualizer::FTrajectoryLatticePhase
AHeatmapPixelTextureVisualizer::PlanTrajectoryLatticePhase(const FVector2D& MeshSizeCm,
                                                           const FTrajectoryFieldConfig& InConfig)
{
	FTrajectoryLatticePhase Phase;
	FTrajectoryField::ResolveGrid(MeshSizeCm.X, MeshSizeCm.Y, InConfig, Phase.CmPerTexel, Phase.GridDims);
	Phase.SquareSide = FMath::Max(Phase.GridDims.X, Phase.GridDims.Y);

	const double MajorExtentCm = FMath::Max(MeshSizeCm.X, MeshSizeCm.Y);
	const int32 SquareSide = Phase.SquareSide;
	const double CmPerTexel = static_cast<double>(Phase.CmPerTexel);

	// Per axis: the integer texel the grid starts at, the pad cells the shift needs, and the shift.
	auto PlanAxis = [SquareSide, MajorExtentCm, CmPerTexel](
		double MinorExtentCm, int32 MinorDim,
		int32& OutOffset, int32& OutPadCells, double& OutShiftCm, bool& OutAbandoned)
	{
		OutOffset = 0;
		OutPadCells = 0;
		OutShiftCm = 0.0;
		OutAbandoned = false;
		if (MajorExtentCm <= 0.0 || SquareSide <= 0 || CmPerTexel <= 0.0)
		{
			return;
		}

		// The MAJOR axis feeds MinorExtentCm == MajorExtentCm and correctly falls out at zero margin,
		// zero residual, zero pad — D-A's snap already made it exact.
		const double IdealTexels = 0.5 * static_cast<double>(SquareSide) * (1.0 - MinorExtentCm / MajorExtentCm);
		int32 Offset = FMath::RoundToInt32(IdealTexels);
		double ShiftCm = -(IdealTexels - static_cast<double>(Offset)) * CmPerTexel;
		int32 PadCells = 0;

		if (FMath::Abs(ShiftCm) > UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			if (MinorDim + 1 <= SquareSide)
			{
				// One pad cell buys the headroom the shift needs. A POSITIVE shift would start the grid
				// ABOVE the mesh's minimum corner and drop a strip there, so spend the pad cell at the
				// bottom instead: step the lattice down one whole cell and take the offset with it. Same
				// lattice, one more cell below the floor, nothing lost at either edge.
				PadCells = 1;
				if (ShiftCm > 0.0)
				{
					ShiftCm -= CmPerTexel;
					--Offset;
				}
			}
			else
			{
				// Near-square floor: the extents differ by less than one cell, so there is no room to pad
				// without growing the texture. Keep today's behaviour. Bounded and rare — the ideal margin
				// is under half a texel here by construction, and across 200k random floors this was
				// reached on 5 axes out of 400k.
				ShiftCm = 0.0;
				OutAbandoned = true;
			}
		}

		const int32 Clamped = FMath::Clamp(Offset, 0, FMath::Max(0, SquareSide - (MinorDim + PadCells)));
		if (Clamped != Offset)
		{
			// The clamp is a safety property, not a formality: an offset past SquareSide - Dim puts grid
			// cells outside the texture and every such write is silently dropped. But a clamped offset no
			// longer matches the shift computed for it, and a mismatched pair is worse than an unphased
			// one — so abandon the phasing rather than ship a lattice that disagrees with itself.
			OutOffset = FMath::Clamp(FMath::RoundToInt32(IdealTexels), 0, FMath::Max(0, SquareSide - MinorDim));
			OutAbandoned = true;
			return;
		}

		OutOffset = Clamped;
		OutPadCells = PadCells;
		OutShiftCm = ShiftCm;
	};

	bool bAbandonedX = false;
	bool bAbandonedY = false;
	PlanAxis(MeshSizeCm.X, Phase.GridDims.X,
		Phase.TexelOffset.X, Phase.ExtraGridCells.X, Phase.OriginShiftCm.X, bAbandonedX);
	PlanAxis(MeshSizeCm.Y, Phase.GridDims.Y,
		Phase.TexelOffset.Y, Phase.ExtraGridCells.Y, Phase.OriginShiftCm.Y, bAbandonedY);
	Phase.bPhaseAbandoned = bAbandonedX || bAbandonedY;

	return Phase;
}

FVector2D AHeatmapPixelTextureVisualizer::HeatmapMeshUV(const FVector2D& LocalOffsetCm,
                                                        const FVector2D& MeshSizeCm)
{
	// UV aspect correction matches the legacy CreateMeshVertexsAndUVs derivation so world-space UV maths
	// remains unchanged across tile boundaries — no seams in the dynamic-texture sampling.
	const double SpanX = MeshSizeCm.X;
	const double SpanY = MeshSizeCm.Y;
	if (SpanX <= 0.0 || SpanY <= 0.0)
	{
		return FVector2D::ZeroVector;
	}

	const bool bAdjustY = SpanX >= SpanY;
	const double AspectRatio = bAdjustY ? (SpanY / SpanX) : (SpanX / SpanY);

	double U = LocalOffsetCm.X / SpanX;
	double V = LocalOffsetCm.Y / SpanY;
	if (bAdjustY)
	{
		V = V * AspectRatio + (1.0 - AspectRatio) * 0.5;
	}
	else
	{
		U = U * AspectRatio + (1.0 - AspectRatio) * 0.5;
	}
	return FVector2D(U, V);
}

void AHeatmapPixelTextureVisualizer::BuildTileBuffers(int32 TileX0, int32 TileY0, int32 TileX1, int32 TileY1,
                                                      const FIntPoint& NumTriangles, const FVector2D& CellSize,
                                                      const TArray<FBox3d>& Quads, FHeatmapTile& Out) const
{
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

		const FVector2D LocalOffset(gx * CellSize.X, gy * CellSize.Y);
		FVector Vertex(LocalOffset.X, LocalOffset.Y, 0.1f);

		// One shared mapping, deliberately NOT re-derived here from gx/(NumTriangles.X - 1). That form and
		// LocalOffset / MeshSize are the same number only while the span invariant holds
		// ((N-1) * CellSize == MeshSize, A0-60); expressing it in world offsets means a future break in
		// that invariant shows up as a visible shift instead of the two forms quietly disagreeing.
		const int32 LocalIdx = Out.Verts.Add(Vertex);
		Out.UVs.Add(HeatmapMeshUV(LocalOffset, HeatmapMeshSize2D));
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

#if WITH_EDITOR
int32 AHeatmapPixelTextureVisualizer::CountEmittedMeshSectionsForTesting() const
{
	return IsValid(RuntimeHeatmapMeshComponent) ? RuntimeHeatmapMeshComponent->GetNumSections() : 0;
}

int32 AHeatmapPixelTextureVisualizer::CountEmittedMeshVerticesForTesting() const
{
	if (!IsValid(RuntimeHeatmapMeshComponent))
	{
		return 0;
	}
	int32 Total = 0;
	const int32 NumSections = RuntimeHeatmapMeshComponent->GetNumSections();
	for (int32 SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
	{
		if (const FProcMeshSection* Section = RuntimeHeatmapMeshComponent->GetProcMeshSection(SectionIdx))
		{
			Total += Section->ProcVertexBuffer.Num();
		}
	}
	return Total;
}

int32 AHeatmapPixelTextureVisualizer::CountEmittedMeshTrianglesForTesting() const
{
	if (!IsValid(RuntimeHeatmapMeshComponent))
	{
		return 0;
	}
	int32 TotalIndices = 0;
	const int32 NumSections = RuntimeHeatmapMeshComponent->GetNumSections();
	for (int32 SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
	{
		if (const FProcMeshSection* Section = RuntimeHeatmapMeshComponent->GetProcMeshSection(SectionIdx))
		{
			TotalIndices += Section->ProcIndexBuffer.Num();
		}
	}
	return TotalIndices / 3;
}

int32 AHeatmapPixelTextureVisualizer::CountCullingQuadsForTesting() const
{
	// Re-derives the mask rather than caching it at generation time: caching would make this report what
	// the last build used, which is the same thing right up until the geometry changes underneath it.
	ARuntimeMeshBuilder* MeshBuilder = nullptr;
	if (World)
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(World, ARuntimeMeshBuilder::StaticClass(), FoundActors);
		if (FoundActors.Num() > 0)
		{
			MeshBuilder = Cast<ARuntimeMeshBuilder>(FoundActors[0]);
		}
	}
	// FindAllQuads returns empty for a null builder, which reads identically to "builder present, no
	// geometry" — both mean no culling. The caller that needs to tell those apart must check the actor.
	return FindAllQuads(MeshBuilder).Num();
}
#endif // WITH_EDITOR

FIntPoint AHeatmapPixelTextureVisualizer::ComputeHeatmapVertexGrid(const FVector2D& MeshSizeCm)
{
	// 25 cm per cell = 4 cells per metre. static_cast rather than the implicit FIntPoint(double,double)
	// narrowing this replaced: same truncation toward zero, but stated instead of incidental, so nobody
	// "tidies" it into a rounding helper and silently shifts every grid by up to one cell.
	return FIntPoint(static_cast<int32>(MeshSizeCm.X / 25.0), static_cast<int32>(MeshSizeCm.Y / 25.0));
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

	// ALWAYS build at the 3D-capable density, whatever bIs3DHeatmap says.
	//
	// The 3D toggle is a REALTIME switch and deliberately does NOT regenerate the mesh or the texture --
	// regenerating either would cost far too much. So the geometry has to be able to carry displacement
	// from the moment it is built. This previously chose the density AT GENERATION TIME from a setting the
	// user can change afterwards: a mesh built with bIs3DHeatmap false got /250, which is 10x fewer
	// vertices per axis, and toggling 3D on later left almost nothing to displace with nothing to fix it.
	// 2D still renders flat -- displacement is a material concern, not a geometry one. Owner ruling A0-64.
	//
	// Side benefit: /250 truncates to fewer than 2 vertices per axis for any extent below 500 cm, which
	// collapses the mesh span to zero (or to no vertices at all below 250 cm). /25 stays >= 2 vertices
	// from 50 cm up, so every plausible floor is safe.
	//
	// bIs3DHeatmap stays on the signature because callers pass it and it still describes intent, but it
	// must NOT influence vertex count -- that independence is exactly what makes the toggle free.
	//
	// The grid comes from ComputeHeatmapVertexGrid, which takes no 3D flag at all, so the independence is
	// structural rather than a comment asking to be trusted. Keep this as a CALL, never a re-inlined copy:
	// an inline duplicate beside a helper is precisely the defect CalculateNumberOfTriangles already is --
	// a function the gates assert while the shipping path computes its own answer.
	(void)bIs3DHeatmap;
	FIntPoint NumTriangles = ComputeHeatmapVertexGrid(MeshSize);
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
	//
	// THROUGH SelectSurfaceMaterial, not a second hand-written ternary. This line used to read
	// `HeatmapType ? HeatmapMaterialInstance : VoronoiMaterialInstance` with no bTrajectoryHeatmap term,
	// so while a floor streamed in with the trajectory surface active every tile was bound to the DENSITY
	// instance — which by then already points at the trajectory accumulation texture. The result was
	// crossing counts banded against Fruin's density edges: one crossing (byte 26) is below the density
	// LOS_A edge of 0.1419, so single-crossing routes rendered as bare floor until emission finished.
	// Self-correcting (FinalizeTileEmit calls AssignMaterialInstanceToMesh) and therefore transient, which
	// is exactly why it survived — it only shows while tiles are still arriving. Two copies of the same
	// selection rule is the actual defect; there is now one.
	UMaterialInstanceDynamic* TargetMaterial = SelectSurfaceMaterial();

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

	// Bump BEFORE the material pass and the logging below: this is the "mesh is final" edge that
	// waiting tests key on, and everything after this point only decorates the sections.
	++CompletedTileEmitCount;

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
