// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SplineGraphLocationBucket.generated.h"


class USplineGraphLocationBucket;
class USearchSegment;


struct FSegmentBucket
{
	/**
	* Bucket Segment ID
	* 
	* - index of the segment, to improve performance we can use an ID to know which segment we are working with,
	* - this is useful when we are working with a large number of entities and want to update the segment quickly
	*/
	int32 SegmentID;

	// The Start of the bucket segment - we can use a float for this as it will either be an x or y value
	float SegmentStart;

	// The End of the bucket segment - we can use a float for this as it will either be an x or y value
	float SegmentEnd;

	// To reduce api calls we can store the segments MidPoint as a variable
	FVector MidPoint;

	// The bounds of the bucket segment
	FBox3d BucketSegmentBounds;

	// Segment Entity Density
	int32 SegmentEntityDensity;
	
};


struct FSearchSegments
{
	/** The left segment of this SearchSegment*/
	USearchSegment* LeftSegment;// left segment
	
	/** The right segment of this SearchSegment*/
	USearchSegment* RightSegment;// right segment
	
};

UCLASS()
class USearchSegment : public UObject
{
	GENERATED_BODY()

public:
	/** Constructor */
	USearchSegment();

	/**
	 * Rercursively create the search segments, once we reach the desired size we will stop and create the bucket segments
	 *
	 * @param RootTree - the root tree - used for creating the buckets
	 * @param MaxBounds - the max bounds of a segment
	 * @param InBound - the current bounds
	 * @param bXAxisGraph - the type of graph we are working with
	 */
	void CreateSearchSegments(USplineGraphLocationBucket* RootTree, FBox3d MaxBounds, FBox3d InBound, bool bXAxisGraph);

	/**
	 * If a location is within the bounds of the segment then we will return the search segment it belongs to
	 *
	 * @param EntityLocation - the location of the entity
	 * @param DidFindSegment - did we find the segment
	 * @param InSegmentBucketID - the ID of the bucket segment once desired size is reached (-1 if not reached)
	 */
	void FindSearchSegment(const FVector& EntityLocation, bool& DidFindSegment, int32& InSegmentBucketID);

	// bool to tell if we are a leaf or not
	bool bIsLeaf = false;

	// The graph type we are working with - true for x-axis, false for y-axis
	bool bIsXGraph;
	
	// To reduce api calls we can store the segments MidPoint as a variable
	FVector MidPoint;

	// The bounds of the bucket segment
	FBox3d BucketSegmentBounds;

	// Segment Bucket ID - the ID of the bucket segment once desired size is reached (-1 if not reached)
	int32 SegmentBucketID = -1; // we could store the actual segment but this is more efficient on memory
	
	/** The Search Segment of this object */
	FSearchSegments SearchSegment;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VISUALIZATION_API USplineGraphLocationBucket : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	USplineGraphLocationBucket();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

#pragma region MY_METHODS
public:	
	/**
	 * Create the spline graph location bucket
	 *
	 * @param InSplineGraphLocation - the spline graph location
	 * @param BucketContainerSize - the bucket size
	 * @param NumberOfSegments - the size the bucket will be divided into
	 * @param bInIsXGraph - is the graph an x-axis graph or a y-axis graph
	 */
	UFUNCTION(BlueprintCallable, Category = "SplineGraphLocationBucket")
	void CreateSplineGraphLocationBuckets(FVector InSplineGraphLocation, FVector BucketContainerSize, int32 NumberOfSegments, bool bInIsXGraph);
	
	/**
	 * Add Entity to bucket segment
	 *
	 * @param EntityLocation - the entity location
	 */
	UFUNCTION(BlueprintCallable, Category = "SplineGraphLocationBucket")
	void AddEntityToBucket(FVector EntityLocation);

	/**
	 * Remove Entity from bucket segment
	 *
	 * @param EntityLocation - the entity location
	 */
	UFUNCTION(BlueprintCallable, Category = "SplineGraphLocationBucket")
	void RemoveEntityFromBucketSegment(FVector EntityLocation);

	/**
	 * Remove Entity from bucket segment by ID
	 *
	 * @param BucketID - the bucket ID to remove the entity from
	 */
	UFUNCTION(BlueprintCallable, Category = "SplineGraphLocationBucket")
	void RemoveEntityFromBucketSegmentByID(int32 BucketID);

	/**
	 * Create New Segment Bucket
	 *
	 * @param InSegmentStart - the start of the segment
	 * @param InSegmentEnd - the end of the segment
	 * @param InBucketSegmentBounds - the bounds of the bucket segment
	 * @return int32 - the ID of the new bucket segment
	 */
	int32 CreateNewSegmentBucket(float InSegmentStart, float InSegmentEnd, FBox3d InBucketSegmentBounds);
	
	/**
	 * Get the total number of entities in the specified segment bucket id
	 *
	 * @param BucketID - the bucket ID
	 * @return int32 - the total number of entities in the specified segment bucket id
	 */
	UFUNCTION(BlueprintCallable, Category = "SplineGraphLocationBucket")
	int32 GetTotalEntitiesInSegmentBucket(int32 BucketID);
	
protected:

private:
	/**
	 * Create the initial search segment root
	 *
	 * @param MaxSearchBoundSize - the max search bounds for a segment
	 * @param TotalSearchBounds - the total search bounds
	 */
	void CreateRootSearchSegment(FBox3d MaxSearchBoundSize, FBox3d TotalSearchBounds);
	
	/**
	 * Internal method for creating the bucket segments
	 *
	 * @param InSegmentStart - the start of the segment
	 * @param InSegmentEnd - the end of the segment
	 * @param InBucketSegmentBounds - the bounds of the bucket segment
	 */
	FSegmentBucket CreateBucketSegment(float InSegmentStart, float InSegmentEnd, FBox3d InBucketSegmentBounds);

	/**
	 * Internal method for incrementing a bucket segment by 1
	 *
	 * @param BucketSegment - the bucket segment
	 */
	void IncrementBucketSegment(FSegmentBucket& BucketSegment);

	/**
	 * Internal method for decrementing a bucket segment by 1
	 *
	 * @param BucketSegment - the bucket segment
	 */
	 void DecrementBucketSegment(FSegmentBucket& BucketSegment);

	/**
	 * Internal method for Incrementing and Decrementing a bucket segment by a specific value
	 *
	 * @param BucketSegment - the bucket segment
	 * @param DecIncValue - the value to increment or decrement by (Positive for increment, Negative for decrement)
	 */
	void IncrementDecrementBucketSegment(FSegmentBucket& BucketSegment, int32 DecIncValue);

	/**
	 * Reset Bucket Array
	 * If any buckets exist then clear the array, so it will be destroyed in garbage collection
	 */
	void ResetBucketArray();

#pragma endregion MY_METHODS

#pragma region MY_VARIABLES
public:

protected:

private:
	/** Box3d to represent the entire bucket segment bounds, used for quickly identifying if an entity will be in this */
	FBox3d BucketSegmentBounds;

	/** Array of Segments */
	TArray<FSegmentBucket> BucketSegments;

	/** Unique Ptr to the binary search Segment tree */
	UPROPERTY()
	USearchSegment* RootSearchSegment;

	/** Total number of entities, keeps track of the total inside this collection of segments */
	int32 TotalEntities;

	/** Is this an X-Axis graph */
	bool bIsXGraph = false;

#pragma endregion MY_VARIABLES
};
