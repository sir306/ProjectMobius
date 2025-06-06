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
#include "MassEntitySubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "HeatmapSubsystem.generated.h"

class AHeatmapPixelTextureVisualizer;
class UHeatmapSubsystem;
class AHeatmapVisualizer;

// Delegates used to broadcast events
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHeatmapAdded, AHeatmapPixelTextureVisualizer*, HeatmapActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHeatmapRemoved, AHeatmapPixelTextureVisualizer*, HeatmapActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FUpdateFloorStatCount, int32, FloorNumber, int32, AgentCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FUpdateBetweenFloorStatCount, int32, FloorNumber, int32, AgentCount);


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
	   // TODO: bound it to the OnMeshBuilt delegate -> this needs refactoring to do as this needs moving to prevent circular dependencies
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
	void AddHeatmapActor(AHeatmapVisualizer* HeatmapActor);
	void AddHeatmapActor(AHeatmapPixelTextureVisualizer* HeatmapActor);
	void RemoveHeatmapActor(AHeatmapPixelTextureVisualizer* HeatmapActor);

	void UpdateHeatmaps(const FVector& AgentLocation);
	void UpdateHeatmapsWithLocations(const TArray<FVector>& LocationArray);
	

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
	void BroadcastTotalAgentCount(int32 TotalAgentCount) const
	{
		OnUpdateFloorStatCount.Broadcast(-1, TotalAgentCount);
	}

protected:

private:
	// these methods are responsible for debouncing the heatmap generation and preventing collisions on updating data
	/** Schedule Heatmap Generation */
	void ScheduleHeatmapGeneration();
	
	/** Process Heatmap Generation */
	void ProcessHeatmapGeneration();

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
	
protected:
	/** Stores all the heatmaps of the world */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Heatmap|Subsystem")
	//TArray<class AHeatmapVisualizer*> Heatmaps;
	TArray<AHeatmapPixelTextureVisualizer*> Heatmaps;

	
private:
	/** The XY spawn point for the heatmaps */
	FVector2D XYSpawnLocation;
	
	/** 2D Bounding Size of the heatmap  */
	FVector2D HeatmapBoundingSize = FVector2D(0, 0);

	/** Array of Height Spawn Locations, this stores all the height spawn locations of the heatmaps, this is done as
	 * mesh generation can at times take longer than the loading of the pedestrian data */
	TArray<float> HeightSpawnLocations = TArray<float>();

	// these properties are responsible for debouncing the heatmap generation and preventing collisions on updating data
	/** Protect the data updates with a Critical Section */
	FCriticalSection HeightSpawnDataLock;

	/** Timer handle used for debouncing */
	FTimerHandle HeatmapGenerationTimerHandle;


#pragma endregion PROPERTIES

#pragma region GETTERS_AND_SETTERS
public:
	/** Return a bool to tell us if we have Heatmaps */
	FORCEINLINE bool AnyHeatmapsActive() const	{ return Heatmaps.Num() > 0;}

	/** Return the number of heatmaps */
	FORCEINLINE int32 GetHeatmapCount() const { return Heatmaps.Num(); }
#pragma endregion GETTERS_AND_SETTERS
};
