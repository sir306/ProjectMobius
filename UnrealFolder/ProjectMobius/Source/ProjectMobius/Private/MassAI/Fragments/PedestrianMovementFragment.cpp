// Fill out your copyright notice in the Description page of Project Settings.


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
