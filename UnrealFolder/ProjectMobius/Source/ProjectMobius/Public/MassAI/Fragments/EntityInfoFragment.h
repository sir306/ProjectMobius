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
#include "EnumsAndStructs/MassAIEnums.h"
#include "EntityInfoFragment.generated.h"


/**
 * 
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FEntityInfoFragment: public FMassFragment
{
	GENERATED_BODY()
	
	/** The Entity ID */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	int32 EntityID = 0;

	/** The Entity Name */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	FString EntityName = "Default[0]";

	/** The Entity simTimeS */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	FString EntitySimTimeS = "0.0";

	/** The Entity MaxSpeed */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	float EntityMaxSpeed = 1.0f;

	/** The Entity M_Plane */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	FString EntityM_Plane = "F#0";

	/** Entity Map */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	int32 EntityMap = 0;

	/** The current location of the pedestrian */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	FVector CurrentLocation = FVector::ZeroVector;

	/** The current Rotation of the pedestrian */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	FRotator CurrentRotation = FRotator::ZeroRotator;

	/** Should this agent be rendered */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianRendering")
	bool bRenderAgent = true;

	/** The Instance ID associated for this Entity */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianRendering")
	int32 InstanceID = 0;

	/** Agent Gender */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianRendering")
	bool bIsMale = true;

	/** Agent Age Demographic */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianRendering")
	EAgeDemographic AgeDemographic = EAgeDemographic::Ead_Adult;

	/** Ready to be destroyed */
	bool bReadyToDestroy = false;

	/** Current Speed of the agent */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	float CurrentSpeed = 0.0f;

	/** Gait/Directional Speed */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	float GaitDirectionalSpeed = 0.0f;

	/** Current Movement Bracket */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianMovement")
	EPedestrianMovementBracket CurrentMovementBracket  = EPedestrianMovementBracket::Emb_NotMoving;

	/** Animation Changed */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PedestrianRendering")
	bool bAnimationChanged = false;
};
