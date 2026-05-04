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

	/** Rebuild smoke volume components to match the supplied B-Risk rooms. */
	bool ConfigureFromRooms(const TArray<FBRiskRoomGeometry>& Rooms, float Scale);

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
	TObjectPtr<UNiagaraSystem> SmokeNiagaraSystem;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SmokeVolumeComponents;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SmokeMaterialInstances;

	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> SmokeNiagaraComponents;

	TArray<FVector> SmokeRoomSizesCm;
	TArray<FBRiskSmokeVisualState> LastSmokeStates;
	bool bSmokeSimulationPaused = true;
};
