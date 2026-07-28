// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BRiskDataImporter.h"
#include "BRisk/BRiskSmokeVisualState.h"
#include "BRiskSmokeVisualizer.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Owns the B-Risk room smoke Niagara components and fallback smoke boxes. */
UCLASS()
class PROJECTMOBIUS_API ABRiskSmokeVisualizer : public AActor
{
	GENERATED_BODY()

public:
	ABRiskSmokeVisualizer();

	/**
	 * Rebuild smoke volume components to match the supplied B-Risk rooms.
	 *
	 * Frame must be the scenario's FBRiskScenarioData::RoomFrame - the same value handed to the
	 * hazard visualizer and the egress subsystem.
	 */
	bool ConfigureFromRooms(
		const TArray<FBRiskRoomGeometry>& Rooms,
		float Scale,
		BRiskCoord::ERoomFrame Frame);

	/** Remove all generated smoke volume components. */
	void ClearSmokeVolumes();

	/** Set all smoke material parameters for one generated smoke volume. */
	bool SetRoomSmokeState(int32 RoomIndex, const FBRiskSmokeVisualState& SmokeState);

	/** Pause or resume Niagara smoke simulation without changing the sampled smoke state. */
	void SetSmokeSimulationPaused(bool bPaused);

	/** Set RoomSmoke for one generated smoke volume. Kept for older callers. */
	bool SetRoomSmokeScalar(int32 RoomIndex, float RoomSmoke);

	/** Number of generated smoke volume components. */
	int32 GetSmokeVolumeCount() const;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SmokeMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SmokeOutlineEdgeMaterial;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> SmokeNiagaraSystem;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SmokeVolumeComponents;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SmokeMaterialInstances;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SmokeOutlineEdgeComponents;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SmokeOutlineEdgeMaterialInstances;

	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> SmokeNiagaraComponents;

	/** Per-room bounding box of the footprint (polygon bbox when there is one). */
	TArray<FVector> SmokeRoomOriginsCm;
	TArray<FVector> SmokeRoomSizesCm;

	/**
	 * Per-room footprint ring in UE XY (cm), counter-clockwise, from Zones-data.json. Empty for a
	 * room that fell back to the equivalent rectangle; those keep the 12-edge box outline.
	 */
	TArray<TArray<FVector2D>> SmokeRoomPolygonsCm;

	/**
	 * Start index of each room's block in SmokeOutlineEdgeComponents. The block size is per-room
	 * (a polygon prism needs 4 edges per vertex, a box needs 16), so there is no fixed stride.
	 */
	TArray<int32> SmokeOutlineEdgeOffsets;

	/**
	 * Per-room footprint coverage mask handed to Niagara as User.FootprintMask. Keeps the volume a
	 * box over the bounding box while restricting which voxels render to the real plan. UPROPERTY
	 * because these are transient textures with no other owner - without it they are collected.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> SmokeFootprintMasks;

	TArray<FBRiskSmokeVisualState> LastSmokeStates;
	bool bSmokeSimulationPaused = true;

	void UpdateSmokeOutlineEdges(int32 RoomIndex, const FBRiskSmokeVisualState& SmokeState);
};
