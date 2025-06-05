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

#include "HeatmapGenerator/HeatmapTextureGenerator.h"

#include "CanvasTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"



// Sets default values
AHeatmapTextureGenerator::AHeatmapTextureGenerator():
	HeatmapMesh(nullptr),
	HeatmapRenderTarget(nullptr),
	TextureWidth(1000),
	TextureHeight(1000),
	HeatmapMaterialInstance(nullptr),
	AgentMaterialInstance(nullptr)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Create the Static mesh component
	{
		HeatmapMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeatmapMesh"));
		RootComponent = HeatmapMesh;
	}
	HeatmapMaterialInstance = nullptr;
	AgentMaterialInstance = nullptr;
	AgentMaterialInstance = CreateDefaultSubobject<UMaterialInstanceDynamic>(TEXT("AgentMaterialInstance"), true);
	HeatmapMaterialInstance = CreateDefaultSubobject<UMaterialInstanceDynamic>(TEXT("HeatmapMaterialInstance"), true);
	// get the materials
	UMaterialInterface* HeatmapMaterial = LoadObject<UMaterial>(nullptr, TEXT("Material'/Game/01_Dev/NickMaster/Heatmaps/Materials/RenderTargetHeatmaps/M_HeatmapRT_V2.M_HeatmapRT_V2'"));
	UMaterialInterface* AgentMaterial = LoadObject<UMaterial>(nullptr, TEXT("Material'/Game/01_Dev/NickMaster/Heatmaps/Materials/RenderTargetHeatmaps/M_AgentHeatmapLocationV2.M_AgentHeatmapLocationV2'"));

	// Assign the materials to the instance
	// Heatmap Instance Material
	if(!HeatmapMaterialInstance && HeatmapMaterial)
	{
		HeatmapMaterialInstance = UMaterialInstanceDynamic::Create(HeatmapMaterial, this);
	}
	// AgentPos Instance Material
	if(!AgentMaterialInstance && AgentMaterial)
	{
		AgentMaterialInstance = UMaterialInstanceDynamic::Create(AgentMaterial, this);
	}
	HeatmapRenderTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("HeatmapRenderTarget"));
	CreateAndSetupRenderTarget();
}


void AHeatmapTextureGenerator::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	CreateMaterialInstances();

	// check mesh is valid
	if(HeatmapMesh->GetStaticMesh() != nullptr)
	{
		HeatmapMesh->SetMaterial(0, HeatmapMaterialInstance);
	}
}

// Called when the game starts or when spawned
void AHeatmapTextureGenerator::BeginPlay()
{
	
	//HeatmapRenderTarget = NewObject<UTextureRenderTarget2D>(this, UTextureRenderTarget2D::StaticClass());
	CreateAndSetupRenderTarget();
	CreateMaterialInstances();
	
	Super::BeginPlay();
}

// Called every frame
void AHeatmapTextureGenerator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AHeatmapTextureGenerator::CreateMaterialInstances()
{
	// // check if the instances are already created and have a material assigned to them
	// if(HeatmapMaterialInstance->GetMaterial()->IsValidLowLevel() && AgentMaterialInstance->GetMaterial()->IsValidLowLevel())
	// {
	// 	return;
	// }

	// get the materials
	UMaterialInterface* HeatmapMaterial = LoadObject<UMaterial>(nullptr, TEXT("Material'/Game/01_Dev/NickMaster/Heatmaps/Materials/RenderTargetHeatmaps/M_HeatmapRT_V2.M_HeatmapRT_V2'"));
	UMaterialInterface* AgentMaterial = LoadObject<UMaterial>(nullptr, TEXT("Material'/Game/01_Dev/NickMaster/Heatmaps/Materials/RenderTargetHeatmaps/M_AgentHeatmapLocationV2.M_AgentHeatmapLocationV2'"));

	// Assign the materials to the instance
	// Heatmap Instance Material
	if(!HeatmapMaterialInstance && HeatmapMaterial)
	{
		HeatmapMaterialInstance = UMaterialInstanceDynamic::Create(HeatmapMaterial, this);
	}
	// AgentPos Instance Material
	if(!AgentMaterialInstance && AgentMaterial)
	{
		AgentMaterialInstance = UMaterialInstanceDynamic::Create(AgentMaterial, this);
	}
}

void AHeatmapTextureGenerator::CreateAndSetupRenderTarget() const
{
	// check if the render target, static mesh and material instance is valid
	if(!HeatmapRenderTarget && !HeatmapMesh->GetStaticMesh() && !HeatmapMaterialInstance)
	{
		return; //TODO: Add better error handling
	}
	// check if in world
	if(GetWorld() == nullptr)
	{
		return; //TODO: Add better error handling
	}
	//TODO FIX THIS SIZE ISSUE
	// Get the mesh bounds so we can use it for the render target size
	FBoxSphereBounds MeshBounds = HeatmapMesh->CalcBounds(FTransform());
	// Get the size of the mesh
	FVector MeshSize = MeshBounds.BoxExtent;
	
	int32 Width = 1000;
	int32 Height = 1000;
	
	
	HeatmapRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	HeatmapRenderTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	HeatmapRenderTarget->bAutoGenerateMips = false;
	//HeatmapRenderTarget->bCanCreateUAV = false;
	HeatmapRenderTarget->InitAutoFormat(TextureWidth, TextureHeight);	
	HeatmapRenderTarget->UpdateResourceImmediate(true);

	
	//UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), HeatmapRenderTarget);
	
	UTexture* outVal;
	
	if(!HeatmapMaterialInstance->GetTextureParameterValue(FName("AgentRenderTarget"), outVal))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to get the texture parameter value from the material instance"));
	}
	// Assign the render target to the heatmap material instance parameter
	HeatmapMaterialInstance->SetTextureParameterValue(FName("AgentRenderTarget"), HeatmapRenderTarget);

}

void AHeatmapTextureGenerator::UpdateHeatmap(const FVector& AgentLocation)
{
	UWorld* World = GetWorld();
	// check if the render target, static mesh and material instance is valid
	if(!HeatmapRenderTarget && !HeatmapMesh->GetStaticMesh() && !HeatmapMaterialInstance)
	{
		return; //TODO: Add better error handling
	}

	FTransform MeshTransform = HeatmapMesh->GetComponentTransform();
	MeshTransform = MeshTransform.Inverse();

	FLinearColor AgentPosOnToMesh = FLinearColor(MeshTransform.TransformPosition(AgentLocation));
	FLinearColor outVal;
	if(!AgentMaterialInstance->GetVectorParameterValue(FName("AgentPosition"), outVal))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to get the texture parameter value from the material instance"));
	}
	
	AgentMaterialInstance->SetVectorParameterValue(FName("AgentPosition"), AgentPosOnToMesh);

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(World, HeatmapRenderTarget, AgentMaterialInstance);
	
}
