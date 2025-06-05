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

#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentRepresenatationFragment.h"

#include "MassAI/Actors/AgentRepresentationActorISM.h"
#include "MassAI/Actors/NiagaraAgentRepActor.h"

FAgentRepresentationFragment::FAgentRepresentationFragment()
{
	ActorRepresentationClass = nullptr;
	MaleStaticMesh = nullptr;
	FemaleStaticMesh = nullptr;
	Material = nullptr;
	SkeletalMesh = nullptr;
	//Animation = nullptr;
	NiagaraAgentRepActor = nullptr;
	NumberOfMaleAdults = 0;
	NumberOfMaleElderly = 0;
	NumberOfFemaleAdults = 0;
	NumberOfFemaleElderly = 0;
	NumberOfChildren = 0;
}

FAgentRepresentationFragment::FAgentRepresentationFragment(TObjectPtr<class AAgentRepresentationActorISM> InActor, UStaticMesh* InMaleStaticMesh, UStaticMesh* InFemaleStaticMesh,  UMaterial* InMaterial, USkeletalMesh* InSkeletalMesh)
{
	ActorRepresentationClass = InActor;
	MaleStaticMesh = InMaleStaticMesh;
	FemaleStaticMesh = InFemaleStaticMesh;
	Material = InMaterial;
	SkeletalMesh = InSkeletalMesh;
	//Animation = InAnimation;
	NiagaraAgentRepActor = nullptr;
	NumberOfMaleAdults = 0;
	NumberOfMaleElderly = 0;
	NumberOfFemaleAdults = 0;
	NumberOfFemaleElderly = 0;
	NumberOfChildren = 0;
}

FAgentRepresentationFragment::FAgentRepresentationFragment(TObjectPtr<AAgentRepresentationActorISM> InActor,
	UStaticMesh* InMaleStaticMesh, UStaticMesh* InFemaleStaticMesh, UMaterial* InMaterial,
	USkeletalMesh* InSkeletalMesh, ANiagaraAgentRepActor* InNiagaraActor)
{
	ActorRepresentationClass = InActor;
	MaleStaticMesh = InMaleStaticMesh;
	FemaleStaticMesh = InFemaleStaticMesh;
	Material = InMaterial;
	SkeletalMesh = InSkeletalMesh;
	//Animation = InAnimation;
	NiagaraAgentRepActor = InNiagaraActor;
	NumberOfMaleAdults = 0;
	NumberOfMaleElderly = 0;
	NumberOfFemaleAdults = 0;
	NumberOfFemaleElderly = 0;
	NumberOfChildren = 0;
}

FAgentRepresentationFragment::~FAgentRepresentationFragment()
{
}

FNiagaraAgentRepFragment::FNiagaraAgentRepFragment()
{
	NiagaraRepresenatationActor = nullptr;
}

FNiagaraAgentRepFragment::FNiagaraAgentRepFragment(TWeakObjectPtr<ANiagaraAgentRepActor> InActor)
{
	NiagaraRepresenatationActor = InActor;
}

FNiagaraAgentRepFragment::~FNiagaraAgentRepFragment()
{
	NiagaraRepresenatationActor = nullptr;
}
