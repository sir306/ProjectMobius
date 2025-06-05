// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 Nicholas R. Harding
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
