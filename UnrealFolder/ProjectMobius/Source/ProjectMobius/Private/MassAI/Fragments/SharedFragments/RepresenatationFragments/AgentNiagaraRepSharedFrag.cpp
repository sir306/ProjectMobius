// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentNiagaraRepSharedFrag.h"


FAgentNiagaraRepSharedFrag::FAgentNiagaraRepSharedFrag()
{
	// Initialize the arrays

	// Male Adults
	MaleAdultAgentLocationAndScales = TArray<FVector4>();
	MaleAdultAgentRotations = TArray<FQuat>();
	MaleAdultAnimationStates = TArray<int32>();

	// Elderly Male
	ElderlyMaleAdultAgentLocationAndScales = TArray<FVector4>();
	ElderlyMaleAdultAgentRotations = TArray<FQuat>();
	ElderlyMaleAdultAnimationStates = TArray<int32>();

	// Female Adults
	FemaleAdultAgentLocationAndScales = TArray<FVector4>();
	FemaleAdultAgentRotations = TArray<FQuat>();
	FemaleAdultAnimationStates = TArray<int32>();

	// Elderly Female
	ElderlyFemaleAdultAgentLocationAndScales = TArray<FVector4>();
	ElderlyFemaleAdultAgentRotations = TArray<FQuat>();
	ElderlyFemaleAdultAnimationStates = TArray<int32>();

	// Children
	ChildrenAgentLocationAndScales = TArray<FVector4>();
	ChildrenAgentRotations = TArray<FQuat>();
	ChildrenAnimationStates = TArray<int32>();
}

FAgentNiagaraRepSharedFrag::~FAgentNiagaraRepSharedFrag()
{
}
