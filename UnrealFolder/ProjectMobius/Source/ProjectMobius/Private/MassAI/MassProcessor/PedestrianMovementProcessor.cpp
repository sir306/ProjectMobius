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

#include "MassAI/MassProcessor/PedestrianMovementProcessor.h"
// Required headers for processing entities, Subsystems and there fragments
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassExternalSubsystemTraits.h" // This is needed so we can use subsystems and have no compile errors
// Fragments to include with this processor
#include "MassAI/Fragments/AgentEgressTenabilityFragments.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
// Shared Fragments to include with the processor
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "SimData/ISimSampleProvider.h" // A1: read samples through the provider, not SimulationData directly
// Subsystems to include with the processor
#include "Subsystems/TimeDilationSubSystem.h"
#include <MassAI/Tags/MassAITags.h>
// multithreading and async
#include "Subsystems/HeatmapSubsystem.h"
#include "MassEntityView.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Async/ParallelFor.h"
#include "HAL/CriticalSection.h"
#include "HAL/IConsoleManager.h" // TAutoConsoleVariable (Mobius.Tenability.HideFailedAgents)
#include "MassAI/SubSystems/MassEntitySpawnSubsystem.h"

class UStatisticSubsystem;

namespace
{
	/**
	 * Runtime escape hatch for the hide-at-failure behaviour. Default on (1): a failed agent's mesh
	 * stops drawing and the in-world fail marker stands in for it. Set to 0 to keep the frozen mesh
	 * visible, which is the pre-2026-07-30 behaviour and the state to fall back to while the marker
	 * render path is still being verified — otherwise a failed agent can vanish with nothing shown in
	 * its place. Read ONCE per Execute into a local, never per entity.
	 */
	TAutoConsoleVariable<int32> CVarHideFailedAgents(
		TEXT("Mobius.Tenability.HideFailedAgents"),
		1,
		TEXT("1 = hide an agent's mesh once it passes its tenability-failure time (the in-world fail\n")
		TEXT("marker stands in for it). 0 = keep the frozen mesh drawn. Render-only either way: the\n")
		TEXT("agent stays in every analysis query and in the published tenability snapshot."),
		ECVF_Default);

