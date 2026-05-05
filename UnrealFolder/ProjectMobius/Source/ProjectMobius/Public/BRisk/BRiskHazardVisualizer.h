// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BRiskDataImporter.h"
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

	/** Rebuild fire and sprinkler components for the active B-Risk scenario. */
	bool ConfigureFromScenario(
		const TArray<FBRiskRoomGeometry>& Rooms,
		const TArray<FBRiskFireGeometry>& Fires,
		const TArray<FBRiskSprinklerGeometry>& Sprinklers,
		float Scale);

	/** Remove all generated hazard components. */
	void ClearHazardVisuals();

	/** Update one fire cone from sampled B-Risk HRR/flame channels. */
	bool SetFireState(int32 FireIndex, const FBRiskFireVisualState& FireState);

	/** Update all sprinkler indicators for the current simulation time. */
	void SetSimulationTime(float TimeSeconds);

	/** Number of generated visual components. */
	int32 GetHazardVisualCount() const;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;

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

	TArray<FVector> FireBaseLocationsCm;
	TArray<FBRiskSprinklerGeometry> SprinklerData;
	TArray<FVector> SprinklerHeadLocationsCm;
	TArray<float> SprinklerRoomHeightsCm;
	float ScenarioScale = 100.0f;
};
