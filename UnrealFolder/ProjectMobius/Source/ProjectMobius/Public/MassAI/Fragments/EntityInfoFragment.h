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
#include "EntityInfoFragment.generated.h"
//TODO: decouple this as the fragment contains to much data and not all are needed in every processor
enum class EPedestrianMovementBracket : uint8;

/**
 * Enum to define the different age demographics
 * ie Child, Adult, Elderly
 */
UENUM()
enum class EAgeDemographic : uint8
{
	/** Child */
	Ead_Child = 0,

	/** Adult */
	Ead_Adult = 1,

	/** Elderly */
	Ead_Elderly = 2,

	/** Default */
	Ead_Default = 3
};

/**
 * 
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FEntityInfoFragment: public FMassFragment
{
	GENERATED_BODY()

public:
#pragma region CONSTRUCTORS_DESTRUCTORS
	// Default Constructor
	FEntityInfoFragment();

	// Standard Constructor
	FEntityInfoFragment(int32 InEntityID, FString InEntityName, FString InEntitySimTimeS, float InEntityMaxSpeed, FString InEntityM_Plane, int32 InEntityMap);

	// Default Destructor
	~FEntityInfoFragment();

#pragma endregion CONSTRUCTORS_DESTRUCTORS
	

#pragma region PUBLIC_METHODS
#pragma endregion PUBLIC_METHODS

#pragma region PUBLIC_PROPERTIES
	/** The Entity ID */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	int32 EntityID;

	/** The Entity Name */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	FString EntityName;

	/** The Entity simTimeS */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	FString EntitySimTimeS;

	/** The Entity MaxSpeed */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	float EntityMaxSpeed;

	/** The Entity M_Plane */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	FString EntityM_Plane;

	/** Entity Map */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	int32 EntityMap;

	/** The current location of the pedestrian */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	FVector CurrentLocation;

	/** The current Rotation of the pedestrian */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	FRotator CurrentRotation;

	/** Should this agent be rendered */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianRendering")
	bool bRenderAgent;

	/** The Instance ID associated for this Entity */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianRendering")
	int32 InstanceID;

	/** Agent Gender */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianRendering")
	bool bIsMale;

	/** Agent Age Demographic */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianRendering")
	EAgeDemographic AgeDemographic = EAgeDemographic::Ead_Adult;

	/** Ready to be destroyed */
	bool bReadyToDestroy;

	/** Current Speed of the agent */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	float CurrentSpeed;

	/** Gait/Directional Speed */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	float GaitDirectionalSpeed = 0.0f;

	/** Current Movement Bracket */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	EPedestrianMovementBracket CurrentMovementBracket;

	/** Animation Changed */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianRendering")
	bool bAnimationChanged = false;

#pragma endregion PUBLIC_PROPERTIES
};