	/**
	 * If an agent has reached its tenability-failure time at or before the current sim time, snap it to
	 * its recorded failure pose and keep it visible (a tenability-failure marker). Centralises the snap
	 * that was previously copy-pasted across the no-data, static, and interpolation paths of Execute()
	 * so the three can never drift apart.
	 *
	 * Behaviour:
	 *   - failed  -> snap to recorded failure pose, speed 0, NotMoving bracket, bRenderAgent kept TRUE
	 *                (analysis gate), bHiddenByTenabilityFailure set from bHideFailedAgents; returns true.
	 *   - tenable -> CLEARS bHiddenByTenabilityFailure and otherwise leaves both fragments untouched;
	 *                returns false (caller owns the tenable path).
	 *
	 * Only that one clear departs from the former inline blocks, and it is deliberate: the hide is
	 * DERIVED state, not a latch. Setting it solely on the failed path would leave an agent invisible
	 * forever once the playhead had ever passed its failure time, so scrubbing back before that time
	 * would show an empty scene — the same "no append-only history" argument the marker design used to
	 * stay navigation-independent. Cheap: one bool store on a fragment already being written.
	 *
	 * bRenderAgent deliberately stays TRUE for a failed agent even when hidden. It is the analysis gate
	 * (AgentEgressHealthCalculationProcessor, AgentEgressHealthProcessor, AgentHeatmapProcessor) and it
	 * drives bReadyToDestroy in NiagaraAgentRepProcessor — clearing it would stop the tenability
	 * projection, drop the agent from the published snapshot (deleting the very fail marker meant to
	 * replace the mesh) and mark the entity for destruction. See FEntityRenderingFragment's docs.
	 *
	 * File-local + FORCEINLINE so it inlines into the per-entity ParallelFor loops (hot path).
	 * Reads the fragment's existing DeathTimeSeconds/DeathLocation/DeathRotation fields — these are the
	 * current names for the tenability-failure time/pose; a codebase-wide rename is a separate task.
	 *
	 * @param bHideFailedAgents Policy from Mobius.Tenability.HideFailedAgents, hoisted out of the loop.
	 * @return true if the agent has failed tenability at CurrentSimTime (failure pose applied), else false.
	 */
	FORCEINLINE bool ApplyTenabilityFailurePoseIfReached(
		FEntityMovementFragment& MoveFrag,
		FEntityRenderingFragment& RenderFrag,
		const FAgentEgressTenabilityFragment& Tenability,
		const float CurrentSimTime,
		const bool bHideFailedAgents)
	{
		// Identical guard to the original: a valid (>=0) failure time that the playhead has reached.
		const bool bTenabilityFailedAtCurrentTime =
			Tenability.DeathTimeSeconds >= 0.0f
			&& CurrentSimTime + UE_KINDA_SMALL_NUMBER >= Tenability.DeathTimeSeconds;

		if (!bTenabilityFailedAtCurrentTime)
		{
			// De-latch: covers scrubbing back before the failure time AND the timeline-rebuild window,
			// which resets DeathTimeSeconds to -1 on every entity.
			RenderFrag.bHiddenByTenabilityFailure = false;
			return false;
		}

		// Freeze the agent at its recorded tenability-failure pose. Still "rendered" as far as every
		// analysis query is concerned; only the Niagara W term honours the hide flag.
		MoveFrag.CurrentLocation = Tenability.DeathLocation;
		MoveFrag.CurrentRotation = Tenability.DeathRotation;
		MoveFrag.CurrentSpeed = 0.0f;
		MoveFrag.CurrentMovementBracket = EPedestrianMovementBracket::Emb_NotMoving;
		RenderFrag.bRenderAgent = true;
		RenderFrag.bReadyToDestroy = false;
		RenderFrag.bAnimationChanged = true;
		RenderFrag.bHiddenByTenabilityFailure = bHideFailedAgents;
		return true;
	}
}

UPedestrianMovementProcessor::UPedestrianMovementProcessor():
	TimeDilationSubSystem(nullptr)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Avoidance);
}

