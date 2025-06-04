// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "AgentNiagaraRepSharedFrag.generated.h"

/**
 * 
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FAgentNiagaraRepSharedFrag : public FMassSharedFragment
{
	GENERATED_BODY()
	
public:
	FAgentNiagaraRepSharedFrag();
	~FAgentNiagaraRepSharedFrag();

	/** Array of Agents to send to the Niagara System */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<FVector4> MaleAdultAgentLocationAndScales;

	/** Niagara System Expects Quats for rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<FQuat> MaleAdultAgentRotations;

	/** Int Array of Animation State for Male Adults */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<int32> MaleAdultAnimationStates; // TODO: likely when we bring dif age brackets we want all of them to be in one array

	/** Array of Elderly Male Agents to send to the Niagara System */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<FVector4> ElderlyMaleAdultAgentLocationAndScales;

	/** Niagara System Expects Quats for Male Elderly rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<FQuat> ElderlyMaleAdultAgentRotations;

	/** Int Array of Animation State for Male Elderly */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<int32> ElderlyMaleAdultAnimationStates;
	
	/** Array of Agents to send to the Niagara System */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<FVector4> FemaleAdultAgentLocationAndScales;

	/** Niagara System Expects Quats for rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<FQuat> FemaleAdultAgentRotations;

	/** Int Array of Animation State for Female Adults */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<int32> FemaleAdultAnimationStates; // TODO: likely when we bring dif age brackets we want all of them to be in one array

	/** Array of Elderly Female Agents to send to the Niagara System */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<FVector4> ElderlyFemaleAdultAgentLocationAndScales;

	/** Niagara System Expects Quats for Female Elderly rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<FQuat> ElderlyFemaleAdultAgentRotations;

	/** Int Array of Animation State for Female Elderly */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<int32> ElderlyFemaleAdultAnimationStates;
	
	/** Array of Children Agents to send to the Niagara System */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<FVector4> ChildrenAgentLocationAndScales;

	/** Niagara System Expects Quats for rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<FQuat> ChildrenAgentRotations;

	/** Int Array of Animation State for Female Adults */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassEntityNiagaraRepresenatation")
	TArray<int32> ChildrenAnimationStates;
		
};
