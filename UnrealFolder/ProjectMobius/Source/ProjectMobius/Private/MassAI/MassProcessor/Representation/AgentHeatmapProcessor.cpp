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
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
// Tags
#include "MassAI/Tags/MassAITags.h"
// Subsystems to include with the processor
#include "MassAI/SubSystems/MassEntitySpawnSubsystem.h"
#include "SimData/ISimSampleProvider.h"
#include "Subsystems/HeatmapSubsystem.h"
#include "Subsystems/TimeDilationSubSystem.h"
// Diagnostics
#include "Diagnostics/TrajectoryCaptureRecorder.h"
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

	// Sample-boundary subdivision reads the dataset the movement processor is playing back. OPTIONAL, not
	// All: Presence::All narrows the query to archetypes that carry the fragment, which would silently drop
	// any agent archetype without it from the LIVE DENSITY path this processor also feeds. Optional leaves
	// the match set untouched and yields a null pointer where the fragment is absent.
	EntityQuery.AddSharedRequirement<FSimulationFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);

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

	// Per-frame sim-time delta for trajectory segments (RULING A0-5: per-frame, not the flush interval
	// computed inside UpdateHeatmapInterval()). That call resets LastFrameSimTime to the current sim
	// time on a dataset-tracking reset or a rewind, so FrameDeltaSeconds reads 0 on those frames.
	const float FrameSimTime = TimeDilationSubSystem->GetCurrentSimTime();
	FrameStartSimTime = LastFrameSimTime;
	FrameDeltaSeconds = FMath::Max(FrameSimTime - FrameStartSimTime, 0.0f);
	LastFrameSimTime = FrameSimTime;

	// The sample-boundary table is per frame and per interval; invalidate it here so the first chunk below
	// rebuilds it. Doing this in Execute rather than in ProcessChunk is what makes "first chunk of THIS
	// frame" well defined when a frame processes several chunks.
	bSampleBoundariesBuilt = false;

#if !UE_BUILD_SHIPPING
	// Diagnostic capture heartbeat. Called before this frame is processed, so the window is half-open:
	// the frame that crosses the deadline ends the capture and is itself excluded, which keeps the
	// written texture consistent with the last logged row.
	FTrajectoryCaptureRecorder::Get().Tick(TimeDilationSubSystem->GetCurrentSimTime());