void UPedestrianMovementProcessor::ConfigureQueries()
{
	// The required fragments for this processor
	EntityQuery.AddSharedRequirement<FSimulationFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::All);

	// The Entity Query Required fragments for this processor
	EntityQuery.AddRequirement<FEntityMovementFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FEntityRenderingFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FAgentEgressTenabilityFragment>(EMassFragmentAccess::ReadOnly);
	// Required Query Tags
	EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::Optional);

	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);

	// Register requirements for the processor
	ProcessorRequirements.AddSubsystemRequirement<UTimeDilationSubSystem>(EMassFragmentAccess::ReadOnly);

	//
}
// TODO: This processor needs breaking up into smaller processors as it is doing too much at once
// one should read new data and apply it and the other should modify the data
void UPedestrianMovementProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	// Check if the subsystems are setup
	if(!bAreSubSystemsSetup)
	{
		SetupSubSystems(ExecutionContext);

		// to stop the function from executing if the subsystem is not valid we return here to prevent crashes
		if(TimeDilationSubSystem == nullptr)
		{
			return;
		}
	}
	else // if the subsystems are setup then we update the current time step
	{
		UpdateCurrentTimeStepAndStepPercentage();
	}

	// Hoisted out of the per-entity ParallelFor loops below: one CVar read per Execute, not per agent.
	const bool bHideFailedAgents = CVarHideFailedAgents.GetValueOnAnyThread() != 0;

	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this, bHideFailedAgents](FMassExecutionContext& Context) {
		{
			//TODO: Move data to subsystem and get it from there so not constantly getting large data sets
	
			// Get the shared fragment
			//auto SharedAgentMovement = Context.GetSharedFragment<FSimulationFragment>(); // this is killing the fps
			
			// Check if there is data to process and stop if there is none
			if(!IsThereDataToProcess(Context))
			{
				// Entity Rendering Fragment
				const TArrayView<FEntityRenderingFragment> EntityRenderingFragment = Context.GetMutableFragmentView<FEntityRenderingFragment>();
				const TArrayView<FEntityMovementFragment> EntityMovementFragment = Context.GetMutableFragmentView<FEntityMovementFragment>();
				const TConstArrayView<FAgentEgressTenabilityFragment> AgentTenabilityFragments =
					Context.GetFragmentView<FAgentEgressTenabilityFragment>();
				
				ParallelFor(EntityRenderingFragment.Num(), [&](int32 i)
				{
					// No sample data this step: agents that have failed tenability still freeze at their
					// failure pose, everyone else is hidden. Matches the former inline failed/else block.
					if (!ApplyTenabilityFailurePoseIfReached(EntityMovementFragment[i], EntityRenderingFragment[i], AgentTenabilityFragments[i], CurrentSimTime, bHideFailedAgents))
					{
						EntityRenderingFragment[i].bRenderAgent = false;
					}
				});
				
				return;
			}
			// Retrieve the simulation fragment once
			const auto& SimulationFragment = Context.GetSharedFragment<FSimulationFragment>();

			// A1: read samples through the provider (windowed accessor). Returns bitwise what
			// SimulationData->Find() did, including nullptr for an absent timestep — which the B2 cache and
			// the bSamplesTheSame check below both rely on. IsThereDataToProcess already proved Provider valid.
			ISimSampleProvider* const Provider = SimulationFragment.Provider.Get(); // non-const: NotifyPlayhead below is non-const
			const TArray<FSimMovementSample>* CurrentSamplesPtr = Provider ? Provider->GetSamplesForTimestep(CurrentTimeStep) : nullptr;
			const TArray<FSimMovementSample>* NextSamplesPtr     = Provider ? Provider->GetSamplesForTimestep(CurrentTimeStep + 1) : nullptr;

			if (!CurrentSamplesPtr)
			{
				// Log an error or handle the case where the current samples are not found
				if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
				{
					Feedback->ReportError(
						FText::FromString("Simulation Data Error"),
						FText::FromString("Missing movement samples"),
						FText::FromString(FString::Printf(TEXT("Movement samples not found for time step %d."), CurrentTimeStep)),
						FText::FromString("PedestrianMovementProcessor"));
				}
				UE_LOG(LogTemp, Error, TEXT("Current movement samples not found for time step %d"), CurrentTimeStep);
				return;
			}
			
			const TArray<FSimMovementSample>& CurrentMovementSamples = *CurrentSamplesPtr;
			const bool bSamplesTheSame = (NextSamplesPtr == nullptr);

			// Avoid repeated lookups and copy only if needed
			const TArray<FSimMovementSample>& NextMovementSamples = bSamplesTheSame ? CurrentMovementSamples : *NextSamplesPtr;

			// Stand-in window detection: if either served block is not the exact block for its
			// timestep (streaming cold miss), poses this frame are cosmetic — flag every agent so
			// analysis (tenability) holds instead of sampling wrong-timestep locations. Side-effect
			// free query (no RequestLoad); the GetSamplesForTimestep calls above already kicked any load.
			const bool bCurrentExact = !Provider || Provider->HasExactSamplesForTimestep(CurrentTimeStep);
			const bool bNextExact = bSamplesTheSame || !Provider || Provider->HasExactSamplesForTimestep(CurrentTimeStep + 1);
			const bool bApproximateWindow = !(bCurrentExact && bNextExact);

			// B2: the maps are keyed chunk-independently (by EntityID), so rebuilding them every chunk every
			// frame is pure redundancy (O(NumChunks * N) game-thread insertions). Rebuild only when the data
			// window changes — composite (DataGeneration, timestep, served-block identities) key, NOT
			// timestep alone (see ShouldRebuildSampleIndexMaps for the file-switch OOB hazard and the A4
			// streaming served-content swap it also guards). The per-frame CurrentSamplesPtr / NextSamplesPtr
			// fetch above is left UNCHANGED so the array bounds always match the cached map.
			// SAFE because ForEachEntityChunk runs sequentially (chunks 2..N in a frame hit the cache);
			// ParallelForEachEntityChunk would reintroduce a race on these shared maps.
			const uint32 CurGen = SimulationFragment.DataGeneration;
			if (ShouldRebuildSampleIndexMaps(CachedDataGeneration, CachedMapsTimeStep, CachedCurrentBlockPtr, CachedNextBlockPtr,
			                                 CurGen, CurrentTimeStep, CurrentSamplesPtr, NextSamplesPtr))
			{
				// A1: tell the provider the playhead moved so a future streaming provider can prefetch. Fired
				// once per window change (inside the rebuild guard, on the first chunk), not per chunk. Dir is
				// +1 forward / -1 rewind / 0 unchanged; the resident provider ignores it. PrevTimeStep tracks
				// the last window we notified for.
				if (Provider)
				{
					const int32 Dir = (CurrentTimeStep == PrevTimeStep) ? 0 : (CurrentTimeStep > PrevTimeStep ? 1 : -1);
					Provider->NotifyPlayhead(CurrentTimeStep, Dir);
					PrevTimeStep = CurrentTimeStep;
				}

				// Reset and pre-reserve with a known max size if available
				EntityIDToCurrentMovementSampleIndexMap.Reset();
				EntityIDToCurrentMovementSampleIndexMap.Reserve(CurrentMovementSamples.Num()); // If Reset doesn't clear capacity

				for (int32 Index = 0; Index < CurrentMovementSamples.Num(); ++Index)
				{
					EntityIDToCurrentMovementSampleIndexMap.Add(CurrentMovementSamples[Index].EntityID, Index);
				}

				if (!bSamplesTheSame)
				{
					EntityIDToNextMovementSampleIndexMap.Reset();
					EntityIDToNextMovementSampleIndexMap.Reserve(NextMovementSamples.Num());

					for (int32 Index = 0; Index < NextMovementSamples.Num(); ++Index)
					{
						EntityIDToNextMovementSampleIndexMap.Add(NextMovementSamples[Index].EntityID, Index);
					}
				}

				CachedDataGeneration = CurGen;
				CachedMapsTimeStep   = CurrentTimeStep;
				CachedCurrentBlockPtr = CurrentSamplesPtr;
				CachedNextBlockPtr    = NextSamplesPtr;
			}


			// Get the required fragments
			const TArrayView<FEntityMovementFragment> EntityMovementFragment = Context.GetMutableFragmentView<FEntityMovementFragment>();
			const TArrayView<FEntityRenderingFragment> EntityRenderingFragment = Context.GetMutableFragmentView<FEntityRenderingFragment>();
			const TConstArrayView<FAgentEgressTenabilityFragment> AgentTenabilityFragments =
				Context.GetFragmentView<FAgentEgressTenabilityFragment>();

			auto Entities = Context.GetEntities();
			
			if (bSamplesTheSame)
			{
				// [2a] Parallel update for static samples (no interpolation required)
				// All agents use only the current timestep sample data
				ParallelFor(Entities.Num(), [&](int32 i)
				{
					// Get mutable fragments for the entity
					FEntityMovementFragment& MoveFrag = EntityMovementFragment[i];
					FEntityRenderingFragment& RenderFrag = EntityRenderingFragment[i];

					// Stamp BEFORE the failure-pose early-out: a frozen agent's stand-in status must
					// still be current (the health processor reads it independently of movement).
					MoveFrag.bSampleApproximate = bApproximateWindow;

					const FAgentEgressTenabilityFragment& Tenability = AgentTenabilityFragments[i];

					// Agents that have failed tenability freeze at their failure pose and skip the lookup.
					if (ApplyTenabilityFailurePoseIfReached(MoveFrag, RenderFrag, Tenability, CurrentSimTime, bHideFailedAgents))
					{
						return;
					}

					// Default: do not render the agent unless confirmed valid
					RenderFrag.bRenderAgent = false;

					// Lookup entity movement sample by ID
					const int32 EntityID = MoveFrag.EntityID;
					if (int32* SampleIndex = EntityIDToCurrentMovementSampleIndexMap.Find(EntityID))
					{
						// Sample found — apply static sample data
						AssignFromSample(MoveFrag, RenderFrag, CurrentMovementSamples[*SampleIndex], true);
					}
					else
					{
						// Sample missing — mark for destruction
						RenderFrag.bReadyToDestroy = true;
					}
					
				});
			}
			else
			{
				// [2b] Parallel update with interpolation (between current and next timestep)
				// Each agent uses both current and next timestep data to interpolate
				ParallelFor(Entities.Num(), [&](int32 i)
				{
					// Get mutable fragments for the entity
					FEntityMovementFragment& MoveFrag = EntityMovementFragment[i];
					FEntityRenderingFragment& RenderFrag = EntityRenderingFragment[i];

					// Stamp BEFORE the failure-pose early-out: a frozen agent's stand-in status must
					// still be current (the health processor reads it independently of movement).
					MoveFrag.bSampleApproximate = bApproximateWindow;

					const FAgentEgressTenabilityFragment& Tenability = AgentTenabilityFragments[i];

					// Agents that have failed tenability freeze at their failure pose and skip the lookup.
					if (ApplyTenabilityFailurePoseIfReached(MoveFrag, RenderFrag, Tenability, CurrentSimTime, bHideFailedAgents))
					{
						return;
					}

					// Default: do not render the agent unless confirmed valid
					RenderFrag.bRenderAgent = false;

					// Lookup movement sample indices by entity ID
					const int32 EntityID = MoveFrag.EntityID;
					const int32* CurIndex = EntityIDToCurrentMovementSampleIndexMap.Find(EntityID);
					const int32* NextIndex = EntityIDToNextMovementSampleIndexMap.Find(EntityID);

					if (CurIndex)
					{
						if (NextIndex)
						{
							// Both samples exist — interpolate between current and next
							const FSimMovementSample& CurSample = CurrentMovementSamples[*CurIndex];
							const FSimMovementSample& NextSample = NextMovementSamples[*NextIndex];
							InterpolateAndAssign(MoveFrag, RenderFrag, CurSample, NextSample, TimeStepPercentage);
						}
						else
						{
							// Next sample missing — fallback to static assignment
							AssignFromSample(MoveFrag, RenderFrag, CurrentMovementSamples[*CurIndex], true);
						}
					}
					else
					{
						// Current sample missing — mark for destruction
						RenderFrag.bReadyToDestroy = true;
					}
				});
			}
		}
	}));
	//TODO: ****** when destroying respawning back this should only be called when needed not every execute ******
	//ExecutionContext.FlushDeferred();
}

