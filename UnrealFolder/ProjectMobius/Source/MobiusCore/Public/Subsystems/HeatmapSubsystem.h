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
#include "HeatmapLegend.h"              // FHeatmapLegendContents is returned by value from a UFUNCTION
#include "MassEntitySubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "HeatmapSubsystem.generated.h"

class AHeatmapPixelTextureVisualizer;
class UHeatmapSubsystem;
class AHeatmapVisualizer;

/** A travelled section of an agent path, expressed in world-space centimetres. */
struct FHeatmapTrajectorySegment
{
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	/** Simulation seconds this segment represents. One frame's sim-time delta, not the flush interval. */
	float DeltaSeconds = 0.0f;
};

/** Length (cm) and sim-seconds of trajectory segments the floor filter has dropped for one heatmap (D7). */
struct FHeatmapTrajectoryDroppedMass
{
	double DroppedLengthCm = 0.0;
	double DroppedSeconds = 0.0;
};

// Delegates used to broadcast events
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHeatmapAdded, AHeatmapPixelTextureVisualizer*, HeatmapActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHeatmapRemoved, AHeatmapPixelTextureVisualizer*, HeatmapActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FUpdateFloorStatCount, int32, FloorNumber, int32, AgentCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FUpdateBetweenFloorStatCount, int32, FloorNumber, int32, AgentCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNewSpawnHeights, const TArray<float>&, NewHeightSpawnLocations);


/*
 * The TMassExternalSubsystemTraits is required for this subsystem so it can be used with mass entity
 * i.e. the representation processor that calls on this subsystem
 */
template<>
struct TMassExternalSubsystemTraits<UHeatmapSubsystem> final
{
	enum
	{
		ThreadSafeRead = true,
		ThreadSafeWrite = false,
		GameThreadOnly = true, // needs to be game thread as we calling rendering api
	};
};
/**
 *
 */
UCLASS()
class MOBIUSCORE_API UHeatmapSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

