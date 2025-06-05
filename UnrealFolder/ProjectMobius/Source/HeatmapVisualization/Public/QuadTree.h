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
#include "QuadTree.generated.h"

class AQuadTree;

/** Enum for representing tree quadrants */
enum EQuadrant
{
	BottomLeft,
	BottomRight,
	TopLeft,
	TopRight
};

/** A struct that can be used for storing pointers to the trees */
struct FQuadTree
{
	AQuadTree* BottomLeft;
	AQuadTree* BottomRight;
	AQuadTree* TopLeft;
	AQuadTree* TopRight;
};

UCLASS()
class HEATMAPVISUALIZATION_API AQuadTree : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AQuadTree();

	/**
	 * Create QuadTree With Parameters
	 * To Recursively create trees we need to pass in the required variables and if this is the root tree then
	 * we can set the parent to nullptr
	 *
	 * @param InBounds - the bounds of the tree
	 * @param InMaxWidth - the max width of the tree
	 * 
	 */
	void Initialize(const FBox2D& InBounds, float InMaxWidth);
	
	
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called every frame
	virtual void Tick(float DeltaTime) override;

	/**
	 * A method for creating the bounds for the specified quadrant
	 *
	 * @param InQuadrant - the quadrant we want to create bounds for
	 * @return FBox2D - the bounds for the specified quadrant
	 * 
	 */
	FBox2d CreateBounds(EQuadrant InQuadrant) const;

	//TODO: If a hash can be made to work out the tree location this would be a lot faster
	/**
	 * Method to recursively add entities to the tree
	 * - As all quadrants are created for density mapping it is important to add entities to the correct quadrant
	 * - And to add them to the root quadrants to improve recursion performance
	 *
	 * @param EntityLocation - the location of the entity being added
	 * 
	 */
	void AddEntityLocationToTree(const FVector2D& EntityLocation);

	/**
	 * Method to recursively remove entities from the tree
	 * TODO: Add ID hashing to remove entities - this way we don't have to recursively search for the entity and can remove it in O(TreeDepth) time
	 *
	 * @param EntityLocation - the location of the entity being removed
	 */
	void RemoveEntityLocationFromTree(const FVector2D& EntityLocation);

	/**
	 * For simplicity, we can clear all trees recursively when we update the tree with the next time step
	 * 
	 * @note - this is not the most efficient way to update the tree, but it is the simplest as we some entities may not move
	 */
	void ResetEntityLocationsFromTree();

#pragma region PUBLIC_METHODS

#pragma endregion PUBLIC_METHODS

#pragma region PUBLIC_VARIABLES
	/**
	 * Density in this Particular Tree node
	 * 
	 * - we can store number of entities in this particular quadrant
	 * - so at the root level we can assess if we need to navigate the tree when looking for data
	 * 
	 */
	int32 Density;
	
	/** Bounds of the QTree */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QuadTree")
	FBox2D Bounds;

	/**
	 * Max Width
	 * 
	 * - the max size each quadrant can be,
	 * this is so we can keep recursively creating a tree till we hit the size we want
	 * 
	 */
	float MaxWidth;

	/**
	 * Struct that stores a reference to child nodes
	 *
	 * - by default they are nullptr, and we can create them as needed
	 * - this way we can keep track of the tree structure and see when it is at the end of the tree
	 */
	TUniquePtr<FQuadTree> QuadTreeStruct;
	
#pragma endregion PUBLIC_VARIABLES

protected:
#pragma region PROTECTED_METHODS

#pragma endregion PROTECTED_METHODS

#pragma region PROTECTED_VARIABLES

#pragma endregion PROTECTED_VARIABLES

private:
#pragma region PRIVATE_METHODS

#pragma endregion PRIVATE_METHODS

#pragma region PRIVATE_VARIABLES

#pragma endregion PRIVATE_VARIABLES

};

UCLASS()
class HEATMAPVISUALIZATION_API UDensitySideViewer : public UObject
{
	GENERATED_BODY()

public:
	UDensitySideViewer();
	~UDensitySideViewer();

	bool bIsXAxis;

	
};
