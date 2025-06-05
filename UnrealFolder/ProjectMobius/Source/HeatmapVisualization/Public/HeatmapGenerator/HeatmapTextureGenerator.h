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

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeatmapTextureGenerator.generated.h"

UCLASS()
class HEATMAPVISUALIZATION_API AHeatmapTextureGenerator : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AHeatmapTextureGenerator();

	// OnConstruction is called whenever the actor is placed or the values are changed in the editor
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

#pragma region PUBLIC_METHODS
	/** Creates and assigns the materials to the instances if not already done */
	void CreateMaterialInstances();

	/*
	 * Create and setup, the render target and bind the heatmap material instance parameter to it
	 */
	void CreateAndSetupRenderTarget() const;
	
	// TODO: This method is likely going to need to be async so that we can update the render target in real time and not block the main thread
	/*
	 * Using the AgentMaterialInstance, we can update the location of the agent on the render target
	 * Giving live updates to the heatmap
	 *
	 * @param AgentLocation The location of the agent in world space
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Rendering|Methods")
	void UpdateHeatmap(const FVector& AgentLocation);
	
#pragma endregion PUBLIC_METHODS

#pragma region PUBLIC_PROPERTIES_AND_COMPONENTS
	/** The static mesh that adds the render target material to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|Mesh")
	class UStaticMeshComponent* HeatmapMesh;

	/** A 2d RenderTarget we use this, so we can update the location of agents whenever we want */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	class UTextureRenderTarget2D* HeatmapRenderTarget;

	/** The width of the Render Target Texture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	int32 TextureWidth;

	/** The height of the Render Target Texture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heatmap|MaterialsAndTextures")
	int32 TextureHeight;

	/** This is the scaled width of a pedestrian circle radius */

#pragma endregion PUBLIC_PROPERTIES_AND_COMPONENTS

private:
#pragma region PRIVATE_METHODS

#pragma endregion PRIVATE_METHODS

#pragma region PRIVATE_PROPERTIES_AND_COMPONENTS
	
	/** The Dynamic Material Instance that uses the render target as an input */
	UPROPERTY(EditAnywhere, Category = "Heatmap|MaterialsAndTextures")
	class UMaterialInstanceDynamic* HeatmapMaterialInstance;
	
	/** The Dynamic Material Instance that creates a location of an agent on the render target */
	UPROPERTY(EditAnywhere, Category = "Heatmap|MaterialsAndTextures")
	class UMaterialInstanceDynamic* AgentMaterialInstance;
	
#pragma endregion PRIVATE_PROPERTIES_AND_COMPONENTS
	
};
