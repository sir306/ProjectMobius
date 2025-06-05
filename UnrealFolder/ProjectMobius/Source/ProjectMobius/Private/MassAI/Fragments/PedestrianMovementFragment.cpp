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

#include "MassAI/Fragments/PedestrianMovementFragment.h"

FPedestrianMovementFragment::FPedestrianMovementFragment()
{
	this->CurrentLocation = FVector(0, 0, 0);
	this->CurrentRotation = FRotator(0, 0, 0);
	this->TargetLocation = FVector(0, 0, 0);
	this->TargetMovemetIndex = 0;
	this->MaxTargetLocations = 0;
}

FPedestrianMovementFragment::~FPedestrianMovementFragment()
{
}

FPedestrianMovementFragment::FPedestrianMovementFragment(FVector CurrentLoc, FRotator CurrentRot, FVector TargetLoc, int32 TargetMoveIndex, int32 MaxTargetMovements)
{
	this->CurrentLocation = CurrentLoc;
	this->CurrentRotation = CurrentRot;
	this->TargetLocation = TargetLoc;
	this->TargetMovemetIndex = TargetMoveIndex;
	this->MaxTargetLocations = MaxTargetMovements;
}

void FPedestrianMovementFragment::IncrementTargetIndex()
{
	// Check if we are at the end of the target locations
	if (this->TargetMovemetIndex == this->MaxTargetLocations)
	{
		// if at the max target locations, set to max target locations as we don't want to go out of bounds
		this->TargetMovemetIndex = this->MaxTargetLocations;
	}
	else
	{
		// Increment the target index
		this->TargetMovemetIndex++;
	}
}