#pragma region METHODS
public:
	/** Constructor */
	UHeatmapSubsystem();

	/** Initializer */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** De-Initializer */
	virtual void Deinitialize() override;


	/**
	 * Updates the XY spawn location of the heatmap spawn point
	 *
	 * @param[FVector] SpawnOrigin - The origin of the heatmap spawn point
	 * @param[FVector] BoundExtents - The extents of the bounding box
	 * Currently called by the mesh-building flow after bounds are known, via the OnMeshBuilt delegate.
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Subsystem|Update")
	void UpdateSpawnLocationAndHeatmapSize(const FVector& SpawnOrigin, const FVector& BoundExtents);

	/**
	 * Update the spawn height array with new array of spawn heights
	 *
	 * @TArray<float> NewHeightSpawnLocations - The new array of spawn heights
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Subsystem|Update")
	void UpdateSpawnHeightLocations(const TArray<float>& NewHeightSpawnLocations);

	/**
	 * Create a Heatmap at the given location, and the index will be used for naming the heatmap
	 *
	 * @param[FVector] Location - The location to create the heatmap
	 * @param[int32] HeatmapIndex - The index of the heatmap, used for naming convention
	 * TODO: we will in future want to pass more parameters to this function like type of heatmap, size, etc
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Subsystem|Create")
	void CreateHeatmap(const FVector& Location, int32 HeatmapIndex);

	/**
	 * Naming convention method for naming the heatmap actor
	 *
	 */

	/**
	 * As we now will be dynamically creating the heatmap actors, we need to make sure that the name is unique and
	 * the array position is correct as this is used for the naming convention
	 */

	/**
	 * Adds a heatmap actor to this subsystem
	 *
	 * @param HeatmapActor The heatmap actor to add
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Subsystem")
	void AddHeatmapActor(AHeatmapPixelTextureVisualizer* HeatmapActor);
	void RemoveHeatmapActor(AHeatmapPixelTextureVisualizer* HeatmapActor);

	void UpdateHeatmaps(const FVector& AgentLocation);

	void UpdateHeatmapsWithLocations_Mpmc(UE::TConsumeAllMpmcQueue<FVector>& LocationQueue);

	void UpdateHeatmapsWithLocations(const TArray<FVector>& LocationArray);

	/** Immediately redraws one heatmap from the most recently processed agent locations. */
	void RefreshHeatmapFromLatestLocations(AHeatmapPixelTextureVisualizer* Heatmap);

	/** Draw path segments accumulated only while the current simulation session is played. */
	void UpdateHeatmapsWithTrajectorySegments(const TArray<FHeatmapTrajectorySegment>& Segments);

	/** True when at least one visible heatmap is in trajectory-path mode. */
	bool AnyTrajectoryHeatmapsActive() const;

	/** Clears the trajectory render targets after a rewind, seek, or new playback session. */
	void ClearTrajectoryHeatmaps();

	/**
	 * Length (cm) and sim-seconds of trajectory segments dropped by the floor filter in
	 * UpdateHeatmapsWithTrajectorySegments (D7) for one heatmap since its last ClearTrajectoryHeatmaps().
	 * For conservation checks: deposited + dropped should equal offered.
	 */
	void GetDroppedTrajectoryMass(const AHeatmapPixelTextureVisualizer* Heatmap, double& OutDroppedLengthCm, double& OutDroppedSeconds) const;

	/**
	 * Ask the heatmap processor to forget every agent's last sampled trajectory position.
	 *
	 * The processor deliberately KEEPS those positions across a rewind or seek, so the scrubbed-to state
	 * is joined to where the agent was and the jump renders as a visible transition rather than a hole.
	 * That is wrong when the agent set itself is replaced — loading a different dataset reuses entity
	 * IDs, so each recycled ID would be joined to a position belonging to the previous simulation and
	 * draw one long straight streak across the floor on the first flush.
	 *
	 * Call this whenever the entities are rebuilt rather than merely re-timed. The processor consumes
	 * the request on its next update; it is a request rather than a direct reset because the map lives
	 * on the MASS processor, which callers on the game thread have no handle to.
	 */
	void RequestTrajectoryTrackingReset();

	/** Processor-side half of RequestTrajectoryTrackingReset. Returns true once per request. */
	bool ConsumeTrajectoryTrackingReset();

	/** Enables or disables trajectory-path mode for every registered heatmap actor. */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Subsystem|Update")
	void SetTrajectoryHeatmapsEnabled(bool bEnabled);

	/**
	 * Selects Route Exposure (person-seconds) over Route Usage (person-metres) for every registered
	 * heatmap actor, and REMEMBERS the selection so an actor registering later inherits it.
	 *
	 * The "stored and applied when the trajectory view is next enabled" claim this comment used to make
	 * was FALSE until 2026-08-11 — nothing stored it and AddHeatmapActor applied no trajectory state at
	 * all, so the mode was silently lost for any heatmap spawned after the toggle. That sentence is what
	 * made the dead path look legitimate, which is why it is called out rather than quietly corrected.
	 *
	 * Display-only: both canonical accumulators are maintained unconditionally, so this re-encodes and
	 * never re-walks or discards a segment. Safe to call mid-playback.
	 *
	 * Takes a bool rather than ETrajectoryMapMode deliberately — the checkbox that drives this should
	 * not have to name the enum, and a bool pin needs no literal, which keeps the widget graph
	 * scriptable (the MCP bridge cannot write enum pin defaults).
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Subsystem|Update")
	void SetTrajectoryRouteExposureEnabled(bool bEnabled);

	/**
	 * The printed colour-band key for the surface the registered heatmaps are currently showing.
	 *
	 * This is the ONE call WBP_HeatmapColourBands needs. Get the world subsystem, call this, push the
	 * six rows into the text blocks. Re-call it after anything that changes the active surface - the two
	 * setters above, and SetTrajectoryMapMode on an actor.
	 *
	 * Reads the FIRST registered heatmap, because the two setters above apply to every actor and remember
	 * the selection for actors registering later, so the set cannot legitimately disagree with itself. If
	 * per-heatmap keys are ever wanted, call AHeatmapPixelTextureVisualizer::GetLegendContents directly on
	 * the actor rather than adding an index here - the actor is the thing that knows.
	 *
	 * With NO heatmap registered the title and unit header still describe the remembered selection, but
	 * bHasData is false: the mode is known and the edges are not. That is deliberately not the same as
	 * returning a default-constructed struct, which would blank the title and read as a broken widget.
	 *
	 * ⚠️ Check bHasData before printing the rows. See FHeatmapLegendContents.
	 */
	UFUNCTION(BlueprintPure, Category = "Heatmap|Legend")
	FHeatmapLegendContents GetActiveHeatmapLegend() const;

	void UpdateHeatmapTextureRender();

	void ClearEmptyHeatmaps();

	/**
	 * Save Selected Heatmaps to PNG
	 *
	 * @param[AHeatmapPixelTextureVisualizer] HeatmapActorArray The array of heatmap actors to save
	 */
	UFUNCTION(BlueprintCallable, Category = "Heatmap|Subsystem|Update")
	void SaveSelectedHeatmapsToPNG(const TArray<AHeatmapPixelTextureVisualizer*>& HeatmapActorArray);
	/**
	 * Save Selected Heatmaps to PNG
	 * @param HeatmapActorArray The array of heatmap actors to save
	 * @param CurrentTimeString To Avoid creating files with the same name and provide more context to the user as to when the data was saved in regards to the simulation
	 */
	void SaveSelectedHeatmapsToPNG(const TArray<AHeatmapPixelTextureVisualizer*>& HeatmapActorArray, const FString& CurrentTimeString);


	//TODO: this method needs to be placed in a more appropriate place as it is not really a heatmap method but we have bound the visualiser chart logic to this system
	/** Broadcast the total agent count, for total stat count */
	void BroadcastTotalAgentCount(int32 NewTotalAgentCount)
	{
		if (TotalAgentCount != NewTotalAgentCount)
		{
			TotalAgentCount = NewTotalAgentCount;
			OnUpdateFloorStatCount.Broadcast(-1, TotalAgentCount);
		}
	}