#endif

	// reuse the array storage instead of reallocating
	HeatmapLocations.Reset();

	// Clear the location queue
	//LocationQueue.ConsumeAllLifo([](const FVector&){}); // No IsEmpty() check -> IsEmpty will traverse the whole queue and then ConsumeAllLifo will traverse it again, so we just clear it directly


	LastProcessedEntityCount = 0;

	if (!bRegisteredProperties)
	{
		RegisterProperties(ExecutionContext);
		if (!bRegisteredProperties)
		{
			return; // if properties are not registered, we cannot proceed
		}
	}

	if (!HeatmapSubsystem)
	{
		bRegisteredProperties = false;
		return;
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

	// Source of the dataset sample interval. Absent in a synthetic world, which simply disables
	// sample-boundary subdivision rather than failing registration.
	if (AgentSpawnSubsystem == nullptr)
	{
		AgentSpawnSubsystem = Context.GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>();
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
	if (!TimeDilationSubSystem) return;
	const float NewTimeStep = TimeDilationSubSystem->CurrentTimeStep;
	const bool NewPauseState = TimeDilationSubSystem->bIsPaused;

	if (CurrentTimeStep != NewTimeStep || bIsPaused != NewPauseState)
	{
		CurrentTimeStep = NewTimeStep;
		bIsPaused = NewPauseState;
		
		bLastPauseLoop = false;// Either the pause or time step has changed, so we reset the last pause loop flag
	}
}

void UAgentHeatmapProcessor::UpdateHeatmapInterval()
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(UAgentHeatmapProcessor_UpdateHeatmapInterval);
	if (!TimeDilationSubSystem) return;

	const float CurrentSimTime = TimeDilationSubSystem->GetCurrentSimTime();

	// A new dataset recycles entity IDs, so a position remembered from the previous simulation would be
	// joined to whichever agent inherited that ID and drawn as one long streak across the floor. Drop
	// them before anything else this frame. Distinct from the rewind path below, which keeps them on
	// purpose.
	if (HeatmapSubsystem && HeatmapSubsystem->ConsumeTrajectoryTrackingReset())
	{
		LastTrajectoryLocations.Reset();
		TrajectorySegments.Reset();
		LastFrameSimTime = CurrentSimTime; // per-frame delta must not span the dataset swap
	}

	if (CurrentSimTime < LastUpdatedCurrentTime)
	{
		if (HeatmapSubsystem && HeatmapSubsystem->AnyTrajectoryHeatmapsActive())
		{
			// Discard the old rendered history, but keep the latest sampled positions. LastFrameSimTime
			// is reset below, so FrameDeltaSeconds is 0 this frame and ProcessChunk emits no join
			// segment for the scrub itself; normal per-frame segments resume from the scrubbed
			// position on the very next frame.
			TrajectorySegments.Reset();
			HeatmapSubsystem->ClearTrajectoryHeatmaps();
		}
		LastUpdatedCurrentTime = CurrentSimTime;
		LastFrameSimTime = CurrentSimTime; // per-frame delta must not span the rewind
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

void UAgentHeatmapProcessor::BuildSampleBoundaries(FMassExecutionContext& Context)
{
	bSampleBoundariesBuilt = true;

	// Cleared unconditionally and first: every early return below means "fall back to one chord per frame",
	// and a table left over from the previous frame would point at the wrong sim times (and, once the
	// provider streams, at freed sample blocks).
	SampleBoundaryTimes.Reset();
	SampleBoundaryBlocks.Reset();
	for (TMap<int32, int32>& IndexMap : SampleBoundaryIndexMaps)
	{
		IndexMap.Reset();
	}

	if (!HeatmapSubsystem || !HeatmapSubsystem->AnyTrajectoryHeatmapsActive())
	{
		return;
	}

	if (FrameDeltaSeconds <= 0.0f || FrameDeltaSeconds > MaxSubdividableDeltaSeconds)
	{
		return;
	}

	SampleIntervalSeconds = AgentSpawnSubsystem ? AgentSpawnSubsystem->GetAgentTimeBetweenSteps() : 0.0f;
	if (SampleIntervalSeconds <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FSimulationFragment* SimulationFragment = Context.GetSharedFragmentPtr<FSimulationFragment>();
	const ISimSampleProvider* const Provider = SimulationFragment ? SimulationFragment->Provider.Get() : nullptr;
	if (!Provider || !Provider->IsValidAndPopulated())
	{
		return;
	}

	// Boundaries are at ABSOLUTE sim time Step * interval - the same grid PedestrianMovementProcessor's
	// RecomputeAgentTimeIndex divides by, so the position at a boundary is the sample itself (alpha == 0)
	// rather than anything this processor has to interpolate.
	const double IntervalSeconds = SampleIntervalSeconds;
	const double StartTime = FrameStartSimTime;
	const double EndTime = static_cast<double>(FrameStartSimTime) + FrameDeltaSeconds;

	// STRICTLY interior. The epsilon keeps an endpoint sitting exactly on a boundary - the common case at
	// 1x playback, where the frame step and the sample interval coincide - from producing a zero-duration
	// sub-segment. Such a segment is harmless (it deposits nothing) but it doubles the segment count.
	const double Epsilon = IntervalSeconds * 1.0e-4;
	const int32 FirstStep = FMath::Max(FMath::FloorToInt32((StartTime + Epsilon) / IntervalSeconds) + 1, 1);
	const int32 LastStep = FMath::CeilToInt32((EndTime - Epsilon) / IntervalSeconds) - 1;
	if (LastStep < FirstStep || (LastStep - FirstStep + 1) > MaxSampleBoundariesPerFrame)
	{
		return;
	}

	const int32 NumTimesteps = Provider->GetNumTimesteps();
	for (int32 Step = FirstStep; Step <= LastStep && Step < NumTimesteps; ++Step)
	{
		const TArray<FSimMovementSample>* Block = Provider->GetSamplesForTimestep(Step);
		if (!Block || Block->IsEmpty())
		{
			continue;
		}

		// A stand-in block (streaming cold miss) holds another timestep's positions. Bending the polyline
		// through one of those vertices would be worse than the chord it replaces, so skip it and let the
		// surrounding pieces span it.
		if (!Provider->HasExactSamplesForTimestep(Step))
		{
			continue;
		}

		const int32 Slot = SampleBoundaryTimes.Num();
		SampleBoundaryTimes.Add(static_cast<float>(Step * IntervalSeconds));
		SampleBoundaryBlocks.Add(Block);

		if (!SampleBoundaryIndexMaps.IsValidIndex(Slot))
		{
			SampleBoundaryIndexMaps.AddDefaulted();
		}
		TMap<int32, int32>& IndexMap = SampleBoundaryIndexMaps[Slot];
		IndexMap.Reserve(Block->Num());
		for (int32 Index = 0; Index < Block->Num(); ++Index)
		{
			IndexMap.Add((*Block)[Index].EntityID, Index);
		}
	}
}

void UAgentHeatmapProcessor::EmitTrajectorySegments(int32 EntityID, const FVector& StartLocation, const FVector& EndLocation)
{
	if (SampleBoundaryTimes.IsEmpty())
	{
		// No boundary inside this frame: the frame's own chord IS a piece of the dataset polyline.
		TrajectorySegments.Add({ StartLocation, EndLocation, FrameDeltaSeconds });
		return;
	}

	FVector PreviousLocation = StartLocation;
	float PreviousTime = FrameStartSimTime;

	for (int32 Slot = 0; Slot < SampleBoundaryTimes.Num(); ++Slot)
	{
		// Agent absent from that sample block (spawned later, already egressed): chord across it rather
		// than inventing a vertex. Degrades to the old behaviour for that one boundary, nothing else.
		const int32* SampleIndex = SampleBoundaryIndexMaps[Slot].Find(EntityID);
		if (!SampleIndex)
		{
			continue;
		}

		const float BoundaryTime = SampleBoundaryTimes[Slot];
		const float SubDeltaSeconds = BoundaryTime - PreviousTime;
		if (SubDeltaSeconds <= 0.0f)
		{
			continue;
		}

		const FVector BoundaryLocation = (*SampleBoundaryBlocks[Slot])[*SampleIndex].Position;
		TrajectorySegments.Add({ PreviousLocation, BoundaryLocation, SubDeltaSeconds });
		PreviousLocation = BoundaryLocation;
		PreviousTime = BoundaryTime;
	}

	// Tail, measured back from the frame's own end time rather than as a running remainder, so the emitted
	// durations sum to FrameDeltaSeconds by construction. A stationary agent lands here with a zero-length
	// segment carrying the whole duration, which is what AC8 depends on.
	const float TailDeltaSeconds = (FrameStartSimTime + FrameDeltaSeconds) - PreviousTime;
	if (TailDeltaSeconds > 0.0f)
	{
		TrajectorySegments.Add({ PreviousLocation, EndLocation, TailDeltaSeconds });
	}
}

void UAgentHeatmapProcessor::ProcessChunk(FMassExecutionContext& Context)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(UAgentHeatmapProcessor_ProcessChunk);
	// Get the required fragments and entities from the context
	const TConstArrayView<FEntityRenderingFragment> EntityRenderingFragment = Context.GetFragmentView<FEntityRenderingFragment>();
	const TConstArrayView<FEntityMovementFragment> EntityMovementFragment = Context.GetFragmentView<FEntityMovementFragment>();
	auto Entities = Context.GetEntities();

	// First chunk of the frame owns the rebuild. Safe because ForEachEntityChunk runs chunks sequentially;
	// switching to ParallelForEachEntityChunk would race on these tables exactly as it would on
	// LastTrajectoryLocations / TrajectorySegments below.
	if (!bSampleBoundariesBuilt)
	{
		BuildSampleBoundaries(Context);
	}

	int32 ChunkSize = HeatmapLocations.Num() == 0 ? 0 : HeatmapLocations.Num() - 1;
	
	// reserve array space to avoid reallocations as entities are added
	HeatmapLocations.Reserve((HeatmapLocations.Num() + Entities.Num()));

	
	//HeatmapLocations.SetNumUninitialized((HeatmapLocations.Num() + Entities.Num()));

	//LastProcessedEntityCount += Entities.Num();

	// ParallelFor(Entities.Num(), [&](int32 i)
	// {
	// 	TRACE_CPUPROFILER_EVENT_SCOPE(UAgentHeatmapProcessor_ParallelFor);
	// 	const auto& EntityMovement = EntityMovementFragment[i];
	// 	const auto& EntityRendering = EntityRenderingFragment[i];
	//
	// 	if (!EntityRendering.bRenderAgent && EntityRendering.bReadyToDestroy)
	// 	{
	// 		return;
	// 	}
	//
	// 	LocationQueue.ProduceItem(EntityMovement.CurrentLocation);
	// 	++LastProcessedEntityCount;
	// });
	
	
#if !UE_BUILD_SHIPPING
	// Hoisted out of the loop: one bool test and, when armed, one sim-time read per chunk.
	FTrajectoryCaptureRecorder& Capture = FTrajectoryCaptureRecorder::Get();
	const bool bCapturing = Capture.IsArmed();
	const float CaptureSimTime = (bCapturing && TimeDilationSubSystem) ? TimeDilationSubSystem->GetCurrentSimTime() : 0.0f;
#endif

	// TODO: this loop is not parallelized, implement ParallelFor for performance and a threadsafe container for HeatmapLocations for simplicity
	for (int i = 0; i < Entities.Num(); i++)
	{
		auto EntityMovement = EntityMovementFragment[i];
		auto& EntityRendering = EntityRenderingFragment[i];

		if (!EntityRendering.bRenderAgent || EntityRendering.bReadyToDestroy) // changed from && to || as likely either condition is sufficient to skip
		{
#if !UE_BUILD_SHIPPING
			// Recorded rather than silently skipped: "the processor never offered this agent" is a
			// finding in its own right when reconciling against the source movement data.
			if (bCapturing)
			{
				Capture.RecordCollect(CaptureSimTime, EntityMovement.EntityID, EntityMovement.CurrentLocation,
					EntityRendering.bRenderAgent, EntityRendering.bReadyToDestroy,
					LastTrajectoryLocations.Contains(EntityMovement.EntityID),
					/*bPositionChanged*/ false, /*bSegmentEmitted*/ false);
			}
#endif
			continue;
		}
		HeatmapLocations.Add(EntityMovement.CurrentLocation);

		bool bHasPrevious = false;
		bool bPositionChanged = false;
		bool bSegmentEmitted = false;

		if (HeatmapSubsystem && HeatmapSubsystem->AnyTrajectoryHeatmapsActive())
		{
			if (const FVector* PreviousLocation = LastTrajectoryLocations.Find(EntityMovement.EntityID))
			{
				bHasPrevious = true;
				bPositionChanged = !PreviousLocation->Equals(EntityMovement.CurrentLocation, KINDA_SMALL_NUMBER);
				// A stationary agent must still emit: the old `bPositionChanged &&` guard here is why
				// Route Exposure read zero at every queue, since the agents whose occupancy the map
				// exists to show are exactly the ones that did not move. DepositSegment handles the
				// zero-length case (Delta-t into the containing cell, zero person-metres, length booked
				// to the negligible bucket so the conservation identity still closes).
				// FrameDeltaSeconds <= 0 still skips: paused, the same sim time sampled twice, or the
				// frame a dataset reset/rewind lands on (RULING A0-5).
				//
				// The frame's interval is SUBDIVIDED at the dataset's own sample boundaries, so what gets
				// deposited is the dataset's polyline rather than one chord per rendered frame. That is
				// what makes the field playback-speed and frame-rate independent: raising playback speed
				// lengthens the frame's sim-time step, and a single chord across several samples cuts every
				// corner between them. With no boundary inside the frame this emits exactly one segment,
				// identical to the per-frame scheme it replaces.
				if (FrameDeltaSeconds > 0.0f)
				{
					EmitTrajectorySegments(EntityMovement.EntityID, *PreviousLocation, EntityMovement.CurrentLocation);
					bSegmentEmitted = true;
				}
			}
			LastTrajectoryLocations.Add(EntityMovement.EntityID, EntityMovement.CurrentLocation);
		}

#if !UE_BUILD_SHIPPING
		if (bCapturing)
		{
			Capture.RecordCollect(CaptureSimTime, EntityMovement.EntityID, EntityMovement.CurrentLocation,
				EntityRendering.bRenderAgent, EntityRendering.bReadyToDestroy,
				bHasPrevious, bPositionChanged, bSegmentEmitted);
		}
#endif
		//HeatmapLocations[(ChunkSize + i)] = EntityMovement.CurrentLocation;
	}
}

void UAgentHeatmapProcessor::ApplyHeatmapUpdates()
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(UAgentHeatmapProcessor_ApplyHeatmapUpdates);
	if (!HeatmapSubsystem) return;
	if (!HeatmapLocations.IsEmpty())
	{
		HeatmapSubsystem->BroadcastTotalAgentCount(HeatmapLocations.Num());
		
		if (bUpdateHeatmap && !bLastPauseLoop)
		{
			if (HeatmapSubsystem->AnyTrajectoryHeatmapsActive())
			{
#if !UE_BUILD_SHIPPING
				if (FTrajectoryCaptureRecorder::Get().IsArmed())
				{
					FTrajectoryCaptureRecorder::Get().RecordFlush(
						TimeDilationSubSystem ? TimeDilationSubSystem->GetCurrentSimTime() : 0.0f,
						TrajectorySegments.Num(), /*bTrajectoryActive*/ true);
				}
#endif
				HeatmapSubsystem->UpdateHeatmapsWithTrajectorySegments(TrajectorySegments);
				TrajectorySegments.Reset();
			}
			else
			{
				LastTrajectoryLocations.Reset();
				TrajectorySegments.Reset();
				HeatmapSubsystem->UpdateHeatmapsWithLocations(HeatmapLocations);
			}
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
