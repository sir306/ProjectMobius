// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassObserverProcessor.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentRepresenatationFragment.h"
#include "AgentRepresentation_MOP.generated.h"

struct FAgentNiagaraRepSharedFrag;
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

	/**
	 * Default setup for first spawn of agent representation
	 *
	 * @param[TArrayView<FEntityInfoFragment>] EntityInfoFrag The entity info fragment to assign to the agent representation
	 */
	void DefaultEntitySetup(const TArrayView<FEntityInfoFragment>& EntityInfoFrag, FAgentRepresentationFragment& AgentRepresentationFragment, UInstancedStaticMeshComponent* MaleISMComponent, UInstancedStaticMeshComponent* FemaleISMComponent, int32 EntityIndexOffst);

	/**
	 * Process the current entity and set up the corresponding niagara system for the demographic of this entity
	 *
	 * @param[FEntityInfoFragment] EntityInfo The entity info fragment to assign to the agent representation
	 * @param[FAgentRepresentationFragment] AgentFrag The agent representation fragment to assign to the entity
	 * @param[FAgentNiagaraRepSharedFrag] NiagaraFrag The agent niagara representation shared fragment to assign to the entity
	 * 
	 */
	static void ProcessEntity(FEntityInfoFragment& EntityInfo, FAgentRepresentationFragment& AgentFrag, FAgentNiagaraRepSharedFrag& NiagaraFrag);

	/**
	 * In the event of a miss match in indexing and offset, we need to reset the data in the niagara system and the
	 * corresponding fragments
	 *
	 * @param[FAgentRepresentationFragment] AgentFrag The agent representation fragment to assign to the entity
	 * @param[FAgentNiagaraRepSharedFrag] NiagaraFrag The agent niagara representation shared fragment to assign to the entity
	 * 
	 */
	void ResetDataInNiagaraSystem(FAgentRepresentationFragment& AgentFrag, FAgentNiagaraRepSharedFrag& NiagaraFrag);

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
	void InitializeEntityInfoAgent(int32 InEntityID, FEntityInfoFragment& EntityInfoToAssign);
	void InitializeEntityInfoAgent(FEntityInfoFragment& EntityInfoToAssign, int32 InEntityID, FString InEntityName,
	                               FString InEntitySimTimeS, float InEntityMaxSpeed, FString InEntityM_Plane,
	                               int32 InEntityMap);


private:
	int32 EntityIndexOffset = 0;

	bool bHasSpawned = false;

	FMassEntityQuery EntityQuery;
};
