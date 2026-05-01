// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "BRisk/BRiskSmokeVisualizer.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogBRiskSmokeVisualizer, Log, All);

ABRiskSmokeVisualizer::ABRiskSmokeVisualizer()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		CubeMesh = CubeMeshFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SmokeMaterialFinder(
		TEXT("/Game/B-Risk/Materials/M_SimpleBoxFireTest.M_SimpleBoxFireTest"));
	if (SmokeMaterialFinder.Succeeded())
	{
		SmokeMaterial = SmokeMaterialFinder.Object;
	}
}

bool ABRiskSmokeVisualizer::ConfigureFromRooms(const TArray<FBRiskRoomGeometry>& Rooms, float Scale)
{
	ClearSmokeVolumes();

	if (!CubeMesh)
	{
		CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}

	if (!SmokeMaterial)
	{
		SmokeMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/B-Risk/Materials/M_SimpleBoxFireTest.M_SimpleBoxFireTest"));
	}

	if (!CubeMesh)
	{
		UE_LOG(LogBRiskSmokeVisualizer, Warning, TEXT("Cannot create B-Risk smoke volumes: cube mesh is missing."));
		return false;
	}

	if (!SmokeMaterial)
	{
		UE_LOG(LogBRiskSmokeVisualizer, Warning, TEXT("Cannot create B-Risk smoke volumes: smoke material is missing."));
		return false;
	}

	if (Rooms.Num() == 0 || Scale <= 0.0f)
	{
		return false;
	}

	SmokeVolumeComponents.Reserve(Rooms.Num());
	SmokeMaterialInstances.Reserve(Rooms.Num());
	SmokeVolumeComponents.SetNum(Rooms.Num());
	SmokeMaterialInstances.SetNum(Rooms.Num());

	int32 CreatedVolumeCount = 0;

	for (int32 RoomIndex = 0; RoomIndex < Rooms.Num(); ++RoomIndex)
	{
		const FBRiskRoomGeometry& Room = Rooms[RoomIndex];
		if (Room.Size.X <= 0.0 || Room.Size.Y <= 0.0 || Room.Size.Z <= 0.0)
		{
			UE_LOG(LogBRiskSmokeVisualizer, Warning,
				TEXT("Skipping smoke volume for invalid B-Risk room %d size=%s."),
				Room.RoomId,
				*Room.Size.ToString());
			continue;
		}

		UStaticMeshComponent* SmokeComponent = NewObject<UStaticMeshComponent>(
			this,
			*FString::Printf(TEXT("BRiskSmokeVolume_%d"), RoomIndex));
		SmokeComponent->SetupAttachment(SceneRoot);
		SmokeComponent->SetStaticMesh(CubeMesh);
		SmokeComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SmokeComponent->SetCastShadow(false);
		SmokeComponent->SetReceivesDecals(false);
		SmokeComponent->SetVisibility(true, true);
		SmokeComponent->SetHiddenInGame(false);
		SmokeComponent->SetMobility(EComponentMobility::Movable);

		const FVector OriginCm = Room.Origin * Scale;
		const FVector SizeCm = Room.Size * Scale;
		SmokeComponent->SetRelativeLocation(OriginCm + SizeCm * 0.5f);
		SmokeComponent->SetRelativeScale3D(SizeCm / 100.0f);

		AddInstanceComponent(SmokeComponent);
		SmokeComponent->OnComponentCreated();
		SmokeComponent->RegisterComponent();

		UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(SmokeMaterial, this);
		if (DynamicMaterial)
		{
			DynamicMaterial->SetScalarParameterValue(TEXT("RoomSmoke"), 1.0f);
			SmokeComponent->SetMaterial(0, DynamicMaterial);
		}

		SmokeVolumeComponents[RoomIndex] = SmokeComponent;
		SmokeMaterialInstances[RoomIndex] = DynamicMaterial;
		++CreatedVolumeCount;

		UE_LOG(LogBRiskSmokeVisualizer, Log,
			TEXT("Created B-Risk smoke volume: roomIndex=%d roomId=%d origin=%s size=%s scale=%g originCm=%s sizeCm=%s componentTransform=%s"),
			RoomIndex,
			Room.RoomId,
			*Room.Origin.ToString(),
			*Room.Size.ToString(),
			Scale,
			*OriginCm.ToString(),
			*SizeCm.ToString(),
			*SmokeComponent->GetComponentTransform().ToHumanReadableString());
	}

	UE_LOG(LogBRiskSmokeVisualizer, Log,
		TEXT("Configured B-Risk smoke visualizer volumes: requestedRooms=%d createdVolumes=%d scale=%g material=%s"),
		Rooms.Num(),
		CreatedVolumeCount,
		Scale,
		*GetNameSafe(SmokeMaterial));

	return CreatedVolumeCount > 0;
}

void ABRiskSmokeVisualizer::ClearSmokeVolumes()
{
	for (UStaticMeshComponent* SmokeComponent : SmokeVolumeComponents)
	{
		if (SmokeComponent)
		{
			SmokeComponent->DestroyComponent();
		}
	}

	SmokeVolumeComponents.Reset();
	SmokeMaterialInstances.Reset();
}

bool ABRiskSmokeVisualizer::SetRoomSmokeScalar(int32 RoomIndex, float RoomSmoke)
{
	if (!SmokeVolumeComponents.IsValidIndex(RoomIndex)
		|| !SmokeVolumeComponents[RoomIndex]
		|| !SmokeMaterialInstances.IsValidIndex(RoomIndex)
		|| !SmokeMaterialInstances[RoomIndex])
	{
		return false;
	}

	SmokeMaterialInstances[RoomIndex]->SetScalarParameterValue(
		TEXT("RoomSmoke"),
		FMath::Clamp(RoomSmoke, 0.0f, 1.0f));
	return true;
}

int32 ABRiskSmokeVisualizer::GetSmokeVolumeCount() const
{
	int32 Count = 0;
	for (const UStaticMeshComponent* SmokeComponent : SmokeVolumeComponents)
	{
		if (SmokeComponent)
		{
			++Count;
		}
	}
	return Count;
}
