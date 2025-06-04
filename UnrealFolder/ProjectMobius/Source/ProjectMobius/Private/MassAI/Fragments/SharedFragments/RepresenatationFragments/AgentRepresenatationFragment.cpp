// Fill out your copyright notice in the Description page of Project Settings.


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
