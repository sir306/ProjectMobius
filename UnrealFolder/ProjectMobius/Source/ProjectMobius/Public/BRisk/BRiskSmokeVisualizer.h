// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BRiskDataImporter.h"
#include "BRiskSmokeVisualizer.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Owns the room-sized B-Risk smoke boxes and their RoomSmoke material parameters. */
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

	/** Set RoomSmoke for one generated smoke volume. */
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
	TArray<TObjectPtr<UStaticMeshComponent>> SmokeVolumeComponents;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SmokeMaterialInstances;
};