void UPedestrianMovementProcessor::RenderEntityInfoFragment(FEntityRenderingFragment& EntityRenderFragToUpdate, bool bNewRenderStatus)
{
	EntityRenderFragToUpdate.bRenderAgent = bNewRenderStatus;
}

void UPedestrianMovementProcessor::UpdateEntityInfoFragment(FEntityMovementFragment& EntityMovementFragToUpdate, FEntityRenderingFragment& EntityRenderFragToUpdate, const FVector& NewLocation, const FRotator& NewRotation, const bool bNewRenderStatus)
{
	EntityMovementFragToUpdate.CurrentLocation = NewLocation; // this value needs to be come a variable for scaling and converting feet to cm, meters, etc.
	EntityMovementFragToUpdate.CurrentRotation = NewRotation;
	RenderEntityInfoFragment(EntityRenderFragToUpdate, bNewRenderStatus);
}

bool UPedestrianMovementProcessor::DoesMovementAndEntityIDMatch(const int32 MovementDataID, const int32 EntityInfoID)
{
	if(MovementDataID == EntityInfoID)
	{
		return true;
	}
	else
	{
		return false;
	}
}

void UPedestrianMovementProcessor::GetEntityLocationAndRotation(const FEntityInfoFragment& Entity,
                                                                FVector& OutStartLocation, FVector& OutEndLocation, FRotator& OutStartRotation, FRotator& OutEndRotation) const
{
	
}

