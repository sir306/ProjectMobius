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

#include "MassAI/MassProcessor/Representation/AgentHeatmapProcessor.h"
// Required headers for processing entities and there fragments
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassExternalSubsystemTraits.h" // This is needed so we can use subsystems and have no compile errors
// Fragments to include with this processor
#include "MassAI/Fragments/EntityInfoFragment.h"
// Tags
#include "MassAI/Tags/MassAITags.h"
// Subsystems to include with the processor
#include "Subsystems/HeatmapSubsystem.h"
#include "Subsystems/TimeDilationSubSystem.h"
// multithreading and async
#include "Async/ParallelFor.h"

UAgentHeatmapProcessor::UAgentHeatmapProcessor():
	HeatmapSubsystem(nullptr), bRegisteredProperties(false)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);

	bRequiresGameThreadExecution = true;

	// set the variable ptrs to null
	TimeDilationSubSystem = nullptr;
}

void UAgentHeatmapProcessor::ConfigureQueries()
{
	// The Entity Query Required fragments for this processor;
	EntityQuery.AddRequirement<FEntityMovementFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FEntityRenderingFragment>(EMassFragmentAccess::ReadOnly);

	/* Add subsystem requirements */
	// Heatmap module subsystem
	//EntityQuery.AddSubsystemRequirement<UHeatmapSubsystem>(EMassFragmentAccess::ReadWrite);

	// Required Query Tags
	EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::None);

	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);

	// Time Dilation Subsystem
	ProcessorRequirements.AddSubsystemRequirement<UTimeDilationSubSystem>(EMassFragmentAccess::ReadOnly);
	ProcessorRequirements.AddSubsystemRequirement<UHeatmapSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAgentHeatmapProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(UAgentHeatmapProcessor_Execute);
	// Check if we have any entities to process and if the time subsystem is available
	if(!EntityQuery.HasMatchingEntities(EntityManager) || !EnsureTimeSubsystem(ExecutionContext))
	{
		return;
	}

	UpdateTimeStepAndPause();

	// determine if heatmaps should be updated this frame
	UpdateHeatmapInterval();

	// reuse the array storage instead of reallocating
	//HeatmapLocations.Reset();

	// Clear the location queue
	LocationQueue.ConsumeAllLifo([](const FVector&){}); // No IsEmpty() check -> IsEmpty will traverse the whole queue and then ConsumeAllLifo will traverse it again, so we just clear it directly


	LastProcessedEntityCount = 0;

	if (!bRegisteredProperties)
	{
		RegisterProperties(ExecutionContext);
		if (!bRegisteredProperties)
		{
			return; // if properties are not registered, we cannot proceed
		}
	}

	if (HeatmapSubsystem->GetHeatmapCount() != ActiveHeatmapCount)
	{
		ActiveHeatmapCount = HeatmapSubsystem->GetHeatmapCount();
		bLastPauseLoop = false;
	}

	// TODO: Getting mallocs and realloc collisions with parallel -> maybe need to look at null check or something
	//EntityQuery.ParallelForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context)
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context)
	{
		ProcessChunk(Context);
	}));

	ApplyHeatmapUpdates();

	bLastPauseLoop = bIsPaused;
}
void UAgentHeatmapProcessor::RegisterProperties(FMassExecutionContext& Context)
{
	// Check if we are in world
	if (!Context.GetWorld())
	{
		return;
	}
	
	
	if(HeatmapSubsystem == nullptr)
	{
		HeatmapSubsystem = Context.GetWorld()->GetSubsystem<UHeatmapSubsystem>();
	}

	// check if the we registered the properties
	if(HeatmapSubsystem == nullptr)
	{
		return;
	}

	// set the properties to registered
	bRegisteredProperties = true;
}
bool UAgentHeatmapProcessor::EnsureTimeSubsystem(FMassExecutionContext& Context)
{
	if (TimeDilationSubSystem == nullptr)
	{
		TimeDilationSubSystem = Context.GetWorld()->GetSubsystem<UTimeDilationSubSystem>();
	}
	return TimeDilationSubSystem != nullptr;
}

