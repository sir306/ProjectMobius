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
#include "Subsystems/HeatmapSubsystem.h"
#include "Async/ParallelFor.h"
#include "Subsystems/TimeDilationSubSystem.h"

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
        if (!EnsureTimeSubsystem(ExecutionContext))
        {
                return;
        }

        UpdateTimeStepAndPause();

        HeatmapLocations.Reset();

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
        if (CurrentTimeStep != TimeDilationSubSystem->CurrentTimeStep)
        {
                CurrentTimeStep = TimeDilationSubSystem->CurrentTimeStep;
                bIsPaused = TimeDilationSubSystem->bIsPaused;
        }
        else if (bIsPaused != TimeDilationSubSystem->bIsPaused)
        {
                bIsPaused = TimeDilationSubSystem->bIsPaused;
        }
}

void UAgentHeatmapProcessor::UpdateHeatmapInterval()
{
        if (TimeDilationSubSystem->GetCurrentSimTime() < LastUpdatedCurrentTime)
        {
                LastUpdatedCurrentTime = TimeDilationSubSystem->GetCurrentSimTime();
                bUpdateHeatmap = true;
        }
        else if (LastUpdatedCurrentTime != TimeDilationSubSystem->GetCurrentSimTime() || LastUpdatedCurrentTime == 0.0f)
        {
                float TimeDifference = TimeDilationSubSystem->GetCurrentSimTime() - LastUpdatedCurrentTime;
                if (TimeDifference < 0.1f && LastUpdatedCurrentTime != 0.0f)
                {
                        bUpdateHeatmap = false;
                }
                else
                {
                        bUpdateHeatmap = true;
                        LastUpdatedCurrentTime = TimeDilationSubSystem->GetCurrentSimTime();
                }
        }
}

void UAgentHeatmapProcessor::ProcessChunk(FMassExecutionContext& Context)
{
        if (!bRegisteredProperties)
        {
                RegisterProperties(Context);
                if (!bRegisteredProperties)
                {
                        return;
                }
        }

        if (HeatmapSubsystem->GetHeatmapCount() != ActiveHeatmapCount)
        {
                ActiveHeatmapCount = HeatmapSubsystem->GetHeatmapCount();
                bLastPauseLoop = false;
        }

        UpdateHeatmapInterval();

        const TArrayView<FEntityInfoFragment>& EntityInfoFragment = Context.GetMutableFragmentView<FEntityInfoFragment>();
        for (FEntityInfoFragment& EntityInfoFrag : EntityInfoFragment)
        {
                if (!EntityInfoFrag.bRenderAgent && EntityInfoFrag.bReadyToDestroy)
                {
                        continue;
                }
                HeatmapLocations.Add(EntityInfoFrag.CurrentLocation);
        }
}

void UAgentHeatmapProcessor::ApplyHeatmapUpdates()
{
        if (!HeatmapLocations.IsEmpty())
        {
                if (bUpdateHeatmap && !bLastPauseLoop)
                {
                        HeatmapSubsystem->UpdateHeatmapsWithLocations(HeatmapLocations);
                }

                if (!HeatmapSubsystem->AnyHeatmapsActive())
                {
                        bLastPauseLoop = false;
                        HeatmapSubsystem->BroadcastTotalAgentCount(HeatmapLocations.Num());
                }
        }
        else
        {
                HeatmapSubsystem->ClearEmptyHeatmaps();
        }
}