protected:

private:
	// these methods are responsible for debouncing the heatmap generation and preventing collisions on updating data
	/** Schedule Heatmap Generation */
	void ScheduleHeatmapGeneration();

	/** Process Heatmap Generation */
	void ProcessHeatmapGeneration();

	void ComputeValidHeatmapLocations_Mpmc(
		UE::TConsumeAllMpmcQueue<FVector>& LocationQueue,
		TArray<TArray<FVector>>& OutValidLocations,
		TArray<TArray<FVector>>& OutBetweenLocations,
		TArray<FVector>& DequeuedData) const;

	/**
	 * Build arrays of valid agent locations for each heatmap and locations
	 * between floors.
	 */
	void ComputeValidHeatmapLocations(const TArray<FVector>& LocationArray,
	                                  TArray<TArray<FVector>>& OutValidLocations,
	                                  TArray<TArray<FVector>>& OutBetweenLocations) const;

	/** Broadcast agent counts for each floor and between floors */
	void BroadcastAgentCounts(const TArray<TArray<FVector>>& ValidLocations,
	                          const TArray<TArray<FVector>>& BetweenLocations);

	void RunAsyncHeatmapUpdate_Mpmc(
	const TArray<TArray<FVector>>& ValidLocations,
	const TArray<FVector>& FallbackLocations);

	/** Run the asynchronous heatmap update using the provided locations */
	void RunAsyncHeatmapUpdate(const TArray<FVector>& LocationArray,
	                           const TArray<TArray<FVector>>& ValidLocations);

#pragma endregion METHODS

#pragma region PROPERTIES
public:
	/** Event to broadcast when a heatmap is added */
	UPROPERTY(BlueprintAssignable, Category = "Heatmap|Subsystem")
	FHeatmapAdded OnHeatmapAdded;

	/** Event to broadcast when a heatmap is removed */
	UPROPERTY(BlueprintAssignable, Category = "Heatmap|Subsystem")
	FHeatmapRemoved OnHeatmapRemoved;

	UPROPERTY(BlueprintAssignable, Category = "Heatmap|Subsystem")
	FUpdateFloorStatCount OnUpdateFloorStatCount;

	UPROPERTY(BlueprintAssignable, Category = "Heatmap|Subsystem")
	FUpdateBetweenFloorStatCount OnUpdateBetweenFloorStatCount;

	UPROPERTY(BlueprintAssignable, Category = "Heatmap|Subsystem")
	FOnNewSpawnHeights OnNewSpawnHeights;

