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
#include "Subsystems/WorldSubsystem.h"
#include "QuadTreeSubsystem.generated.h"

class AQuadTree;

/**
 * 
 */
UCLASS()
class VISUALIZATION_API UQuadTreeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
#pragma region CONSTRUCTORS_DESTRUCTORS
	// Default Constructor
	explicit UQuadTreeSubsystem();

	
#pragma endregion CONSTRUCTORS_DESTRUCTORS
	
#pragma region INHERITED_METHODS
	/**
	* Initialize the subsystem -- we can initialize other subsystems and delegates here
	* @param Collection The collection that this subsystem belongs to
	*/
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Deinitialize the subsystem */
	virtual void Deinitialize() override;

	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTimeDilationSubSystem, STATGROUP_Tickables); }

	/** Tick -- fires every frame */
	virtual void Tick(float DeltaTime) override;
#pragma endregion INHERITED_METHODS

#pragma region METHODS
public:
	void BuildQuadTreeMapping();
	void BuildQuadTreeMapping(FVector2D InMinMovement, FVector2D InMaxMovement, float BoundaryDistance);
	void SetQuadTreeQuadrantSize(float InMaxQuadSize);
	void AddEntitityToTree(FVector EntityLocation, FString& AgentHashID);
	void RemoveEntityFromTree(FVector EntityLocation);//TODO: this needs to be a hash
	void ClearEntititesFromTrees();

	/**
	 * Add any quadtrees that have already been created to the subsystem
	 *
	 * @param QuadTreeToAdd - the quadtree to add to the subsystem
	 */
	UFUNCTION(BlueprintCallable, Category = "QuadTreeSubsystem")
	void AddQuadTree(AQuadTreeDataMap* QuadTreeToAdd);
	
protected:
	void UpdateBoundary(const FBox2D& NewBoundary, float BoundaryDistance);

private:
//TODO: work out the columns and rows for the quadtree for spline graph to use
//STEP 1: Create a grid of points - need to be half the size of maxquadtree size
//STEP 2: traverse by specified amount and get the hashs for that coloumn
//STEP 3: Increment to the next row and repeat step 2 till we reach the end of the grid

	
#pragma endregion METHODS

#pragma region VARIABLES
public:
	/** Max Width of a Quadtree Quadrant */
	float MaxQuadTreeSize;
	
	/** Boundary Box XY */
	FBox2D BoundaryBox;
	
	/** QuadTree Actor this is our visualization helper */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentData")
	class AQuadTreeDataMap* QuadTreeDataActor;

	/** Quadtree Actor Array, this stores any trees that have been premade elsewhere */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentData")
	TArray<class AQuadTreeDataMap*> QuadTreeDataActors;
	
#pragma endregion VARIABLES
};

