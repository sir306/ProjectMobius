// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/MassObserverProcessor/DestroyEntities_MOP.h"
// Required headers for processing entities and there fragments
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
// Tags
#include "MassAI/Tags/MassAITags.h"

UDestroyEntities_MOP::UDestroyEntities_MOP()
{
	ObservedType = FMassEntityDeleteTag::StaticStruct();
	Operation = EMassObservedOperation::Add;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;

	bRequiresGameThreadExecution = false;
}

void UDestroyEntities_MOP::ConfigureQueries()
{
	// We are interested in all entities with the FMassEntityDeleteTag
	EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::Any);

	// Register the query
	EntityQuery.RegisterWithProcessor(*this);
}

void UDestroyEntities_MOP::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this, &EntityManager](FMassExecutionContext& Context) {
		//// Loop through all the entities and destroy them
		//for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); EntityIndex++)
		//{
		//	// Destroy the entity
		//	Context.Defer().DestroyEntity(Context.GetEntity(EntityIndex));
		//}
		//// Flush the deferred actions
		//EntityManager.FlushCommands();
	}));
	
}
