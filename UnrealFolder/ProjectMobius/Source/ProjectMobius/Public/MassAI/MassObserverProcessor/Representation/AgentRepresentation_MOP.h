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
#include "MassObserverProcessor.h"
#include "NiagaraComponent.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Fragments/SharedFragments/RepresentationFragments/AgentRepresentationFragment.h"
#include "AgentRepresentation_MOP.generated.h"

struct FAgentNiagaraDataFrag;
/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UAgentRepresentation_MOP : public UMassObserverProcessor
{
	GENERATED_BODY()
	
public:
	UAgentRepresentation_MOP();

protected:
	virtual void ConfigureQueries() override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	void SetNiagaraAgentData(UNiagaraComponent* Nc, FAgentNiagaraDataFrag& NiagaraDataFrag);

	/**
	 * Process the current entity using its movement and rendering fragments.
	 * Consumes {@link FEntityMovementFragment} and {@link FEntityRenderingFragment}
	 * to populate Niagara and representation data for the agent.
	 *
	 * @param EntityMovementFrag Movement data for the entity
	 * @param EntityRenderingFrag Rendering data that will be updated with the instance id
	 * @param NiagaraStatsFrag Representation fragment that stores per-agent counts
	 * @param NiagaraDataFrag Niagara shared fragment that gathers instance data
	 */
	static void ProcessEntity(const FEntityMovementFragment& EntityMovementFrag, FEntityRenderingFragment& EntityRenderingFrag, FNiagaraStatsFragment& NiagaraStatsFrag, FAgentNiagaraDataFrag& NiagaraDataFrag);

	/**
	 * In the event of a miss match in indexing and offset, we need to reset the data in the niagara system and the
	 * corresponding fragments
	 *
	 * @param[FNiagaraStatsFragment] NiagaraStatsFrag The agent representation fragment to assign to the entity
	 * @param[FAgentNiagaraRepSharedFrag] NiagaraFrag The agent niagara representation shared fragment to assign to the entity
	 * 
	 */
	void ResetDataInNiagaraSystem(FNiagaraStatsFragment& NiagaraStatsFrag, FAgentNiagaraDataFrag& NiagaraFrag);

	/**
	 * Method to get the current spawn niagara actor or create one if not found
	 * 
	 * @param[UWorld] World The current world
	 * @return[ANiagaraAgentRepActor] return the current spawn niagara actor or creates one if not found
	 */
	static ANiagaraAgentRepActor* GetOrCreateNiagaraRepActor(UWorld* World);
	
	/**
	 * Method to Add Instances to the specified ISM Component and set the custom data values
	 *
	 * @param ISMComponent - The ISM Component to add the instance to
	 * @param InstanceTransform - The transform of the instance
	 *
	 * @return The index of the instance in the ISM Component
	 */
	static int32 AddInstanceToISMComponent(UInstancedStaticMeshComponent* ISMComponent, const FTransform& InstanceTransform);



private:
	UPROPERTY()
	int32 EntityIndexOffset = 0;

	UPROPERTY()
	bool bHasSpawned = false;

	UPROPERTY()
	FMassEntityQuery EntityQuery;
};
