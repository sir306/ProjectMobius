// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/SubSystems/MassRepresentation/NiagaraActorRepSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentRepresenatationFragment.h"

void UNiagaraActorRepSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	auto MassSubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	NiagaraEntityManager = MassSubsystem->GetMutableEntityManager().AsShared();
	Super::Initialize(Collection);
}

FSharedStruct UNiagaraActorRepSubsystem::GetOrCreateSharedStructForNiagaraSystem(UNiagaraSystem* NiagaraSystem)
{

	//return NiagaraEntityManager->GetOrCreateSharedFragmentByHash<FNiagaraAgentRepFragment>(ParamHash, SharedStructToReturn);
	return FSharedStruct();
}
