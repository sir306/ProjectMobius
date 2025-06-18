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
#include "MassEntityTypes.h" // So we can use the FMassFragment
#include "EnumsAndStructs/VelocityVector2D.h"
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
#include "SimulationFragment.generated.h"


/**
 *
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FSimMovementSample
{
	GENERATED_BODY()
public:
	FSimMovementSample(){};
	
	FSimMovementSample(int32 InEntityID, FVector InPosition, FRotator InRotation, float InSpeed, FString InMode)
	{
		EntityID = InEntityID;
		Position = InPosition;
		Rotation = InRotation;
		Speed = InSpeed;
		Mode = InMode;
	};
	
	FSimMovementSample(int32 InEntityID, FVector InPosition, FRotator InRotation, float InSpeed, FString InMode, EPedestrianMovementBracket InMovementBracket, unsigned long InStepDurationMS, FVelocityVector2D InStepVector)
	{
		EntityID = InEntityID;
		Position = InPosition;
		Rotation = InRotation;
		Speed = InSpeed;
		Mode = InMode;
		MovementBracket = InMovementBracket;
		StepDurationMS = InStepDurationMS;
		StepVector = InStepVector;
	};

#pragma region PUBLIC_PROPERTIES
	/** Entity ID */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	int32 EntityID = 0;

	/** Position for Entity */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	FVector Position = FVector::ZeroVector;

	/** Mode for the sample */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	FString Mode = "";

	/** Rotation for Entity */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Speed for Entity */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	float Speed = 0.f;

	/** predefined movement bracket (for animation) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	EPedestrianMovementBracket MovementBracket = EPedestrianMovementBracket::Emb_NotMoving;
	
	/** step duration in milliseconds */
	unsigned long StepDurationMS = 0;

	/** Angular vectors, smoothed across estimated steps/strides */
	FVelocityVector2D StepVector = FVelocityVector2D();

#pragma endregion PUBLIC_PROPERTIES
};

/**
 * 
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FSimulationFragment : public FMassSharedFragment
{
	GENERATED_BODY()
public:
	// TODO: Add buffer method and not store all data in this struct
	// TODO: This Map logic needs improving as it is not efficient with large data sets and looping over all data is poor
	/** TMap for data the key is time and value is struct array of FSimMovementSample */
	TMap<int32, TArray<FSimMovementSample>> SimulationData = TMap<int32, TArray<FSimMovementSample>>();

	float MaxTime = 0.0f;

	///** Mapped Simulation Data */
	//TArray<TArray<TMap<int32, FSimMovementSample>>> MappedSimulationData;

};
