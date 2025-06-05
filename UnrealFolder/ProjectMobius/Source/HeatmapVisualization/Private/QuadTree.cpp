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

#include "QuadTree.h"


// Sets default values
AQuadTree::AQuadTree():
Density(0),
MaxWidth(0),
QuadTreeStruct(nullptr)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

void AQuadTree::Initialize(const FBox2D& InBounds, const float InMaxWidth)
{
	Bounds = InBounds;
	MaxWidth = InMaxWidth;
	
	UE_LOG(LogTemp, Warning, TEXT("Initializing QuadTree"));
	// We check to see if half the bounds is greater than the max width - this way we need to create another tree
	if (Bounds.Max.X - Bounds.Min.X > MaxWidth || Bounds.Max.Y - Bounds.Min.Y > MaxWidth)
	{
		// Create the tree
		UE_LOG(LogTemp, Warning, TEXT("create QuadTree"));
		// store the tree in the QuadTree
		QuadTreeStruct = MakeUnique<FQuadTree>();
		
		// Create the bounds for each quadrant
		FBox2D BottomLeftBounds = CreateBounds(BottomLeft);
		FBox2D BottomRightBounds = CreateBounds(BottomRight);
		FBox2D TopLeftBounds = CreateBounds(TopLeft);
		FBox2D TopRightBounds = CreateBounds(TopRight);
	
		// Create the trees for each quadrant and pass the bounds to each tree
		QuadTreeStruct->BottomLeft = NewObject<AQuadTree>();
		QuadTreeStruct->BottomLeft->Initialize(BottomLeftBounds, MaxWidth);
		
		QuadTreeStruct->BottomRight = NewObject<AQuadTree>();
		QuadTreeStruct->BottomRight->Initialize(BottomRightBounds, MaxWidth);
		
		QuadTreeStruct->TopLeft = NewObject<AQuadTree>();
		QuadTreeStruct->TopLeft->Initialize(TopLeftBounds, MaxWidth);
		
		QuadTreeStruct->TopRight = NewObject<AQuadTree>();
		QuadTreeStruct->TopRight->Initialize(TopRightBounds, MaxWidth);
	}
	else
	{
		// At the final quadrant size so there is no need to create any more trees

		// store the tree in the QuadTree
		QuadTreeStruct = MakeUnique<FQuadTree>();
		// Create the trees for each quadrant and pass the bounds to each tree
		QuadTreeStruct->BottomLeft = nullptr;
		QuadTreeStruct->BottomRight = nullptr;
		QuadTreeStruct->TopLeft = nullptr;
		QuadTreeStruct->TopRight = nullptr;
		UE_LOG(LogTemp, Warning, TEXT("Initializing Empty QuadTree"));
	}
}

// Called when the game starts or when spawned
void AQuadTree::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AQuadTree::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

FBox2d AQuadTree::CreateBounds(const EQuadrant InQuadrant) const
{
	switch (InQuadrant)
	{
		case BottomLeft:
			return FBox2d(FVector2d(Bounds.Min.X, Bounds.Min.Y), FVector2d(Bounds.Min.X + (Bounds.Max.X - Bounds.Min.X) / 2, Bounds.Min.Y + (Bounds.Max.Y - Bounds.Min.Y) / 2));
		case BottomRight:
			return FBox2d(FVector2d(Bounds.Min.X + (Bounds.Max.X - Bounds.Min.X) / 2, Bounds.Min.Y), FVector2d(Bounds.Max.X, Bounds.Min.Y + (Bounds.Max.Y - Bounds.Min.Y) / 2));
		case TopLeft:
			return FBox2d(FVector2d(Bounds.Min.X, Bounds.Min.Y + (Bounds.Max.Y - Bounds.Min.Y) / 2), FVector2d(Bounds.Min.X + (Bounds.Max.X - Bounds.Min.X) / 2, Bounds.Max.Y));
		case TopRight:
			return FBox2d(FVector2d(Bounds.Min.X + (Bounds.Max.X - Bounds.Min.X) / 2, Bounds.Min.Y + (Bounds.Max.Y - Bounds.Min.Y) / 2), FVector2d(Bounds.Max.X, Bounds.Max.Y));
		default: 
			return FBox2d();
	}
}

void AQuadTree::AddEntityLocationToTree(const FVector2D& EntityLocation)
{
	// check if this location is in the bounds of this tree
	if (Bounds.IsInside(EntityLocation))
	{
		// check if we have any sub quads in this tree
		if (QuadTreeStruct->BottomLeft != nullptr)
		{
			// check which quadrant the location is in and add it to that tree
			if (QuadTreeStruct->BottomLeft->Bounds.IsInside(EntityLocation))
			{
				QuadTreeStruct->BottomLeft->AddEntityLocationToTree(EntityLocation);
			}
			else if (QuadTreeStruct->BottomRight->Bounds.IsInside(EntityLocation))
			{
				QuadTreeStruct->BottomRight->AddEntityLocationToTree(EntityLocation);
			}
			else if (QuadTreeStruct->TopLeft->Bounds.IsInside(EntityLocation))
			{
				QuadTreeStruct->TopLeft->AddEntityLocationToTree(EntityLocation);
			}
			else if (QuadTreeStruct->TopRight->Bounds.IsInside(EntityLocation))
			{
				QuadTreeStruct->TopRight->AddEntityLocationToTree(EntityLocation);
			}
		}
		else
		{
			// increment the density for this tree
			Density += 1;
		}
	}
}

void AQuadTree::RemoveEntityLocationFromTree(const FVector2D& EntityLocation)
{
	// check if this location is in the bounds of this tree
	if (Bounds.IsInside(EntityLocation))
	{
		// check if we have any sub quads in this tree
		if (QuadTreeStruct->BottomLeft != nullptr)
		{
			// check which quadrant the location is in and add it to that tree
			if (QuadTreeStruct->BottomLeft->Bounds.IsInside(EntityLocation))
			{
				QuadTreeStruct->BottomLeft->RemoveEntityLocationFromTree(EntityLocation);
			}
			else if (QuadTreeStruct->BottomRight->Bounds.IsInside(EntityLocation))
			{
				QuadTreeStruct->BottomRight->RemoveEntityLocationFromTree(EntityLocation);
			}
			else if (QuadTreeStruct->TopLeft->Bounds.IsInside(EntityLocation))
			{
				QuadTreeStruct->TopLeft->RemoveEntityLocationFromTree(EntityLocation);
			}
			else if (QuadTreeStruct->TopRight->Bounds.IsInside(EntityLocation))
			{
				QuadTreeStruct->TopRight->RemoveEntityLocationFromTree(EntityLocation);
			}
		}
		else
		{
			// increment the density for this tree
			Density -= 1;
		}
	}
	// if hashing cant be done in time then we may need to add a check to ensure we are not decreasing below zero
}

void AQuadTree::ResetEntityLocationsFromTree()
{
	// empty this trees density 
	Density = 0;

	// check if we have any sub quads in this tree and if so reset them
	if(QuadTreeStruct->BottomLeft != nullptr) // as we divide into quads we know there will always be 4 quads so only need to check 1
	{
		QuadTreeStruct->BottomLeft->ResetEntityLocationsFromTree();
		QuadTreeStruct->BottomRight->ResetEntityLocationsFromTree();
		QuadTreeStruct->TopLeft->ResetEntityLocationsFromTree();
		QuadTreeStruct->TopRight->ResetEntityLocationsFromTree();
	}
}

UDensitySideViewer::UDensitySideViewer()
{
}

UDensitySideViewer::~UDensitySideViewer()
{
}

