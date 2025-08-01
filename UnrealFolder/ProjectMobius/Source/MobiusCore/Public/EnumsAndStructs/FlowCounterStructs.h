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
	TAtomic<float> MinZBounds = 0.0f;
	/** Max Z bounds of the search box */
	TAtomic<float> MaxZBounds = 0.0f;

	/** Equalities check to see if a value is in the z bounds of this search limit
	 * @param[float] InZCheck The value to check if in the bounds of this search limits
	 * @return[bool] true if the value is in the bounds of this search limits, false otherwise
	 */
	bool IsInZBounds(float InZCheck) const
	{
		return (InZCheck >= MinZBounds && InZCheck <= MaxZBounds);
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
