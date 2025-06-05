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

#include "SplineGraphLocationBucket.h"


USearchSegment::USearchSegment():
bIsXGraph(false)
{
}

void USearchSegment::CreateSearchSegments(USplineGraphLocationBucket* RootTree, FBox3d MaxBounds, FBox3d InBound, bool bXAxisGraph)
{
	// the bounds of this segment
	BucketSegmentBounds = InBound;

	// set the graph axis
	bIsXGraph = bXAxisGraph;

	// create the next segment
	SearchSegment = FSearchSegments();

	// are we working with the x axis graph
	if(bXAxisGraph)
	{
		// calculate the mid point of the bounds
		MidPoint = FVector(InBound.GetCenter().X, MaxBounds.Max.Y, MaxBounds.Max.Z);
		
		// check if we are at the leaf node 
		if(InBound.GetExtent().X <= MaxBounds.GetExtent().X)
		{
			// create a new segment bucket
			SegmentBucketID = RootTree->CreateNewSegmentBucket(BucketSegmentBounds.Min.X, BucketSegmentBounds.Max.X, BucketSegmentBounds);
			
			// set the left and right segment to null as we are at the leaf node
			SearchSegment.LeftSegment = nullptr;
			SearchSegment.RightSegment = nullptr;

			// set the leaf node to true
			bIsLeaf = true;

			return;
		}
	}
	else
	{
		// calculate the mid point of the bounds
		MidPoint = FVector(MaxBounds.Max.X, InBound.GetCenter().Y,MaxBounds.Max.Z);
		
		// check if we are at the leaf node
		if(InBound.GetExtent().Y <= MaxBounds.GetExtent().Y)
		{
			// create a new segment bucket
			SegmentBucketID = RootTree->CreateNewSegmentBucket(BucketSegmentBounds.Min.Y, BucketSegmentBounds.Max.Y, BucketSegmentBounds);

			// set the left and right segment to null as we are at the leaf node
			SearchSegment.LeftSegment = nullptr;
			SearchSegment.RightSegment = nullptr;
			
			// set the leaf node to true
			bIsLeaf = true;
			
			return;
		}
	}
	

	// create the left segment
	FBox3d LeftSegmentBounds = FBox3d(InBound.Min, MidPoint);
	SearchSegment.LeftSegment = NewObject<USearchSegment>(this);
	SearchSegment.LeftSegment->CreateSearchSegments(RootTree, MaxBounds, LeftSegmentBounds, bXAxisGraph);

	// are we working with the x axis graph
	if(bXAxisGraph)
	{
		// calculate the mid point of the bounds
		MidPoint = FVector(InBound.GetCenter().X, MaxBounds.Min.Y, MaxBounds.Min.Z);
		
	}
	else
	{
		// calculate the mid point of the bounds
		MidPoint = FVector(MaxBounds.Min.X, InBound.GetCenter().Y,MaxBounds.Min.Z);
	}
	
	// create the right segment
	FBox3d RightSegmentBounds = FBox3d(MidPoint, InBound.Max);
	SearchSegment.RightSegment = NewObject<USearchSegment>(this);
	SearchSegment.RightSegment->CreateSearchSegments(RootTree, MaxBounds, RightSegmentBounds, bXAxisGraph);

}

void USearchSegment::FindSearchSegment(const FVector& EntityLocation, bool& DidFindSegment, int32& InSegmentBucketID)
{
	// check if we are at the leaf node
	if(bIsLeaf)
	{
		// if leaf see if location is in this segment
		if(BucketSegmentBounds.IsInsideOrOn(EntityLocation))
		{
			DidFindSegment = true;
			InSegmentBucketID = SegmentBucketID;
			// log the segment bucket ID
			UE_LOG(LogTemp, Warning, TEXT("Segment Bucket ID: %d"), InSegmentBucketID);
			return;
		}
		else
		{
			DidFindSegment = false;
			InSegmentBucketID = -1;
			return;
		}
	}
	
	if(bIsXGraph)
	{
		// check if we are to the left or right of the mid point
		if(EntityLocation.X <= MidPoint.X)
		{
			// check if the entity is in the left segment
			SearchSegment.LeftSegment->FindSearchSegment(EntityLocation, DidFindSegment, InSegmentBucketID);
		}
		else
		{
			// check if the entity is in the right segment
			SearchSegment.RightSegment->FindSearchSegment(EntityLocation, DidFindSegment, InSegmentBucketID);
		}
	}
	else
	{
		// check if we are to the left or right of the mid point
		if(MidPoint.Y <= EntityLocation.Y)
		{
			// check if the entity is in the left segment
			SearchSegment.LeftSegment->FindSearchSegment(EntityLocation, DidFindSegment, InSegmentBucketID);
		}
		else
		{
			// check if the entity is in the right segment
			SearchSegment.RightSegment->FindSearchSegment(EntityLocation, DidFindSegment, InSegmentBucketID);
		}
	}

}

