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
#include "MassEntityTypes.h" // So we can use the FMassFragment
#include "PedestrianMovementFragment.generated.h"

/**
 * 
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FPedestrianMovementFragment: public FMassFragment
{
	GENERATED_BODY()
public:
#pragma region CONSTRUCTORS_DESTRUCTORS
	/** Default Constructor */
	FPedestrianMovementFragment();

	/** Explict Constuctor */
	FPedestrianMovementFragment(FVector CurrentLoc, FRotator CurrentRot, FVector TargetLoc, int32 TargetMoveIndex, int32 MaxTargetMovements);


	/** Default Destructor */
	~FPedestrianMovementFragment();

#pragma endregion CONSTRUCTORS_DESTRUCTORS



#pragma region PUBLIC_METHODS
	/** Increment Target Index */
	void IncrementTargetIndex();
#pragma endregion PUBLIC_METHODS


#pragma region PUBLIC_PROPERTIES
	// The current location of the pedestrian
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	FVector CurrentLocation;

	// The current Rotation of the pedestrian
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	FRotator CurrentRotation;

	// The current target location
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	FVector TargetLocation;

	// Index to store current target location
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	int32 TargetMovemetIndex;

	// Max number of target locations
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	int32 MaxTargetLocations;

#pragma endregion PUBLIC_PROPERTIES

};
