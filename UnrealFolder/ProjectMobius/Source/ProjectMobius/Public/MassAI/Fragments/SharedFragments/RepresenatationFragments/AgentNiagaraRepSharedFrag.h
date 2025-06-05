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
