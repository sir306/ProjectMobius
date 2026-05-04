// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "BRisk/BRiskSmokeVisualizer.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogBRiskSmokeVisualizer, Log, All);

namespace
{
	constexpr float SmokeNiagaraActivationDensityThreshold = 0.02f;

	bool ShouldRunSmokeNiagara(const FBRiskSmokeVisualState& SmokeState)
	{
		return FMath::Clamp(SmokeState.SmokeDensity, 0.0f, 1.0f) >= SmokeNiagaraActivationDensityThreshold;
	}
}

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

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SmokeNiagaraSystemFinder(
		TEXT("/Game/B-Risk/Niagara/NS_B-RiskSmoke.NS_B-RiskSmoke"));
	if (SmokeNiagaraSystemFinder.Succeeded())
	{
		SmokeNiagaraSystem = SmokeNiagaraSystemFinder.Object;
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

	if (!SmokeNiagaraSystem)
	{
		SmokeNiagaraSystem = LoadObject<UNiagaraSystem>(
			nullptr,
			TEXT("/Game/B-Risk/Niagara/NS_B-RiskSmoke.NS_B-RiskSmoke"));
	}

	if (Rooms.Num() == 0 || Scale <= 0.0f)
	{
		return false;
	}

	SmokeVolumeComponents.Reserve(Rooms.Num());
	SmokeMaterialInstances.Reserve(Rooms.Num());
	SmokeNiagaraComponents.Reserve(Rooms.Num());
	SmokeRoomSizesCm.Reserve(Rooms.Num());
	LastSmokeStates.Reserve(Rooms.Num());
	SmokeVolumeComponents.SetNum(Rooms.Num());
	SmokeMaterialInstances.SetNum(Rooms.Num());
	SmokeNiagaraComponents.SetNum(Rooms.Num());
	SmokeRoomSizesCm.SetNum(Rooms.Num());
	LastSmokeStates.SetNum(Rooms.Num());

	int32 CreatedVolumeCount = 0;
	int32 CreatedNiagaraCount = 0;

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

		const FVector OriginCm = Room.Origin * Scale;
		const FVector SizeCm = Room.Size * Scale;
		SmokeRoomSizesCm[RoomIndex] = SizeCm;

		if (CubeMesh && SmokeMaterial)
		{
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
			SmokeComponent->SetRelativeLocation(OriginCm + SizeCm * 0.5f);
			SmokeComponent->SetRelativeScale3D(SizeCm / 100.0f);

			AddInstanceComponent(SmokeComponent);
			SmokeComponent->OnComponentCreated();
			SmokeComponent->RegisterComponent();

			UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(SmokeMaterial, this);
			if (DynamicMaterial)
			{
				DynamicMaterial->SetScalarParameterValue(TEXT("RoomSmoke"), 1.0f);
				DynamicMaterial->SetScalarParameterValue(TEXT("SmokeDensity"), 0.0f);
				DynamicMaterial->SetScalarParameterValue(TEXT("SmokeHeat"), 0.0f);
				SmokeComponent->SetMaterial(0, DynamicMaterial);
			}

			SmokeVolumeComponents[RoomIndex] = SmokeComponent;
			SmokeMaterialInstances[RoomIndex] = DynamicMaterial;
			++CreatedVolumeCount;
		}
		else if (SmokeNiagaraSystem)
		{
			UNiagaraComponent* NiagaraComponent = NewObject<UNiagaraComponent>(
				this,
				*FString::Printf(TEXT("BRiskSmokeNiagara_%d"), RoomIndex));
			NiagaraComponent->SetupAttachment(SceneRoot);
			NiagaraComponent->SetAsset(SmokeNiagaraSystem);
			NiagaraComponent->SetAutoActivate(false);
			NiagaraComponent->SetRelativeLocation(OriginCm + FVector(SizeCm.X * 0.5f, SizeCm.Y * 0.5f, 0.0f));
			NiagaraComponent->SetMobility(EComponentMobility::Movable);
			NiagaraComponent->SetVariableVec3(TEXT("User.RoomSizeCm"), SizeCm);
			NiagaraComponent->SetVariableVec3(TEXT("User.RoomOriginCm"), OriginCm);
			NiagaraComponent->SetVariableFloat(TEXT("User.RoomSmoke"), 1.0f);
			NiagaraComponent->SetVariableFloat(TEXT("User.SmokeDensity"), 0.0f);
			NiagaraComponent->SetVariableFloat(TEXT("User.SmokeHeat"), 0.0f);

			AddInstanceComponent(NiagaraComponent);
			NiagaraComponent->OnComponentCreated();
			NiagaraComponent->RegisterComponent();
			NiagaraComponent->DeactivateImmediate();

			SmokeNiagaraComponents[RoomIndex] = NiagaraComponent;
			++CreatedNiagaraCount;
		}
		else
		{
			UE_LOG(LogBRiskSmokeVisualizer, Warning,
				TEXT("Skipping B-Risk smoke visual for room %d: Niagara system is missing and cube fallback is unavailable."),
				Room.RoomId);
			continue;
		}

		UE_LOG(LogBRiskSmokeVisualizer, Log,
			TEXT("Created B-Risk smoke visual: roomIndex=%d roomId=%d origin=%s size=%s scale=%g originCm=%s sizeCm=%s niagara=%s fallbackCube=%s"),
			RoomIndex,
			Room.RoomId,
			*Room.Origin.ToString(),
			*Room.Size.ToString(),
			Scale,
			*OriginCm.ToString(),
			*SizeCm.ToString(),
			SmokeNiagaraComponents[RoomIndex] ? TEXT("true") : TEXT("false"),
			SmokeVolumeComponents[RoomIndex] ? TEXT("true") : TEXT("false"));
	}

	UE_LOG(LogBRiskSmokeVisualizer, Log,
		TEXT("Configured B-Risk smoke visualizer: requestedRooms=%d createdNiagara=%d createdFallbackVolumes=%d scale=%g niagara=%s material=%s"),
		Rooms.Num(),
		CreatedNiagaraCount,
		CreatedVolumeCount,
		Scale,
		*GetNameSafe(SmokeNiagaraSystem),
		*GetNameSafe(SmokeMaterial));

	return CreatedNiagaraCount > 0 || CreatedVolumeCount > 0;
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

	for (UNiagaraComponent* NiagaraComponent : SmokeNiagaraComponents)
	{
		if (NiagaraComponent)
		{
			NiagaraComponent->DeactivateImmediate();
			NiagaraComponent->DestroyComponent();
		}
	}

	SmokeNiagaraComponents.Reset();
	SmokeRoomSizesCm.Reset();
	LastSmokeStates.Reset();
	bSmokeSimulationPaused = true;
}

