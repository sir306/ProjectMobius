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
// Shared Fragments to include with the processor
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentRepresenatationFragment.h"
// Tags
#include "MassAI/Tags/MassAITags.h"
// Subsystems to include with the processor
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
// actors
#include "MassAI/Actors/AgentRepresentationActorISM.h"
#include "Components/InstancedStaticMeshComponent.h"
// multithreading and async
#include "HeatmapSubsystem.h"
#include "Async/ParallelFor.h"
#include "MassAI/Subsystems/TimeDilationSubSystem.h"

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
	EntityQuery.AddRequirement<FEntityInfoFragment>(EMassFragmentAccess::ReadWrite);

	/* Add subsystem requirements */
	// Heatmap module subsystem
	EntityQuery.AddSubsystemRequirement<UHeatmapSubsystem>(EMassFragmentAccess::ReadWrite);

	// Required Query Tags
	EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::None);

	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);

	// Time Dilation Subsystem
	ProcessorRequirements.AddSubsystemRequirement<UTimeDilationSubSystem>(EMassFragmentAccess::ReadOnly);
}

void UAgentHeatmapProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	// if the time dilation subsystem is not valid attempt to get it
	if(TimeDilationSubSystem == nullptr)
	{
		TimeDilationSubSystem = ExecutionContext.GetWorld()->GetSubsystem<UTimeDilationSubSystem>();

		// in case the subsystem is not valid return and try again next tick
		if(TimeDilationSubSystem == nullptr)
		{
			return;
		}
	}
	
	
	//TODO: too many checks needs to be optimized
	// Get the time dilation subsystem and check current time step
	if(CurrentTimeStep != TimeDilationSubSystem->CurrentTimeStep) // this works by adding this to a processor requirement instead of the entity query
	{
		CurrentTimeStep = TimeDilationSubSystem->CurrentTimeStep;
		bIsPaused = TimeDilationSubSystem->bIsPaused;
	}
	// check that the pause state not changed this way if the time step hasn't changed but is about to we need to get animations moving again
	else if(bIsPaused != TimeDilationSubSystem->bIsPaused)
	{
		bIsPaused = TimeDilationSubSystem->bIsPaused;
	}
	else
	{
		//return;
	}

	// clear the heatmap locations array but maintain memory allocation size
	HeatmapLocations.Reset();
	
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context) {

		
		// Check if we are in world and registered the properties -- we use a bool so we only check once and not every variable each time
		if(!bRegisteredProperties)
		{
			RegisterProperties(Context);

			// we check again because we need to make sure we have the properties before we continue and in world 
			if(!bRegisteredProperties)
			{
				return;
			}
		}

		
		// if the number of active heatmaps changes then we need to update the heatmap count and set the last pause loop to false
		if (HeatmapSubsystem->GetHeatmapCount() != ActiveHeatmapCount)
		{
			ActiveHeatmapCount = HeatmapSubsystem->GetHeatmapCount();
			bLastPauseLoop = false; // reset the last pause loop so we can update the heatmaps
		}

		// if we swap data the last time needs to be updated
		// if (TimeDilationSubSystem->GetCurrentSimTime() == 0.0f)
		// {
		// 	LastUpdatedCurrentTime = 0.0f;
		// }
		if (TimeDilationSubSystem->GetCurrentSimTime() < LastUpdatedCurrentTime)
		{
			LastUpdatedCurrentTime = TimeDilationSubSystem->GetCurrentSimTime();
			bUpdateHeatmap = true;
		}

		// if time is the same then no heatmap update is needed
		// TODO: this is a TEMP fix by limiting the heatmap update to no more than 0.1 seconds between updates
		else if (LastUpdatedCurrentTime != TimeDilationSubSystem->GetCurrentSimTime() || LastUpdatedCurrentTime == 0.0f)// if 0 then this is first run
		{
			float TimeDifference = TimeDilationSubSystem->GetCurrentSimTime() - LastUpdatedCurrentTime;
			if(TimeDifference < 0.1f && LastUpdatedCurrentTime != 0.0f)
			{
				bUpdateHeatmap = false;
			}
			else
			{
				bUpdateHeatmap = true;
				LastUpdatedCurrentTime = TimeDilationSubSystem->GetCurrentSimTime();
			}
		
		}
		// Get the entity info fragment
		const TArrayView<FEntityInfoFragment>& EntityInfoFragment = Context.GetMutableFragmentView<FEntityInfoFragment>();
		

		// TODO: need to not empty the locations for every loop as this is causing things to reset before they are sent to the heatmap
		// TODO: need to sort a better way to memory manage the heatmap locations -> we know max agents so implement memory management for this
		
		// // Clear the heatmap locations array
		// HeatmapLocations.Empty();
		//
		// // Size the heatmap locations array to the number of entities
		// HeatmapLocations.Reserve(EntityInfoFragment.Num());

		/* TODO
		 * this for loop is not great on performance it accounts for 15 -20ms  and is causing hitches in the framerate
		 * 
		 * main problem is the logic on this loop needs adjusting to not be so heavy on the cpu and be thread safe
		 * current variables and methods are not thread safe 
		 *
		 * another big problem is the ism components -> the issue is the update instance transform can only happen on main thread
		 * 
		*/
		
		for(FEntityInfoFragment& EntityInfoFrag : EntityInfoFragment)
		{
			//HeatmapSubsystem->UpdateHeatmaps(EntityInfo.CurrentLocatiodn);
		
			// if the agent render is false and already ready to destroy we dont need to update the instance
			if(!EntityInfoFrag.bRenderAgent && EntityInfoFrag.bReadyToDestroy) 
			{
				continue;
			}
					
			// as the above checks the render state we don't need to check if the instance is valid
			HeatmapLocations.Add(EntityInfoFrag.CurrentLocation);
		}
		
		
	}));


	// TODO: this causes 4-5ms overhead likely due to parts being non async

	
	
	// are there any locations to update the heatmap with
	if(!HeatmapLocations.IsEmpty())
	{
		// update if in valid time interval
		if (bUpdateHeatmap && !bLastPauseLoop)
		{
			HeatmapSubsystem->UpdateHeatmapsWithLocations(HeatmapLocations);
		}
		
		// if we don't have heatmaps then don't perform update logic
		if (!HeatmapSubsystem->AnyHeatmapsActive())
		{
			// ensure that the last pause loop is false so when heatmaps are active again they will update
			bLastPauseLoop = false;
			HeatmapSubsystem->BroadcastTotalAgentCount(HeatmapLocations.Num());
		}
	}
	else
	{
		HeatmapSubsystem->ClearEmptyHeatmaps();//clear live tracking heatmaps
	}

	// update the pause state
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