protected:
	/** Stores all the heatmaps of the world */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Heatmap|Subsystem")
	//TArray<class AHeatmapVisualizer*> Heatmaps;
	TArray<AHeatmapPixelTextureVisualizer*> Heatmaps;

	/** Most recent simulation locations, retained only to refresh a live mode after a visual mode change. */
	TArray<FVector> LastAgentLocations;


private:
	/**
	 * The trajectory view state the subsystem believes is selected, so an actor registering LATER inherits
	 * it (AddHeatmapActor applies both). Before 2026-08-11 neither was stored: the setters only forwarded
	 * to actors already in Heatmaps, so a heatmap spawned after the user made a selection — which is the
	 * normal order, since heatmaps are created per floor when a dataset loads — came up in the default
	 * Route Usage with trajectory off, while the checkboxes still showed the user's choice.
	 *
	 * These mirror UI intent, NOT actor truth. AHeatmapPixelTextureVisualizer remains the authority on its
	 * own state and both of its setters early-out on no-change, so re-applying here is idempotent and
	 * cannot re-trigger the ClearTexture() that entering trajectory mode performs.
	 */
	bool bTrajectoryHeatmapsEnabled = false;
	bool bRouteExposureEnabled = false;

	/** Set by RequestTrajectoryTrackingReset, cleared by ConsumeTrajectoryTrackingReset. Game thread only. */
	bool bTrajectoryTrackingResetPending = false;

	/**
	 * Trajectory mass dropped by the floor filter per heatmap (D7); the entry for a heatmap is zeroed
	 * when ClearTrajectoryHeatmaps() clears it. Raw pointer keys match the existing Heatmaps array.
	 */
	TMap<const AHeatmapPixelTextureVisualizer*, FHeatmapTrajectoryDroppedMass> TrajectoryDroppedMassByHeatmap;

	/** The XY spawn point for the heatmaps */
	UPROPERTY()
	FVector2D XYSpawnLocation;

	/** 2D Bounding Size of the heatmap  */
	UPROPERTY()
	FVector2D HeatmapBoundingSize = FVector2D(0, 0);

	/** Array of Height Spawn Locations, this stores all the height spawn locations of the heatmaps, this is done as
	 * mesh generation can at times take longer than the loading of the pedestrian data */
	UPROPERTY()
	TArray<float> HeightSpawnLocations = TArray<float>();

	// these properties are responsible for debouncing the heatmap generation and preventing collisions on updating data
	/** Protect the data updates with a Critical Section */
	FCriticalSection HeightSpawnDataLock;

	/** Timer handle used for debouncing */
	UPROPERTY()
	FTimerHandle HeatmapGenerationTimerHandle;

	/**
	 * Array storing the count of agents on the last processed floor for each heatmap.
	 * Used to track and compare the number of agents between floors during heatmap updates.
	 */
	UPROPERTY()
	TArray<int32> LastFloorCounts;

	/**
	 * Array that stores the counts of agents located between floors for the most recent calculation.
	 * This data is used to track agent distribution and transitions occurring between distinct levels
	 * within the heatmap system.
	 */
	UPROPERTY()
	TArray<int32> LastBetweenFloorCounts;

	UPROPERTY()
	int32 TotalAgentCount = 0;

#pragma endregion PROPERTIES

#pragma region GETTERS_AND_SETTERS
public:
	/** Return a bool to tell us if we have Heatmaps */
	FORCEINLINE bool AnyHeatmapsActive() const	{ return Heatmaps.Num() > 0;}

	/** Return the number of heatmaps */
	FORCEINLINE int32 GetHeatmapCount() const { return Heatmaps.Num(); }

        /** Return the heatmap at the requested index, or nullptr if invalid. */
        FORCEINLINE AHeatmapPixelTextureVisualizer* GetHeatmapByIndex(int32 Index) const
        {
                return Heatmaps.IsValidIndex(Index) ? Heatmaps[Index] : nullptr;
        }
#pragma endregion GETTERS_AND_SETTERS
};
