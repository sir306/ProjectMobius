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
#include "MassEntityTypes.h"
#include "AgentRepresentationFragment.generated.h"

class AAgentRepresentationActorISM;
class ANiagaraAgentRepActor;
/**
 * 
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FAgentRepresentationFragment : public FMassSharedFragment
{
	GENERATED_BODY()

#pragma region PROPERTIES
	/** Actor Class that holds instances that are created */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	TObjectPtr<AAgentRepresentationActorISM> ActorRepresentationClass = nullptr;
	
	/** The Male Static Mesh that is used */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	UStaticMesh* MaleStaticMesh = nullptr;
	
	/** The Female Static Mesh that is used */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	UStaticMesh* FemaleStaticMesh = nullptr;
	
	/** The Material that is used for the static mesh*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	UMaterial* Material = nullptr;
	
	/** The Skeletal Mesh that is used */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	USkeletalMesh* SkeletalMesh = nullptr;
	
	///** The Skeletal Mesh Animation that is used for the skeletal mesh */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	//class UAnimInstance* Animation;
	
	//NOTE: this is were things are progressing towards
	
	/** The Niagara System Actor for the rendering of agents */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	TObjectPtr<ANiagaraAgentRepActor> NiagaraAgentRepActor = nullptr;
	
	/** Number of Male Adults */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	int32 NumberOfMaleAdults = 0;
	
	/** Number of Male Elderly */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	int32 NumberOfMaleElderly = 0;
	
	/** Number of Female Adults */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	int32 NumberOfFemaleAdults = 0;
	
	/** Number of Female Elderly */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	int32 NumberOfFemaleElderly = 0;
	
	/** Number of Female Adults */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	int32 NumberOfChildren = 0;// TODO: Maybe do different genders

	/** Number of empty wheelchairs to draw — every wheelchair agent, any age, any gender.
	 *  APPENDED, never inserted (shared fragment; inserting shifts serialized offsets).
	 *  Defaults to 0 and stays 0 for every dataset with no wheelchair agents, which is what gates
	 *  the chair upload and the chair emitter off entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	int32 NumberOfWheelchairs = 0;

	//TODO: currently use bool to switch between low spec static effect and med VAT effect -> when we use enum we will use it here too
	/** Using low spec agent render effect? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	bool bUseLowSpecAgentRenderEffect = false;
	
#pragma endregion PROPERTIES
};


/**
 * A Niagara-based fragment used in a Mass Entity system for managing
 * the representation and the number of agents in a simulation, and the rendering quality settings.
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FNiagaraStatsFragment : public FMassSharedFragment
{
	GENERATED_BODY()

public:

#pragma region PROPERTIES
	/** Actor Class that holds instances that are created */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	TWeakObjectPtr<class ANiagaraAgentRepActor> NiagaraRepresentationActor = nullptr;

	/** Number of Male Adults */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	int32 NumberOfMaleAdults = 0;

	/** Number of Male Elderly */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	int32 NumberOfMaleElderly = 0;

	/** Number of Female Adults */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	int32 NumberOfFemaleAdults = 0;

	/** Number of Female Elderly */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	int32 NumberOfFemaleElderly = 0;

	/** Number of Female Adults */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	int32 NumberOfChildren = 0;// TODO: Maybe do different genders

	/** Number of empty wheelchairs to draw — every wheelchair agent, any age, any gender.
	 *  APPENDED, never inserted (shared fragment; inserting shifts serialized offsets).
	 *  Defaults to 0 and stays 0 for every dataset with no wheelchair agents, which is what gates
	 *  the chair upload and the chair emitter off entirely.
	 *  NOTE: deliberately NOT part of UAgentRepresentation_MOP's CurrentInstanceTotal sum — every
	 *  agent still lands in exactly one HUMAN slot, so adding this would make the total exceed the
	 *  entity count and reset the Niagara data every chunk, every Execute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	int32 NumberOfWheelchairs = 0;

	//TODO: currently use bool to switch between low spec static effect and med VAT effect -> when we use enum we will use it here too
	/** Using low spec agent render effect? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresentation")
	bool bUseLowSpecAgentRenderEffect = false;

#pragma endregion PROPERTIES
};

/**
 * Tag fragment to indicate that we want to display an entity's details in UI
 */
USTRUCT()
struct PROJECTMOBIUS_API FDisplayEntityDetailsTag : public FMassTag
{
	GENERATED_BODY()
};