TPair<FVector, FRotator> UPedestrianMovementProcessor::LinearInterpolate(const FVector& StartLocation,
                                                                         const FVector& EndLocation, const FRotator& StartRotation, const FRotator& EndRotation) const
{
	// Get the new location
	FVector NewLocation = FMath::Lerp(StartLocation, EndLocation, TimeStepPercentage);
	// Get the new rotation
	FRotator NewRotation = FMath::Lerp(StartRotation, EndRotation, TimeStepPercentage);
	// Return the new location and rotation
	return TPair<FVector, FRotator>(NewLocation, NewRotation);
}

void UPedestrianMovementProcessor::AssignFromSample(FEntityMovementFragment& MoveFrag,
                                                    FEntityRenderingFragment& RenderFrag, const FSimMovementSample& Sample, bool bEnableRender)
{
	MoveFrag.CurrentLocation = Sample.Position;
	MoveFrag.CurrentRotation = Sample.Rotation;
	MoveFrag.CurrentSpeed = Sample.Speed;

	RenderFrag.bRenderAgent = bEnableRender;
	RenderFrag.bReadyToDestroy = !bEnableRender;
	RenderFrag.bAnimationChanged = static_cast<EPedestrianMovementBracket>(MoveFrag.CurrentMovementBracket) != static_cast<EPedestrianMovementBracket>(Sample.MovementBracket);

	MoveFrag.CurrentMovementBracket = Sample.MovementBracket;
	
	// Quick Fix for flow counters - when we set this fragment we need to sim time stamp it so we can use it for flow counters
	MoveFrag.LastUpdatedSimTime = CurrentSimTime;
}