void UAgentHeatmapProcessor::UpdateTimeStepAndPause()
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(UAgentHeatmapProcessor_UpdateTimeStepAndPause);
	const float NewTimeStep = TimeDilationSubSystem->CurrentTimeStep;
	const bool NewPauseState = TimeDilationSubSystem->bIsPaused;

	if (CurrentTimeStep != NewTimeStep || bIsPaused != NewPauseState)
	{
		CurrentTimeStep = NewTimeStep;
		bIsPaused = NewPauseState;
	}
}

void UAgentHeatmapProcessor::UpdateHeatmapInterval()
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(UAgentHeatmapProcessor_UpdateHeatmapInterval);
	const float CurrentSimTime = TimeDilationSubSystem->GetCurrentSimTime();
	
	if (CurrentSimTime < LastUpdatedCurrentTime)
	{
		LastUpdatedCurrentTime = CurrentSimTime;
		bUpdateHeatmap = true;
		return;
	}
	
	if (LastUpdatedCurrentTime != CurrentSimTime || LastUpdatedCurrentTime == 0.0f)
	{
		float TimeDifference = CurrentSimTime - LastUpdatedCurrentTime;
		if (TimeDifference >= 0.1f || LastUpdatedCurrentTime == 0.0f)
		{
			bUpdateHeatmap = true;
			LastUpdatedCurrentTime = CurrentSimTime;
			return;
		}
	}
	bUpdateHeatmap = false;
}

void UAgentHeatmapProcessor::ProcessChunk(FMassExecutionContext& Context)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(UAgentHeatmapProcessor_ProcessChunk);
	// Get the required fragments and entities from the context
	const TConstArrayView<FEntityRenderingFragment> EntityRenderingFragment = Context.GetFragmentView<FEntityRenderingFragment>();
	const TConstArrayView<FEntityMovementFragment> EntityMovementFragment = Context.GetFragmentView<FEntityMovementFragment>();
	auto Entities = Context.GetEntities();

	// reserve array space to avoid reallocations as entities are added
	//HeatmapLocations.Reserve((HeatmapLocations.Num() + Entities.Num()));

	//int32 ChunkSize = HeatmapLocations.Num() == 0 ? 0 : HeatmapLocations.Num() - 1;
	
	//HeatmapLocations.SetNumUninitialized((HeatmapLocations.Num() + Entities.Num()));

	//LastProcessedEntityCount += Entities.Num();

	ParallelFor(Entities.Num(), [&](int32 i)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UAgentHeatmapProcessor_ParallelFor);
		const auto& EntityMovement = EntityMovementFragment[i];
		const auto& EntityRendering = EntityRenderingFragment[i];

		if (!EntityRendering.bRenderAgent && EntityRendering.bReadyToDestroy)
		{
			return;
		}

		LocationQueue.ProduceItem(EntityMovement.CurrentLocation);
		++LastProcessedEntityCount;
	});
	
	
	// TODO: this loop is not parallelized, implement ParallelFor for performance and a threadsafe container for HeatmapLocations for simplicity
	// for (int i = 0; i < Entities.Num(); i++)
	// {
	// 	auto EntityMovement = EntityMovementFragment[i];
	// 	auto& EntityRendering = EntityRenderingFragment[i];
	// 	
	// 	if (!EntityRendering.bRenderAgent && EntityRendering.bReadyToDestroy)
	// 	{
	// 		continue;
	// 	}
	// 	//HeatmapLocations.Add(EntityMovement.CurrentLocation);
	// 	HeatmapLocations[(ChunkSize + i)] = EntityMovement.CurrentLocation;
	// }
}

void UAgentHeatmapProcessor::ApplyHeatmapUpdates()
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(UAgentHeatmapProcessor_ApplyHeatmapUpdates);
	if (!LocationQueue.IsEmpty())
	{
		HeatmapSubsystem->BroadcastTotalAgentCount(LastProcessedEntityCount.Load());
		
		if (bUpdateHeatmap && !bLastPauseLoop)
		{
			//HeatmapSubsystem->UpdateHeatmapsWithLocations(HeatmapLocations);

			HeatmapSubsystem->UpdateHeatmapsWithLocations_Mpmc(LocationQueue);
		}

		if (!HeatmapSubsystem->AnyHeatmapsActive())
		{
			bLastPauseLoop = false;// reset to false if no heatmaps are active - this will allow heatmaps to be updated again when they are active
			
		}
		//HeatmapLocations.Empty();
	}
	else
	{
		HeatmapSubsystem->ClearEmptyHeatmaps();
	}
}
