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

#include "QuadTreeDataMap.h"


// Sets default values
AQuadTreeDataMap::AQuadTreeDataMap() :
Density(0),
MaxWidth(0),
QuadTreeStruct(nullptr)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// By default we set it to QRT meaning the QuadRootTree 
	QuadrantIDHash = FString("QRT");
}

void AQuadTreeDataMap::Initialize(const FBox2D& InBounds, const float InMaxWidth, FString QuadrantHash)
{
	UE_LOG(LogTemp, Warning, TEXT("Initializing QuadTree"));

	// Assign Bounds to the InBounds
	Bounds = InBounds;
	// Assign the MaxWidth
	MaxWidth = InMaxWidth;
	this->QuadrantIDHash = QuadrantHash;

	// Log The ID Hash
	UE_LOG(LogTemp, Warning, TEXT("QuadTree Hash: %s"), *QuadrantIDHash);
	
	// We check to see if half the bounds is greater than the max width - this way we need to create another tree
	if (Bounds.GetExtent().X >= MaxWidth || Bounds.GetExtent().Y >= MaxWidth)
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
		QuadTreeStruct->BottomLeft = GetWorld()->SpawnActor<AQuadTreeDataMap>();
		FString NewIDHash = QuadrantHash + "QBL";
		QuadTreeStruct->BottomLeft->Initialize(BottomLeftBounds, MaxWidth, NewIDHash);
		
		QuadTreeStruct->BottomRight = GetWorld()->SpawnActor<AQuadTreeDataMap>();
		NewIDHash = QuadrantHash + "QBR";
		QuadTreeStruct->BottomRight->Initialize(BottomRightBounds, MaxWidth, NewIDHash);
		
		QuadTreeStruct->TopLeft = GetWorld()->SpawnActor<AQuadTreeDataMap>();
		NewIDHash = QuadrantHash + "QTL";
		QuadTreeStruct->TopLeft->Initialize(TopLeftBounds, MaxWidth, NewIDHash);
		
		QuadTreeStruct->TopRight = GetWorld()->SpawnActor<AQuadTreeDataMap>();
		NewIDHash = QuadrantHash + "QTR";
		QuadTreeStruct->TopRight->Initialize(TopRightBounds, MaxWidth, NewIDHash);
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
void AQuadTreeDataMap::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AQuadTreeDataMap::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

FBox2d AQuadTreeDataMap::CreateBounds(const EQuadrant InQuadrant) const
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

void AQuadTreeDataMap::AddEntityLocationToTree(const FVector2D& EntityLocation, FString& TreeHash)
{
	// Check that the location is within the bounds of this tree
	if(Bounds.IsInsideOrOn(EntityLocation) == false)
	{
		UE_LOG(LogTemp,Warning, TEXT("Entity is not within the bounds of this tree"));
		return;
	}
	
	// increment the density for this tree
	this->Density += 1;

	// Assign this quadrants hash to the tree hash
	TreeHash = QuadrantIDHash;
		
	// Find the quadrant the entity is in
	AQuadTreeDataMap* EntityQuadrant = FindQuadrantByLocation(EntityLocation);

	// if we returned nullptr then we are at the final tree
	if(EntityQuadrant != nullptr)
	{
		// call this method to continue adding the entity to the tree
		EntityQuadrant->AddEntityLocationToTree(EntityLocation, TreeHash);
	}
}

void AQuadTreeDataMap::RemoveEntityLocationFromTree(const FVector2D& EntityLocation)
{
	// increment the density for this tree
	Density -= 1;
		
	// Find the quadrant the entity is in
	AQuadTreeDataMap* EntityQuadrant = FindQuadrantByLocation(EntityLocation);

	// if we returned nullptr then we are at the final tree
	if(EntityQuadrant != nullptr)
	{
		// call this method to continue removing the entity from the tree
		EntityQuadrant->RemoveEntityLocationFromTree(EntityLocation);
	}
	
	// if hashing cant be done in time then we may need to add a check to ensure we are not decreasing below zero
}

void AQuadTreeDataMap::ResetEntityLocationsFromTree()
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

void AQuadTreeDataMap::GetEntityDensityAtHash(const FString LocationHash, int32& EntityDensity)
{

	// Check that the location hash contains this quads id
	if(LocationHash.Contains(QuadrantIDHash))
	{
		UE_LOG(LogTemp,Warning, TEXT("LocationHash: %s"), *LocationHash);
		// strip this quadrant id hash from the left side of the location hash
		FString RemainingHash = LocationHash.RightChop(QuadrantIDHash.Len());
		UE_LOG(LogTemp,Warning, TEXT("RemainingHash: %s"), *RemainingHash);

		// log this hash and its density
		UE_LOG(LogTemp,Warning, TEXT("Hash: %s, Density: %i"), *QuadrantIDHash, Density);

		EntityDensity = Density;

		if(RemainingHash.Len()>0)
		{
			// check if we at the end tree
			if(QuadTreeStruct->BottomLeft == nullptr)
			{
				EntityDensity = 0; // set to 0 we could leave it set to the last density
				return; // need to return a density and error as the hash going deeper than there is quads
			}
			
			// if there is remaining hash then we need to keep searching for the quadrant
			FString QuadrantHash = RemainingHash.Left(3); // its been made to always have 3 chars for each quad

			if(QuadrantHash == "QBL")
			{
				QuadTreeStruct->BottomLeft->GetEntityDensityAtHash(LocationHash, EntityDensity);
			}
			else if(QuadrantHash == "QBR")
			{
				QuadTreeStruct->BottomRight->GetEntityDensityAtHash(LocationHash,EntityDensity);
			}
			else if(QuadrantHash == "QTL")
			{
				QuadTreeStruct->TopLeft->GetEntityDensityAtHash(LocationHash,EntityDensity);
			}
			else if(QuadrantHash == "QTR")
			{
				QuadTreeStruct->TopRight->GetEntityDensityAtHash(LocationHash,EntityDensity);
			}
			else
			{
				UE_LOG(LogTemp,Warning, TEXT("Shouldn't be here, the quadrant hash: %s"), *QuadrantHash);
			}
			
		}
		else
		{
			// We are at the specified quadrant and don't need to keep searching the tree
			EntityDensity = Density;
		}
	}
	else
	{
		// log this hash and its density
		UE_LOG(LogTemp,Warning, TEXT("Hash: %s, Density: %i"), *QuadrantIDHash, Density);
	}
}

AQuadTreeDataMap* AQuadTreeDataMap::FindQuadrantByLocation(const FVector2D& SearchLocation)
{
	// check that child tree structs exist
	if(QuadTreeStruct->BottomLeft == nullptr)
	{
		// return nullptr
		return nullptr;
	}
	// Center point of the bounds
	FVector2D MidPoint = Bounds.GetCenter();

	// Offset the search location
	FVector2D OffsetSearchLocation = OffsetLocation(SearchLocation);

	// is search location X less than the mid-point of the bounds if so it is on the left side
	if(MidPoint.X > OffsetSearchLocation.X)
	{
		// is the location the bottom
		if(MidPoint.Y > OffsetSearchLocation.Y)
		{
			return QuadTreeStruct->BottomLeft;
		}
		// Top
		return QuadTreeStruct->TopLeft;
	}
	
	// if previous check fails then we are right side
	// is the location the bottom
	if(MidPoint.Y > OffsetSearchLocation.Y)
	{
		return QuadTreeStruct->BottomRight;
	}
	// Top
	return QuadTreeStruct->TopRight;
}

FVector2D AQuadTreeDataMap::OffsetLocation(const FVector2D& InLocation) const
{
	return InLocation - FVector2D(this->GetActorLocation().X, this->GetActorLocation().Y);
}
