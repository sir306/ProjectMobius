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

#include "QuadTreeSubsystem.h"

#include "QuadTreeDataMap.h"

UQuadTreeSubsystem::UQuadTreeSubsystem():
MaxQuadTreeSize(50),
BoundaryBox(FBox2D(FVector2D(0.0f,0.0f),FVector2D(100.0f,100.0f))),
QuadTreeDataActor(nullptr)
{
}

void UQuadTreeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UQuadTreeSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UQuadTreeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void UQuadTreeSubsystem::BuildQuadTreeMapping()
{
	// if a quadtree already exists then destroy it - the child trees should be automactically be handled by garbage collection but if not will add a method to handle this
	if(QuadTreeDataActor == nullptr)
	{
		QuadTreeDataActor->Destroy();
	}
	QuadTreeDataActor = GetWorld()->SpawnActor<AQuadTreeDataMap>(AQuadTreeDataMap::StaticClass());
	QuadTreeDataActor->Initialize(BoundaryBox, MaxQuadTreeSize, FString("RT"));
}

void UQuadTreeSubsystem::BuildQuadTreeMapping(const FVector2D InMinMovement, const FVector2D InMaxMovement, const float BoundaryDistance)
{
	UpdateBoundary(FBox2D(InMinMovement,InMaxMovement), BoundaryDistance);
	BuildQuadTreeMapping();
}

void UQuadTreeSubsystem::SetQuadTreeQuadrantSize(const float InMaxQuadSize)
{
	MaxQuadTreeSize = InMaxQuadSize;
}

void UQuadTreeSubsystem::AddEntitityToTree(FVector EntityLocation, FString& AgentHashID)
{
	// check tree not null -TODO: Add bool if we using a quad or oct tree(eventually all be oct)
	if(QuadTreeDataActor == nullptr)
	{
		// throw error a tree should always be created by this point
		ensure(QuadTreeDataActor != nullptr);
	}

	// add to the tree the subsystem is managing
	QuadTreeDataActor->AddEntityLocationToTree(FVector2D(EntityLocation.X, EntityLocation.Y), AgentHashID);

	// loop through any other trees and add the entity to them
	for(const auto& Tree : QuadTreeDataActors)
	{
		Tree->AddEntityLocationToTree(FVector2D(EntityLocation.X, EntityLocation.Y), AgentHashID);
	}
}

void UQuadTreeSubsystem::RemoveEntityFromTree(FVector EntityLocation)
{
	// throw error a tree should always be created by this point
	ensure(QuadTreeDataActor != nullptr);

	// remove the entity from the tree the subsystem is managing
	QuadTreeDataActor->RemoveEntityLocationFromTree(FVector2D(EntityLocation.X, EntityLocation.Y));

	// loop through any other trees and remove the entity from them
	for(const auto& Tree : QuadTreeDataActors)
	{
		Tree->RemoveEntityLocationFromTree(FVector2D(EntityLocation.X, EntityLocation.Y));
	}
}

void UQuadTreeSubsystem::ClearEntititesFromTrees()
{
	// throw error a tree should always be created by this point
	ensure(QuadTreeDataActor != nullptr);

	// clear the tree the subsystem is managing
	QuadTreeDataActor->ResetEntityLocationsFromTree();

	// loop through any other trees and reset them
	for(const auto& Tree : QuadTreeDataActors)
	{
		Tree->ResetEntityLocationsFromTree();
	}
}

void UQuadTreeSubsystem::AddQuadTree(AQuadTreeDataMap* QuadTreeToAdd)
{
	// check the tree is not null
	if(QuadTreeToAdd == nullptr)
	{
		// throw error a tree should always be created by this point
		ensure(QuadTreeToAdd != nullptr);
	}

	// if not null add to the array
	QuadTreeDataActor = QuadTreeToAdd;
}

void UQuadTreeSubsystem::UpdateBoundary(const FBox2D& NewBoundary, const float BoundaryDistance)
{
	BoundaryBox.Min = NewBoundary.Min - BoundaryDistance;
	BoundaryBox.Max = NewBoundary.Max + BoundaryDistance;
}
