// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassAI/Actors/NiagaraAgentRepActor.h"
#include "MassRepresentationProcessor.h"
#include "NiagaraActorRepSubsystem.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UNiagaraActorRepSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override
	{
		// ensure that the MassEntityManager is reset
		NiagaraEntityManager.Reset();
		// ensure that the TMap of Niagara Agent Rep Actors is empty
		NiagaraAgentRepActors.Empty();
	}

	/** Get or Create the shared Niagara Actor Rep Fragment */
	FSharedStruct GetOrCreateSharedStructForNiagaraSystem(class UNiagaraSystem* NiagaraSystem);// may add other parameters later

protected:
	/** The MassEntityManager that will be linked to this subsystem */
	TSharedPtr<FMassEntityManager> NiagaraEntityManager;

	/** The TMap of Niagara Agent Rep Actors */
	UPROPERTY()
	TMap<uint32, ANiagaraAgentRepActor*> NiagaraAgentRepActors;
};
