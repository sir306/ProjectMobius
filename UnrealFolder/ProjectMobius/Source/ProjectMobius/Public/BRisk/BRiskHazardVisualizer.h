// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BRiskDataImporter.h"
#include "BRisk/BRiskVentFlow.h"
#include "BRiskHazardVisualizer.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Runtime visual state for one B-Risk fire source. */
struct FBRiskFireVisualState
{
	float HeatReleaseRateKw = 0.0f;
	float FlameHeightM = 0.0f;
	float FireBaseM = 0.0f;
};

/** Simple cone-based fire and sprinkler indicators for B-Risk playback. */
UCLASS()
class PROJECTMOBIUS_API ABRiskHazardVisualizer : public AActor
{
	GENERATED_BODY()

public:
	ABRiskHazardVisualizer();

	/** Rebuild fire, sprinkler and vent components for the active B-Risk scenario. */
	bool ConfigureFromScenario(
		const TArray<FBRiskRoomGeometry>& Rooms,
		const TArray<FBRiskFireGeometry>& Fires,
		const TArray<FBRiskSprinklerGeometry>& Sprinklers,
		const TArray<FBRiskVentGeometry>& Vents,
		float Scale);

	/** Remove all generated hazard components. */
	void ClearHazardVisuals();

	/** Update one fire cone from sampled B-Risk HRR/flame channels. */
	bool SetFireState(int32 FireIndex, const FBRiskFireVisualState& FireState);

	/** Update all sprinkler indicators for the current simulation time. */
	void SetSimulationTime(float TimeSeconds);

	/**
	 * Update the per-vent in/out flow indicators from derived flow (one entry per vent,
	 * index-aligned with the Vents array passed to ConfigureFromScenario). Out and in
	 * streams are split at the neutral plane, sized by mass flow and coloured by stream
	 * temperature. See UBRiskDataSubsystem::ComputeWallVentFlow.
	 */
	void SetVentFlows(const TArray<FBRiskVentFlow>& VentFlows);

	/** Number of generated visual components. */
	int32 GetHazardVisualCount() const;

	/**
	 * Compute the world-space slab (centre + size, cm) for a B-Risk vent on its
	 * FromRoom wall. The wall is taken from room adjacency when ToRoom is a real
	 * room (the shared wall is geometrically unambiguous and matches Smokeview),
	 * and from the B-Risk/CFAST face id (1=-Y front, 2=+X right, 3=+Y rear,
	 * 4=-X left) only for vents to the exterior (ToRoom == nullptr). Returns
	 * false if FromRoom is missing or the opening has no area.
	 */
	static bool ComputeVentSlab(
		const FBRiskVentGeometry& Vent,
		const FBRiskRoomGeometry* FromRoom,
		const FBRiskRoomGeometry* ToRoom,
		float Scale,
		float ThicknessCm,
		FVector& OutCenterCm,
		FVector& OutSizeCm);

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BasicShapeMaterial;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> SimpleFireNiagaraSystem;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> FireConeComponents;

	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> FireNiagaraComponents;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FireMaterials;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SprinklerConeComponents;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SprinklerMaterials;

	/** One thin slab marking each B-Risk vent/opening on its FromRoom wall. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> VentComponents;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> VentMaterials;

	/** Per-vent cone indicators for the OUT (FromRoom -> ToRoom) and IN flow streams. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> VentFlowOutArrows;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> VentFlowInArrows;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> VentFlowOutMaterials;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> VentFlowInMaterials;

	/** Per-vent opening geometry (cm, world) used to place the flow indicators. */
	struct FVentFlowGeom
	{
		bool bValid = false;
		FVector OpeningCenterCm = FVector::ZeroVector;
		FVector OutwardNormal = FVector::ZeroVector; // unit, points out of FromRoom through the wall
		float SillZCm = 0.0f;
		float HeadZCm = 0.0f;
		float FloorZCm = 0.0f; // FromRoom floor Z in cm (datum for the neutral-plane height)
	};
	TArray<FVentFlowGeom> VentFlowGeometry;

	TArray<FVector> FireBaseLocationsCm;
	TArray<FBRiskSprinklerGeometry> SprinklerData;
	TArray<FVector> SprinklerHeadLocationsCm;
	TArray<float> SprinklerRoomHeightsCm;
	float ScenarioScale = 100.0f;
};