// Sets default values for this component's properties
USplineGraphLocationBucket::USplineGraphLocationBucket(): TotalEntities(0)
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void USplineGraphLocationBucket::BeginPlay()
{
	Super::BeginPlay();

	// ...
	
}


// Called every frame
void USplineGraphLocationBucket::TickComponent(float DeltaTime, ELevelTick TickType,
                                               FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

void USplineGraphLocationBucket::CreateSplineGraphLocationBuckets(FVector InSplineGraphLocation, FVector BucketContainerSize,
	int32 NumberOfSegments, bool bInIsXGraph)
{
	ResetBucketArray(); // reset the bucket array
	
	// if the number of segments is less than 1 then set it to 1 so at least one segment is created and no errors occur
	if (NumberOfSegments < 1)
	{
		NumberOfSegments = 1;
	}

	// set the graph type
	this->bIsXGraph = bInIsXGraph;

	// set the total entities to 0
	TotalEntities = 0;

	// set the bounds of the bucket segment
	BucketSegmentBounds = FBox3d(
		InSplineGraphLocation,
		FVector(
			InSplineGraphLocation.X + BucketContainerSize.X,
			InSplineGraphLocation.Y + BucketContainerSize.Y,
			InSplineGraphLocation.Z + BucketContainerSize.Z));

	// max search size
	FBox3d MaxBucketSegmentBound;
	// The Total bound area of all the segments
	FBox3d TotalBucketSegmentBound;

	/* check if the graph is on the x-axis,
	 * we want to offset the min and max by half the size of the bucket container and number of segments + 1,
	 * this keeps the center of the segments in line with the graph, and as we check the root before adding an entity we
	 * know the entity is in the bucket and don't have to worry with the search segments being offset
	 * so the graph is split into segments of equal size and looks like this
	 *		| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |  * graph is split into 10 segments lines being the points
	 *	    |----------------------------------------|  * detection bounds
	 *    |---|---|---|---|---|---|---|---|---|---|---| * search segments
	 * 
	 */	
	if(bIsXGraph)
	{
		MaxBucketSegmentBound = FBox3d(
			FVector(
			InSplineGraphLocation.X - BucketContainerSize.X / (NumberOfSegments + 1),
			InSplineGraphLocation.Y,
			InSplineGraphLocation.Z),
			FVector(
			InSplineGraphLocation.X + (BucketContainerSize.X / (NumberOfSegments + 1)),
			InSplineGraphLocation.Y + BucketContainerSize.Y,
			InSplineGraphLocation.Z + BucketContainerSize.Z));

		TotalBucketSegmentBound = FBox3d(
			FVector(
			InSplineGraphLocation.X - BucketContainerSize.X / (NumberOfSegments + 1),
			InSplineGraphLocation.Y,
			InSplineGraphLocation.Z),
			FVector(
			InSplineGraphLocation.X + (BucketContainerSize.X + (BucketContainerSize.X / (NumberOfSegments + 1))),
			InSplineGraphLocation.Y + BucketContainerSize.Y,
			InSplineGraphLocation.Z + BucketContainerSize.Z));
	}
	else
	{
		MaxBucketSegmentBound = FBox3d(
			FVector(
			InSplineGraphLocation.X,
			InSplineGraphLocation.Y - BucketContainerSize.Y / (NumberOfSegments + 1),
			InSplineGraphLocation.Z),
			FVector(
			InSplineGraphLocation.X + BucketContainerSize.X,
			InSplineGraphLocation.Y + (BucketContainerSize.Y / (NumberOfSegments + 1)),
			InSplineGraphLocation.Z + BucketContainerSize.Z));

		TotalBucketSegmentBound = FBox3d(
			FVector(
			InSplineGraphLocation.X,
			InSplineGraphLocation.Y - BucketContainerSize.Y / (NumberOfSegments + 1),
			InSplineGraphLocation.Z),
			FVector(
			InSplineGraphLocation.X + BucketContainerSize.X,
			InSplineGraphLocation.Y + (BucketContainerSize.Y + (BucketContainerSize.Y / (NumberOfSegments + 1))),
			InSplineGraphLocation.Z + BucketContainerSize.Z));
	}
	
	// create the root search segment
	CreateRootSearchSegment(MaxBucketSegmentBound, TotalBucketSegmentBound);
	
}

void USplineGraphLocationBucket::AddEntityToBucket(FVector EntityLocation)
{
	// check if the entity is inside this bucket
	if(BucketSegmentBounds.IsInsideOrOn(EntityLocation))
	{
		// we know entity is in this bucket so add it to the total
		TotalEntities++;
		
		// values used for searching the segment
		bool bDidFind = false;
		int32 BucketID = -1;

		RootSearchSegment->FindSearchSegment(EntityLocation, bDidFind, BucketID);
		
		// find which bucket it belongs too
		if(bDidFind)
		{
			IncrementBucketSegment(BucketSegments[BucketID]);

			// log the bucket segment ID and the number of entities in the segment
			UE_LOG(LogTemp, Warning, TEXT("Bucket ID: %d, Entities: %d"), BucketID, BucketSegments[BucketID].SegmentEntityDensity)
		}
		else
		{
			// log error this should not happen
			UE_LOG(LogTemp, Warning, TEXT("We should have found a segment bucket but didnt"))
			// log bounds
			UE_LOG(LogTemp, Warning, TEXT("Bounds: %s"), *BucketSegmentBounds.ToString());
			// log location of entity
			UE_LOG(LogTemp, Warning, TEXT("Entity Location: %s"), *EntityLocation.ToString());
			
			// log bucket id
			UE_LOG(LogTemp, Warning, TEXT("Bucket ID: %d"), BucketID);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Entity is not in this bucket"));

		// log location of entity
		UE_LOG(LogTemp, Warning, TEXT("Entity Location: %s"), *EntityLocation.ToString());
		// log bucket bounds
		UE_LOG(LogTemp, Warning, TEXT("Bucket Bounds: %s"), *BucketSegmentBounds.ToString());
	}
}

void USplineGraphLocationBucket::RemoveEntityFromBucketSegment(FVector EntityLocation)
{
	// check if the entity is inside this bucket
	if(BucketSegmentBounds.IsInsideOrOn(EntityLocation))
	{
		// we know entity is in this bucket so remove it from the total
		TotalEntities--;
		
		// values used for searching the segment
		bool bDidFind = false;
		int32 BucketID;

		RootSearchSegment->FindSearchSegment(EntityLocation, bDidFind, BucketID);
		
		// find which bucket it belongs too
		if(bDidFind)
		{
			RemoveEntityFromBucketSegmentByID(BucketID);
		}
		else
		{
			// log error this should not happen
			UE_LOG(LogTemp, Warning, TEXT("We should have found a segment bucket but didnt"))
		}
	}
}

void USplineGraphLocationBucket::RemoveEntityFromBucketSegmentByID(int32 BucketID)
{
	// check bucket ID is valid index
	if(BucketSegments.IsValidIndex(BucketID))
	{
		DecrementBucketSegment(BucketSegments[BucketID]);
	}
}

int32 USplineGraphLocationBucket::CreateNewSegmentBucket(float InSegmentStart, float InSegmentEnd,
	FBox3d InBucketSegmentBounds)
{
	// create new bucket segment
	FSegmentBucket NewBucket = CreateBucketSegment(InSegmentStart, InSegmentEnd, InBucketSegmentBounds);

	// add the bucket segment to the array
	BucketSegments.Add(NewBucket);

	// return the ID of the new bucket segment
	return NewBucket.SegmentID;
}

int32 USplineGraphLocationBucket::GetTotalEntitiesInSegmentBucket(int32 BucketID)
{
	// check bucket ID is valid index
	if(BucketSegments.IsValidIndex(BucketID))
	{
		return BucketSegments[BucketID].SegmentEntityDensity;
	}
	
	// return -2 if the bucket ID is not valid
	return -2;
}

void USplineGraphLocationBucket::CreateRootSearchSegment(FBox3d MaxSearchBoundSize, FBox3d TotalSearchBounds)
{
	// create the root search segment
	RootSearchSegment = NewObject<USearchSegment>(this);

	// pass initial values to the root search segment to begin recursion
	RootSearchSegment->CreateSearchSegments(this, MaxSearchBoundSize, TotalSearchBounds, bIsXGraph);
	
}

FSegmentBucket USplineGraphLocationBucket::CreateBucketSegment(float InSegmentStart, float InSegmentEnd,
                                                               FBox3d InBucketSegmentBounds)
{
	// as the bucket is added to the array after being created the ID of the will be this value
	int32 BucketID = BucketSegments.Num();

	FSegmentBucket NewBucket;
	NewBucket.SegmentID = BucketID;
	NewBucket.SegmentStart = InSegmentStart;
	NewBucket.SegmentEnd = InSegmentEnd;
	NewBucket.BucketSegmentBounds = InBucketSegmentBounds;
	NewBucket.SegmentEntityDensity = 0;

	return NewBucket;
}

void USplineGraphLocationBucket::IncrementBucketSegment(FSegmentBucket& BucketSegment)
{
	BucketSegment.SegmentEntityDensity += 1;
}

void USplineGraphLocationBucket::DecrementBucketSegment(FSegmentBucket& BucketSegment)
{
	BucketSegment.SegmentEntityDensity -= 1;
}

void USplineGraphLocationBucket::IncrementDecrementBucketSegment(FSegmentBucket& BucketSegment, int32 DecIncValue)
{
	BucketSegment.SegmentEntityDensity += DecIncValue;
}

void USplineGraphLocationBucket::ResetBucketArray()
{
	BucketSegments.Empty();
}

