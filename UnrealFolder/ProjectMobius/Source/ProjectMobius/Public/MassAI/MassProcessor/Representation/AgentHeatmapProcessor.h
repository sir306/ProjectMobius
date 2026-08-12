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
#include "MassProcessor.h"
#include "MassAI/Actors/AgentRepresentationActorISM.h"
#include "Subsystems/HeatmapSubsystem.h"
#include "AgentHeatmapProcessor.generated.h"

enum class EPedestrianMovementBracket : uint8;
class UMRS_RepresentationSubsystem;
class UTimeDilationSubSystem;
class UMassEntitySpawnSubsystem;
struct FEntityInfoFragment;
struct FSimMovementSample;
/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UAgentHeatmapProcessor : public UMassProcessor
{
	GENERATED_BODY()
	
public:
	UAgentHeatmapProcessor();

protected:
	virtual void ConfigureQueries() override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	/**
	 * Assign the properties of this processor - will only be called till properties are assigned
	 *
	 * @param Context - The execution context
	 */
	void RegisterProperties(FMassExecutionContext& Context);

private:
	/** Ensure the time dilation subsystem is valid */
	bool EnsureTimeSubsystem(FMassExecutionContext& Context);

	/** Update cached time step and pause state */
	void UpdateTimeStepAndPause();

	/** Update internal state determining if heatmaps should be updated */
	void UpdateHeatmapInterval();

	/** Process a single entity chunk */
	void ProcessChunk(FMassExecutionContext& Context);

	/**
	 * Rebuilds the per-frame table of DATASET SAMPLE BOUNDARIES that fall strictly inside this frame's
	 * sim-time interval, so EmitTrajectorySegments can lay down the dataset's own polyline instead of one
	 * chord per rendered frame. Called on the first chunk of a frame only; chunks run sequentially
	 * (ForEachEntityChunk, not the parallel variant), which is what makes this shared state safe.
	 *
	 * Leaves the table EMPTY - i.e. falls back to the single per-frame chord - whenever subdividing would
	 * be wrong or unavailable: no trajectory heatmap active, no dataset provider, an unknown sample
	 * interval, an implausible frame delta (see MaxSubdividableDeltaSeconds), or absurdly many boundaries.
	 */
	void BuildSampleBoundaries(FMassExecutionContext& Context);

	/**
	 * Appends this frame's trajectory segments for one agent, split at every dataset sample boundary the
	 * frame spans. The durations sum to FrameDeltaSeconds, so each segment still carries the sim time it
	 * actually represents and the field's four-bucket conservation identity closes unchanged.
	 */
	void EmitTrajectorySegments(int32 EntityID, const FVector& StartLocation, const FVector& EndLocation);

	/** Apply the collected heatmap data */
	void ApplyHeatmapUpdates();

	/**
	 * A frame delta above this is NOT subdivided; it is emitted as one oversized segment so that
	 * FTrajectoryField's delta-t gate rejects it whole. Splitting a timeline skip into sub-cap pieces
	 * would walk every piece PAST that gate and paint the entire skipped duration onto the floor, which is
	 * precisely what the gate exists to prevent. Must stay <= FTrajectoryFieldConfig::MaxPlausibleDeltaSeconds
	 * (Source\Visualization\Public\TrajectoryField.h, 5.0 s); duplicated rather than included because
	 * ProjectMobius does not depend on the Visualization module.
	 */
	static constexpr float MaxSubdividableDeltaSeconds = 5.0f;

	/**
	 * Refuse to subdivide beyond this many boundaries in one frame. 5.0 s of a 0.1 s dataset is 49, so this
	 * is only reachable on a pathologically fine sample grid; the fallback (one chord) is what the old
	 * per-frame scheme always did, so exceeding it is a loss of accuracy, never of mass.
	 */
	static constexpr int32 MaxSampleBoundariesPerFrame = 128;

	// Entity Query
	UPROPERTY()
	FMassEntityQuery EntityQuery;

	/** Subsystem for heatmaps -- TODO: move this to its own processor */
	UPROPERTY()
	class UHeatmapSubsystem* HeatmapSubsystem;

	// bool to say if we have the registered the necessary properties of this processor
	UPROPERTY()
	bool bRegisteredProperties;

	/** Current Time step, if we store a value of a current time step we can check if changed as we don't need to update renders every tick only when data changes */
	UPROPERTY()
	int32 CurrentTimeStep = -1; // -1 is the default this ensures when checking if the time step has changed it will always be true on the first run

	/** stores the current animation pause state from the subsystem */
	UPROPERTY()
	bool bIsPaused = false;

	/** stores the last loop pause state, this way it's not updating custom data values for every entity every loop */
	UPROPERTY()
	bool bLastPauseLoop = false;

	/** Ptr to the time dilation subsystem */
	UPROPERTY()
	TObjectPtr<UTimeDilationSubSystem> TimeDilationSubSystem;

	/** bool to say when heatmaps should be updated */
	UPROPERTY()
	bool bUpdateHeatmap = true;

	// last current time value
	UPROPERTY()
	float LastUpdatedCurrentTime = 0.0f;

	/**
	 * Sim time as of the previous Execute() call. Used only to derive FrameDeltaSeconds below; reset
	 * (to the current sim time) alongside the trajectory-tracking reset and the rewind branch inside
	 * UpdateHeatmapInterval(), so the delta spanning a dataset swap or a rewind reads as zero.
	 */
	UPROPERTY()
	float LastFrameSimTime = 0.0f;

	/**
	 * Per-frame sim-time delta (RULING A0-5: per-frame, never the ~0.1s flush interval), attached to any
	 * trajectory segment emitted this frame. Never negative or NaN. Zero on the first tracked frame and
	 * on the frame a dataset reset or rewind lands on -- ProcessChunk emits no segment in that case.
	 */
	UPROPERTY()
	float FrameDeltaSeconds = 0.0f;

	/**
	 * Sim time at the START of the interval FrameDeltaSeconds spans, i.e. the previous frame's sim time.
	 * Captured before LastFrameSimTime is overwritten. Sample-boundary subdivision needs the interval, not
	 * just its width - a boundary is at an ABSOLUTE sim time (Step * sample interval), so a duration alone
	 * cannot locate it.
	 */
	UPROPERTY()
	float FrameStartSimTime = 0.0f;

	/**
	 * Dataset sample interval in sim seconds, re-read per frame from the spawn subsystem (it changes on
	 * every file load). 0 means unknown - no dataset, or a synthetic/unit-test world - and disables
	 * subdivision, which restores the exact per-frame emission this replaced.
	 */
	UPROPERTY()
	float SampleIntervalSeconds = 0.0f;

	/** Source of the dataset sample interval. Cached once; stable for the world's lifetime. */
	UPROPERTY()
	TObjectPtr<UMassEntitySpawnSubsystem> AgentSpawnSubsystem;

	/** False at the top of every Execute; the first ProcessChunk of the frame rebuilds the tables below. */
	bool bSampleBoundariesBuilt = false;

	/** Absolute sim times of the dataset sample boundaries strictly inside this frame's interval, ascending. */
	TArray<float> SampleBoundaryTimes;

	/** The provider's sample block for each boundary. Borrowed for the frame only - never cached across one. */
	TArray<const TArray<FSimMovementSample>*> SampleBoundaryBlocks;

	/**
	 * EntityID -> index into the parallel entry of SampleBoundaryBlocks. Storage is kept (never shrunk)
	 * across frames and only the contents reset, so a steady playback rate stops reallocating after the
	 * first few frames.
	 */
	TArray<TMap<int32, int32>> SampleBoundaryIndexMaps;

	/** Stores the locations of the agents so it can be sent to the heatmap subsystem */
	UPROPERTY()
	TArray<FVector> HeatmapLocations;

	/** Previous accepted position for each active agent while trajectory mode is enabled. */
	TMap<int32, FVector> LastTrajectoryLocations;

	/** Path sections emitted during the current heatmap sample interval. */
	TArray<FHeatmapTrajectorySegment> TrajectorySegments;


	/** Thread-safe queue container for heatmap locations */
	UE::TConsumeAllMpmcQueue<FVector> LocationQueue; //TODO: Need to handle garbage collection of this queue
	
	TAtomic<int32> LastProcessedEntityCount = 0;

	/** Active Number of heatmaps */
	UPROPERTY()
	int32 ActiveHeatmapCount = 0;
	
};