void UPedestrianMovementProcessor::InterpolateAndAssign(FEntityMovementFragment& MoveFrag,
                                                        FEntityRenderingFragment& RenderFrag, const FSimMovementSample& Current, const FSimMovementSample& Next,
                                                        float LerpAlpha)
{
	const TPair<FVector, FRotator> Interp = LinearInterpolate(Current.Position, Next.Position, Current.Rotation, Next.Rotation);
	UpdateEntityInfoFragment(MoveFrag, RenderFrag, Interp.Key, Interp.Value, true);

	RenderFrag.bReadyToDestroy = false;
	MoveFrag.CurrentSpeed = FMath::Lerp(Current.Speed, Next.Speed, LerpAlpha);

	RenderFrag.bAnimationChanged = static_cast<EPedestrianMovementBracket>(MoveFrag.CurrentMovementBracket) != static_cast<EPedestrianMovementBracket>(Next.MovementBracket);
	MoveFrag.CurrentMovementBracket = Current.MovementBracket;

	// Quick Fix for flow counters - when we set this fragment we need to sim time stamp it so we can use it for flow counters
	MoveFrag.LastUpdatedSimTime = CurrentSimTime;
}

bool UPedestrianMovementProcessor::IsThereDataToProcess(const FMassExecutionContext& ExecutionContext) const
{
	// TODO: This one should be at the start to only check once per call not per loop iteration per call
	// A1: query the provider instead of SimulationData directly. IsValidAndPopulated() mirrors the old
	// "valid && !IsEmpty()", GetNumTimesteps() mirrors SimData->Num(), GetSamplesForTimestep() mirrors Find()
	// (incl. nullptr for an absent timestep) — so this is logically identical to the previous check.
	const ISimSampleProvider* const Provider =
		ExecutionContext.GetSharedFragment<FSimulationFragment>().Provider.Get();
	if (!Provider || !Provider->IsValidAndPopulated())
	{
		return false;
	}

	if (CurrentTimeStep >= Provider->GetNumTimesteps())
	{
		return false;
	}

	const TArray<FSimMovementSample>* StepSamples = Provider->GetSamplesForTimestep(CurrentTimeStep);
	if (!StepSamples || StepSamples->IsEmpty())
	{
		return false;
	}
	if(ExecutionContext.GetNumEntities() == 0)
	{
		return false;
	}
	return true;
}

