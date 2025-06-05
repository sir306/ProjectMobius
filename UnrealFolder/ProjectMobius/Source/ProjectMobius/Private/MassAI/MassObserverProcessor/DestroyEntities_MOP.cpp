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