bool ABRiskSmokeVisualizer::SetRoomSmokeState(int32 RoomIndex, const FBRiskSmokeVisualState& SmokeState)
{
	const float RoomSmoke = FMath::Clamp(SmokeState.RoomSmoke, 0.0f, 1.0f);
	const float SmokeDensity = FMath::Clamp(SmokeState.SmokeDensity, 0.0f, 1.0f);
	const float SmokeHeat = FMath::Clamp(SmokeState.SmokeHeat, 0.0f, 1.0f);

	bool bUpdated = false;

	if (LastSmokeStates.IsValidIndex(RoomIndex))
	{
		LastSmokeStates[RoomIndex] = SmokeState;
	}

	if (SmokeNiagaraComponents.IsValidIndex(RoomIndex) && SmokeNiagaraComponents[RoomIndex])
	{
		UNiagaraComponent* NiagaraComponent = SmokeNiagaraComponents[RoomIndex];
		NiagaraComponent->SetVariableFloat(TEXT("User.RoomSmoke"), RoomSmoke);
		NiagaraComponent->SetVariableFloat(TEXT("User.SmokeDensity"), SmokeDensity);
		NiagaraComponent->SetVariableFloat(TEXT("User.SmokeHeat"), SmokeHeat);

		if (SmokeRoomSizesCm.IsValidIndex(RoomIndex))
		{
			NiagaraComponent->SetVariableVec3(TEXT("User.RoomSizeCm"), SmokeRoomSizesCm[RoomIndex]);
		}

		const bool bShouldRunNiagara = ShouldRunSmokeNiagara(SmokeState);
		if (!bShouldRunNiagara)
		{
			NiagaraComponent->DeactivateImmediate();
		}
		else if (bSmokeSimulationPaused)
		{
			if (NiagaraComponent->IsActive())
			{
				NiagaraComponent->SetPaused(true);
			}
		}
		else
		{
			if (!NiagaraComponent->IsActive())
			{
				NiagaraComponent->Activate(true);
			}
			NiagaraComponent->SetPaused(false);
		}

		bUpdated = true;
	}

	if (SmokeVolumeComponents.IsValidIndex(RoomIndex)
		&& SmokeVolumeComponents[RoomIndex]
		&& SmokeMaterialInstances.IsValidIndex(RoomIndex)
		&& SmokeMaterialInstances[RoomIndex])
	{
		SmokeMaterialInstances[RoomIndex]->SetScalarParameterValue(TEXT("RoomSmoke"), RoomSmoke);
		SmokeMaterialInstances[RoomIndex]->SetScalarParameterValue(TEXT("SmokeDensity"), SmokeDensity);
		SmokeMaterialInstances[RoomIndex]->SetScalarParameterValue(TEXT("SmokeHeat"), SmokeHeat);
		bUpdated = true;
	}

	return bUpdated;
}

void ABRiskSmokeVisualizer::SetSmokeSimulationPaused(bool bPaused)
{
	if (bSmokeSimulationPaused == bPaused)
	{
		return;
	}

	bSmokeSimulationPaused = bPaused;

	for (int32 RoomIndex = 0; RoomIndex < SmokeNiagaraComponents.Num(); ++RoomIndex)
	{
		UNiagaraComponent* NiagaraComponent = SmokeNiagaraComponents[RoomIndex];
		if (!NiagaraComponent)
		{
			continue;
		}

		const bool bShouldRunNiagara = LastSmokeStates.IsValidIndex(RoomIndex)
			&& ShouldRunSmokeNiagara(LastSmokeStates[RoomIndex]);

		if (!bShouldRunNiagara)
		{
			NiagaraComponent->DeactivateImmediate();
			continue;
		}

		if (bSmokeSimulationPaused)
		{
			if (NiagaraComponent->IsActive())
			{
				NiagaraComponent->SetPaused(true);
			}
			continue;
		}

		if (!NiagaraComponent->IsActive())
		{
			NiagaraComponent->Activate(true);
		}
		NiagaraComponent->SetPaused(false);
	}
}

bool ABRiskSmokeVisualizer::SetRoomSmokeScalar(int32 RoomIndex, float RoomSmoke)
{
	FBRiskSmokeVisualState SmokeState;
	SmokeState.RoomSmoke = RoomSmoke;
	return SetRoomSmokeState(RoomIndex, SmokeState);
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
	for (const UNiagaraComponent* NiagaraComponent : SmokeNiagaraComponents)
	{
		if (NiagaraComponent)
		{
			++Count;
		}
	}
	return Count;
}
