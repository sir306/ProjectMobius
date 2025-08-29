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
#include "FlowCounterStructs.generated.h"


/** */

struct MOBIUSCORE_API FFlowCounterZSearchLimits
{
	/** Default constructor */	
	FFlowCounterZSearchLimits() = default;
	/**
	 * Constructor to set the Z bounds of the search box
	 * @param MinZ The minimum Z value of the search box
	 * @param MaxZ The maximum Z value of the search box
	 */
	FFlowCounterZSearchLimits(float MinZ, float MaxZ)
		: MinZBounds(MinZ), MaxZBounds(MaxZ){};
	
	/** Min Z bounds of the search box */
	std::atomic<float> MinZBounds = 0.0f;
	/** Max Z bounds of the search box */
	std::atomic<float> MaxZBounds = 0.0f;

	/** Equalities check to see if a value is in the z bounds of this search limit
	 * @param[float] InZCheck The value to check if in the bounds of this search limits
	 * @return[bool] true if the value is in the bounds of this search limits, false otherwise
	 */
	bool IsInZBounds(float InZCheck) const
	{
		return (InZCheck >= MinZBounds.load() && InZCheck <= MaxZBounds.load());
	}
};

/** */
struct MOBIUSCORE_API FFlowCounterData
{
	int32 AgentID = 0; // Unique ID for the agent
	FVector Location = FVector::ZeroVector;// Location of the agent in world space

	// constructor
	FFlowCounterData() = default;
	FFlowCounterData(int32 InAgentID, const FVector& InLocation)
		: AgentID(InAgentID), Location(InLocation) {};
};

/**
 * Data structure to hold information about an agent that interacted with a flow counter.
 * This Struct should be used in conjunction with a map or dictionary where the key is the agent's unique ID.
 */
USTRUCT(BlueprintType)
struct MOBIUSCORE_API FFlowCounterCountedAgentData
{
	GENERATED_BODY()
public:
	FFlowCounterCountedAgentData();
	
	explicit FFlowCounterCountedAgentData(float InTime, FVector InIntersectionLocation, float InIntersectionThreshold)
		: TimePassedThroughCounter(InTime), IntersectionLocation(InIntersectionLocation), IntersectionThreshold(InIntersectionThreshold) {};
	
	float TimePassedThroughCounter = 0.0f; // The time the agent passed through the flow counter
	FVector IntersectionLocation = FVector::ZeroVector;// The location where the agent intersected the flow counter
	float IntersectionThreshold = 0.0f; // 0.0 to 1.0 representing the location along the flow counter line where the intersection occurred - useful for bucketing
};

/** */
USTRUCT(BlueprintType)
struct MOBIUSCORE_API FFlowCounterBucketData
{
	GENERATED_BODY()
	
public:
	FFlowCounterBucketData();

	explicit FFlowCounterBucketData(int32 InBucketID, FVector InLocationStart, FVector InLocationEnd, float InStartThreshold, float InEndThreshold);

	FFlowCounterBucketData(int32 InBucketID, FVector InLocationStart, FVector InLocationEnd, int32 InAgentCount, float InFlowRate, const TArray<int32>& InAgentIDs, float InCurrentTrackedTime)
		: BucketID(InBucketID), SegmentStart(InLocationStart), SegmentEnd(InLocationEnd), AgentCount(InAgentCount), FlowRate(InFlowRate), AgentIDs(InAgentIDs), CurrentTrackedTime(InCurrentTrackedTime) {};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Buckets")
	int32 BucketID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Buckets")
	FVector SegmentStart = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Buckets")
	FVector SegmentEnd = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Buckets")
	float StartThreshold = 0.0f; // 0.0 to 1.0 representing the start of the segment along the flow counter line

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Buckets")
	float EndThreshold = 0.0f; // 0.0 to 1.0 representing the end of the segment along the flow counter line
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Buckets")
	int32 AgentCount = 0;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Buckets")
	float FlowRate = 0.0f; // e.g., agents per minute

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Buckets")
	TArray<int32> AgentIDs = TArray<int32>();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Buckets")
	float CurrentTrackedTime = 0.0f;
};

inline FFlowCounterCountedAgentData::FFlowCounterCountedAgentData()
{
}

inline FFlowCounterBucketData::FFlowCounterBucketData()
{
	BucketID = 0;
	SegmentStart = FVector::ZeroVector;
	SegmentEnd = FVector::ZeroVector;
	AgentCount = 0;
	FlowRate = 0.0f;
	AgentIDs = TArray<int32>();
	CurrentTrackedTime = 0.0f;
}

inline FFlowCounterBucketData::FFlowCounterBucketData(int32 InBucketID, FVector InLocationStart, FVector InLocationEnd, float InStartThreshold, float InEndThreshold):
BucketID(InBucketID),
SegmentStart(InLocationStart),
SegmentEnd(InLocationEnd),
StartThreshold(InStartThreshold),
EndThreshold(InEndThreshold)
{
	AgentCount = 0;
	FlowRate = 0.0f;
	AgentIDs = TArray<int32>();
	CurrentTrackedTime = 0.0f;
}