void UPedestrianMovementProcessor::SetupSubSystems(FMassExecutionContext& ExecutionContext)
{
	// The TimeDilationSubSystem should not be allocated if the world is not valid
	if(!ExecutionContext.GetWorld())
	{
		return;
	}

	// Check if the TimeDilation subsystem is nullptr
	if(TimeDilationSubSystem == nullptr)
	{
		// Get the TimeDilationSubSystem
		TimeDilationSubSystem = ExecutionContext.GetWorld()->GetSubsystem<UTimeDilationSubSystem>();
	}

	// Cache the spawn subsystem (stable for the world's lifetime); source of the agent sample interval.
	if(AgentSpawnSubsystem == nullptr)
	{
		AgentSpawnSubsystem = ExecutionContext.GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>();
	}

	// check that the TimeDilationSubSystem was allocated and update the current time step value
	if(TimeDilationSubSystem != nullptr)
	{
		CurrentSimTime = TimeDilationSubSystem->GetCurrentSimTime();
		RecomputeAgentTimeIndex();

		// Update flag to true so we don't check again
		bAreSubSystemsSetup = true;
	}
}

void UPedestrianMovementProcessor::UpdateCurrentTimeStepAndStepPercentage()
{
	if(TimeDilationSubSystem != nullptr)
	{
		CurrentSimTime = TimeDilationSubSystem->GetCurrentSimTime();
		RecomputeAgentTimeIndex();
	}
	else
	{
		// if the subsystem is nullptr then set the flag to false, this way it will try to get the subsystem again
		bAreSubSystemsSetup = false;
	}
}

void UPedestrianMovementProcessor::RecomputeAgentTimeIndex()
{
	// The agent sample map is keyed on the agent's own grid. Index it from absolute seconds so the
	// lookup is correct regardless of which source owns the shared clock interval (B-Risk may own it).
	const float AgentInterval = AgentSpawnSubsystem ? AgentSpawnSubsystem->GetAgentTimeBetweenSteps() : 0.0f;

	if (AgentInterval > UE_KINDA_SMALL_NUMBER)
	{
		const float StepFloat = CurrentSimTime / AgentInterval;
		const int32 Step = FMath::Max(0, FMath::FloorToInt32(StepFloat));
		SetCurrentTimeStep(Step);
		// Fractional part in [0,1) — interpolation alpha between this sample and the next.
		TimeStepPercentage = StepFloat - static_cast<float>(FMath::FloorToInt32(StepFloat));
	}
	else
	{
		// Fallback: agent interval unavailable -> use the shared clock grid (legacy behaviour). This
		// is identity with the above when the agent owns the clock (clock interval == agent interval).
		SetCurrentTimeStep(TimeDilationSubSystem->GetCurrentTimeStep());
		TimeStepPercentage = TimeDilationSubSystem->GetCurrentTimeStepPercentage();
	}
}
