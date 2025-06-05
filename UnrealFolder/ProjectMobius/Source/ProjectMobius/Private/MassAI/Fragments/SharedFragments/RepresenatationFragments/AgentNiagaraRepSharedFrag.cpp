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
