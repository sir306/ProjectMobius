// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "AgentRepresenatationFragment.generated.h"

class ANiagaraAgentRepActor;
/**
 * 
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FAgentRepresentationFragment : public FMassSharedFragment
{
	GENERATED_BODY()

public:
#pragma region METHODS
	/** Default Constructor */
	FAgentRepresentationFragment();

	/** Constructor With editable params */
	FAgentRepresentationFragment(TObjectPtr<class AAgentRepresentationActorISM> InActor, UStaticMesh* InMaleStaticMesh,
	                             UStaticMesh* InFemaleStaticMesh, UMaterial* InMaterial, USkeletalMesh* InSkeletalMesh);

	/** Constructor With editable params */
	FAgentRepresentationFragment(TObjectPtr<AAgentRepresentationActorISM> InActor, UStaticMesh* InMaleStaticMesh,
								 UStaticMesh* InFemaleStaticMesh, UMaterial* InMaterial, USkeletalMesh* InSkeletalMesh, ANiagaraAgentRepActor *InNiagaraActor);

	// TODO: add an array of characters that stores the the ism type, instance id and agent id - this will be used so we can do respawns of agents
	
	~FAgentRepresentationFragment();
#pragma endregion METHODS

#pragma region PROPERTIES
	/** Actor Class that holds instances that are created */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresenatation")
	TObjectPtr<AAgentRepresentationActorISM> ActorRepresentationClass;

	/** The Male Static Mesh that is used */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresenatation")
	UStaticMesh* MaleStaticMesh;

	/** The Female Static Mesh that is used */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresenatation")
	UStaticMesh* FemaleStaticMesh;

	/** The Material that is used for the static mesh*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresenatation")
	UMaterial* Material;

	/** The Skeletal Mesh that is used */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresenatation")
	USkeletalMesh* SkeletalMesh;

	///** The Skeletal Mesh Animation that is used for the skelatal mesh */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresenatation")
	//class UAnimInstance* Animation;

	//NOTE: this is were things are progressing towards
	
	/** The Niagara System Actor for the rendering of agents */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresenatation")
	TObjectPtr<ANiagaraAgentRepActor> NiagaraAgentRepActor;

	/** Number of Male Adults */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresenatation")
	int32 NumberOfMaleAdults;

	/** Number of Male Elderly */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresenatation")
	int32 NumberOfMaleElderly;

	/** Number of Female Adults */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresenatation")
	int32 NumberOfFemaleAdults;

	/** Number of Female Elderly */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresenatation")
	int32 NumberOfFemaleElderly;

	/** Number of Female Adults */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresenatation")
	int32 NumberOfChildren;// TODO: Maybe do different genders

#pragma endregion PROPERTIES
};


/**
 * A Niagara-based fragment used in a Mass Entity system for managing
 * the representation of agent actors within the Niagara framework.
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FNiagaraAgentRepFragment : public FMassSharedFragment
{
	GENERATED_BODY()

public:
#pragma region METHODS
	/** Default Constructor */
	FNiagaraAgentRepFragment();

	/** Constructor With editable params */
	FNiagaraAgentRepFragment(TWeakObjectPtr<class ANiagaraAgentRepActor> InActor);

	~FNiagaraAgentRepFragment();
#pragma endregion METHODS

#pragma region PROPERTIES
	/** Actor Class that holds instances that are created */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityRepresenatation")
	TWeakObjectPtr<class ANiagaraAgentRepActor> NiagaraRepresenatationActor;

	// TODO: Possible add the Custom Data for the Niagara Actor params in here

#pragma endregion PROPERTIES
};