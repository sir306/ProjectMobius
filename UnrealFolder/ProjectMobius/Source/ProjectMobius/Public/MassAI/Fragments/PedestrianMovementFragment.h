// Fill out your copyright notice in the Description page of Project Settings.

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
