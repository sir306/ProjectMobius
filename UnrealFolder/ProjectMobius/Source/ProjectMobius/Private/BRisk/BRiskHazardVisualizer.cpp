// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "BRisk/BRiskHazardVisualizer.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogBRiskHazardVisualizer, Log, All);

namespace
{
	constexpr float FireActivationHrrKw = 1.0f;
	constexpr float ConeMeshRadiusCm = 50.0f;
	constexpr float ConeMeshHeightCm = 100.0f;
	constexpr float MaxFireHeightCm = 120.0f;
	constexpr float MinFireRadiusCm = 18.0f;
	constexpr float MinFireConeAngleDeg = 8.0f;
	constexpr float MaxFireConeAngleDeg = 55.0f;

	const FBRiskRoomGeometry* FindRoomById(const TArray<FBRiskRoomGeometry>& Rooms, int32 RoomId)
	{
		return Rooms.FindByPredicate([RoomId](const FBRiskRoomGeometry& Room)
		{
			return Room.RoomId == RoomId;
		});
	}

	FVector TransformHazardRoomLocalToWorldM(const FBRiskRoomGeometry* Room, const FVector& RoomLocalLocationM)
	{
		if (!Room)
		{
			return RoomLocalLocationM;
		}

		// B-Risk/Smokeview reports fire and sprinkler room-local X from the opposite end
		// of the generated room mesh convention used by our VENTGEOM face layout.
		return Room->Origin + FVector(
			Room->Size.X - RoomLocalLocationM.X,
			RoomLocalLocationM.Y,
			RoomLocalLocationM.Z);
	}

	UMaterialInstanceDynamic* MakeColoredMaterial(
		UMaterialInterface* BaseMaterial,
		UObject* Outer,
		const FLinearColor& Color)
	{
		if (!BaseMaterial)
		{
			return nullptr;
		}

		UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, Outer);
		if (DynamicMaterial)
		{
			DynamicMaterial->SetVectorParameterValue(TEXT("Color"), Color);
			DynamicMaterial->SetVectorParameterValue(TEXT("BaseColor"), Color);
		}
		return DynamicMaterial;
	}
}

ABRiskHazardVisualizer::ABRiskHazardVisualizer()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMeshFinder(
		TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeMeshFinder.Succeeded())
	{
		ConeMesh = ConeMeshFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicShapeMaterialFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicShapeMaterialFinder.Succeeded())
	{
		BasicShapeMaterial = BasicShapeMaterialFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SimpleFireNiagaraSystemFinder(
		TEXT("/Game/B-Risk/Niagara/NS_SimpleFire.NS_SimpleFire"));
	if (SimpleFireNiagaraSystemFinder.Succeeded())
	{
		SimpleFireNiagaraSystem = SimpleFireNiagaraSystemFinder.Object;
	}
}

bool ABRiskHazardVisualizer::ConfigureFromScenario(
	const TArray<FBRiskRoomGeometry>& Rooms,
	const TArray<FBRiskFireGeometry>& Fires,
	const TArray<FBRiskSprinklerGeometry>& Sprinklers,
	float Scale)
{
	ClearHazardVisuals();

	if (!ConeMesh)
	{
		ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	}
	if (!BasicShapeMaterial)
	{
		BasicShapeMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (!SimpleFireNiagaraSystem)
	{
		SimpleFireNiagaraSystem = LoadObject<UNiagaraSystem>(
			nullptr,
			TEXT("/Game/B-Risk/Niagara/NS_SimpleFire.NS_SimpleFire"));
	}

	if (!ConeMesh || Scale <= 0.0f)
	{
		return false;
	}

	ScenarioScale = Scale;
	FireConeComponents.SetNum(Fires.Num());
	FireNiagaraComponents.SetNum(Fires.Num());
	FireMaterials.SetNum(Fires.Num());
	FireBaseLocationsCm.SetNum(Fires.Num());

	for (int32 FireIndex = 0; FireIndex < Fires.Num(); ++FireIndex)
	{
		const FBRiskFireGeometry& Fire = Fires[FireIndex];
		const FBRiskRoomGeometry* Room = FindRoomById(Rooms, Fire.RoomId);
		const FVector WorldLocationM = TransformHazardRoomLocalToWorldM(Room, Fire.Location);
		FireBaseLocationsCm[FireIndex] = WorldLocationM * Scale;

		UStaticMeshComponent* FireCone = NewObject<UStaticMeshComponent>(
			this,
			*FString::Printf(TEXT("BRiskFireCone_%d"), FireIndex));
		FireCone->SetupAttachment(SceneRoot);
		FireCone->SetStaticMesh(ConeMesh);
		FireCone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FireCone->SetCastShadow(false);
		FireCone->SetReceivesDecals(false);
		FireCone->SetMobility(EComponentMobility::Movable);
		FireCone->SetVisibility(false, true);
		FireCone->SetHiddenInGame(true);

		AddInstanceComponent(FireCone);
		FireCone->OnComponentCreated();
		FireCone->RegisterComponent();

		UMaterialInstanceDynamic* FireMaterial = MakeColoredMaterial(
			BasicShapeMaterial,
			this,
			FLinearColor(1.0f, 0.22f, 0.02f, 1.0f));
		if (FireMaterial)
		{
			FireCone->SetMaterial(0, FireMaterial);
		}

		FireConeComponents[FireIndex] = FireCone;
		FireMaterials[FireIndex] = FireMaterial;

		if (SimpleFireNiagaraSystem)
		{
			UNiagaraComponent* FireNiagara = NewObject<UNiagaraComponent>(
				this,
				*FString::Printf(TEXT("BRiskSimpleFire_%d"), FireIndex));
			FireNiagara->SetupAttachment(SceneRoot);
			FireNiagara->SetAsset(SimpleFireNiagaraSystem);
			FireNiagara->SetAutoActivate(false);
			FireNiagara->SetRelativeLocation(FireBaseLocationsCm[FireIndex]);
			FireNiagara->SetMobility(EComponentMobility::Movable);
			FireNiagara->SetVisibility(false, true);
			FireNiagara->SetHiddenInGame(true);

			AddInstanceComponent(FireNiagara);
			FireNiagara->OnComponentCreated();
			FireNiagara->RegisterComponent();
			FireNiagara->DeactivateImmediate();

			FireNiagaraComponents[FireIndex] = FireNiagara;
			FireCone->SetVisibility(false, true);
			FireCone->SetHiddenInGame(true);
		}
	}

	SprinklerData = Sprinklers;
	SprinklerConeComponents.SetNum(Sprinklers.Num());
	SprinklerMaterials.SetNum(Sprinklers.Num());
	SprinklerHeadLocationsCm.SetNum(Sprinklers.Num());
	SprinklerRoomHeightsCm.SetNum(Sprinklers.Num());

	for (int32 SprinklerIndex = 0; SprinklerIndex < Sprinklers.Num(); ++SprinklerIndex)
	{
		const FBRiskSprinklerGeometry& Sprinkler = Sprinklers[SprinklerIndex];
		const FBRiskRoomGeometry* Room = FindRoomById(Rooms, Sprinkler.RoomId);
		const float RoomHeightCm = Room ? Room->Size.Z * Scale : 260.0f;
		SprinklerHeadLocationsCm[SprinklerIndex] =
			TransformHazardRoomLocalToWorldM(Room, Sprinkler.Location) * Scale;
		SprinklerRoomHeightsCm[SprinklerIndex] = RoomHeightCm;

		UStaticMeshComponent* SprinklerCone = NewObject<UStaticMeshComponent>(
			this,
			*FString::Printf(TEXT("BRiskSprinklerCone_%d"), SprinklerIndex));
		SprinklerCone->SetupAttachment(SceneRoot);
		SprinklerCone->SetStaticMesh(ConeMesh);
		SprinklerCone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SprinklerCone->SetCastShadow(false);
		SprinklerCone->SetReceivesDecals(false);
		SprinklerCone->SetMobility(EComponentMobility::Movable);
		SprinklerCone->SetRelativeRotation(FRotator(180.0f, 0.0f, 0.0f));
		SprinklerCone->SetVisibility(false, true);
		SprinklerCone->SetHiddenInGame(true);

		AddInstanceComponent(SprinklerCone);
		SprinklerCone->OnComponentCreated();
		SprinklerCone->RegisterComponent();

		UMaterialInstanceDynamic* SprinklerMaterial = MakeColoredMaterial(
			BasicShapeMaterial,
			this,
			FLinearColor(0.18f, 0.55f, 1.0f, 1.0f));
		if (SprinklerMaterial)
		{
			SprinklerCone->SetMaterial(0, SprinklerMaterial);
		}

		SprinklerConeComponents[SprinklerIndex] = SprinklerCone;
		SprinklerMaterials[SprinklerIndex] = SprinklerMaterial;
	}

	UE_LOG(LogBRiskHazardVisualizer, Log,
		TEXT("Configured B-Risk hazard visualizer: fires=%d sprinklers=%d scale=%g"),
		Fires.Num(),
		Sprinklers.Num(),
		Scale);

	return Fires.Num() > 0 || Sprinklers.Num() > 0;
}

void ABRiskHazardVisualizer::ClearHazardVisuals()
{
	for (UStaticMeshComponent* FireCone : FireConeComponents)
	{
		if (FireCone)
		{
			FireCone->DestroyComponent();
		}
	}

	for (UNiagaraComponent* FireNiagara : FireNiagaraComponents)
	{
		if (FireNiagara)
		{
			FireNiagara->DeactivateImmediate();
			FireNiagara->DestroyComponent();
		}
	}

	for (UStaticMeshComponent* SprinklerCone : SprinklerConeComponents)
	{
		if (SprinklerCone)
		{
			SprinklerCone->DestroyComponent();
		}
	}

	FireConeComponents.Reset();
	FireNiagaraComponents.Reset();
	FireMaterials.Reset();
	FireBaseLocationsCm.Reset();
	SprinklerConeComponents.Reset();
	SprinklerMaterials.Reset();
	SprinklerData.Reset();
	SprinklerHeadLocationsCm.Reset();
	SprinklerRoomHeightsCm.Reset();
}

bool ABRiskHazardVisualizer::SetFireState(int32 FireIndex, const FBRiskFireVisualState& FireState)
{
	if (!FireConeComponents.IsValidIndex(FireIndex)
		|| !FireBaseLocationsCm.IsValidIndex(FireIndex))
	{
		return false;
	}

	UStaticMeshComponent* FireCone = FireConeComponents[FireIndex];
	UNiagaraComponent* FireNiagara =
		FireNiagaraComponents.IsValidIndex(FireIndex) ? FireNiagaraComponents[FireIndex] : nullptr;
	const bool bFireOn = FireState.HeatReleaseRateKw >= FireActivationHrrKw
		&& FireState.FlameHeightM > 0.0f;

	if (FireNiagara)
	{
		FireNiagara->SetVisibility(bFireOn, true);
		FireNiagara->SetHiddenInGame(!bFireOn);
		if (!bFireOn)
		{
			FireNiagara->DeactivateImmediate();
		}
	}
	if (FireCone)
	{
		const bool bShowFallbackCone = bFireOn && !FireNiagara;
		FireCone->SetVisibility(bShowFallbackCone, true);
		FireCone->SetHiddenInGame(!bShowFallbackCone);
	}
	if (!bFireOn)
	{
		return true;
	}

	const float FlameHeightCm = FMath::Clamp(FireState.FlameHeightM * ScenarioScale * 0.38f, 18.0f, MaxFireHeightCm);
	const float HrrSizeBoost = FMath::Clamp(FMath::Sqrt(FireState.HeatReleaseRateKw) * 0.75f, 0.0f, 42.0f);
	const float FireRadiusCm = FMath::Clamp(
		FireState.FireBaseM * ScenarioScale * 0.45f + HrrSizeBoost,
		MinFireRadiusCm,
		80.0f);
	const FVector BaseLocationCm = FireBaseLocationsCm[FireIndex];

	const FVector FireScale(
		FireRadiusCm / ConeMeshRadiusCm,
		FireRadiusCm / ConeMeshRadiusCm,
		FlameHeightCm / ConeMeshHeightCm);
	const float FireConeAngleDeg = FMath::Clamp(
		FMath::RadiansToDegrees(FMath::Atan2(FireRadiusCm, FMath::Max(FlameHeightCm, 1.0f))),
		MinFireConeAngleDeg,
		MaxFireConeAngleDeg);

	if (FireNiagara)
	{
		FireNiagara->SetRelativeLocation(BaseLocationCm);
		FireNiagara->SetRelativeScale3D(FVector::OneVector);
		FireNiagara->SetVariableFloat(TEXT("User.FireHeight"), FlameHeightCm);
		FireNiagara->SetVariableFloat(TEXT("User.FireConeAngle"), FireConeAngleDeg);
		FireNiagara->SetVariableFloat(TEXT("User.HeatReleaseRateKw"), FireState.HeatReleaseRateKw);
		FireNiagara->SetVariableFloat(TEXT("User.FlameHeightM"), FireState.FlameHeightM);
		FireNiagara->SetVariableFloat(TEXT("User.FlameHeightCm"), FlameHeightCm);
		FireNiagara->SetVariableFloat(TEXT("User.FireRadiusCm"), FireRadiusCm);
		if (!FireNiagara->IsActive())
		{
			FireNiagara->Activate(true);
		}
	}

	if (FireCone && !FireNiagara)
	{
		FireCone->SetRelativeLocation(BaseLocationCm + FVector(0.0f, 0.0f, FlameHeightCm * 0.5f));
		FireCone->SetRelativeScale3D(FireScale);
	}

	return true;
}

void ABRiskHazardVisualizer::SetSimulationTime(float TimeSeconds)
{
	for (int32 SprinklerIndex = 0; SprinklerIndex < SprinklerConeComponents.Num(); ++SprinklerIndex)
	{
		if (!SprinklerConeComponents[SprinklerIndex]
			|| !SprinklerData.IsValidIndex(SprinklerIndex)
			|| !SprinklerHeadLocationsCm.IsValidIndex(SprinklerIndex)
			|| !SprinklerRoomHeightsCm.IsValidIndex(SprinklerIndex))
		{
			continue;
		}

		const FBRiskSprinklerGeometry& Sprinkler = SprinklerData[SprinklerIndex];
		const bool bHasActivated = Sprinkler.ActivationTimeSeconds >= 0.0
			&& TimeSeconds >= Sprinkler.ActivationTimeSeconds;
		UStaticMeshComponent* SprinklerCone = SprinklerConeComponents[SprinklerIndex];
		SprinklerCone->SetVisibility(bHasActivated, true);
		SprinklerCone->SetHiddenInGame(!bHasActivated);
		if (!bHasActivated)
		{
			continue;
		}

		const float Ramp = FMath::Clamp(
			static_cast<float>((TimeSeconds - Sprinkler.ActivationTimeSeconds) / 5.0),
			0.2f,
			1.0f);
		const float HeadHeightCm = SprinklerHeadLocationsCm[SprinklerIndex].Z;
		const float SprayHeightCm = FMath::Clamp(HeadHeightCm * 0.35f * Ramp, 25.0f, 90.0f);
		const float SprayRadiusCm = FMath::Clamp(
			static_cast<float>(Sprinkler.SprayRadius * ScenarioScale) * 0.16f * Ramp,
			14.0f,
			70.0f);

		SprinklerCone->SetRelativeLocation(SprinklerHeadLocationsCm[SprinklerIndex] - FVector(0.0f, 0.0f, SprayHeightCm * 0.5f));
		SprinklerCone->SetRelativeScale3D(FVector(
			SprayRadiusCm / ConeMeshRadiusCm,
			SprayRadiusCm / ConeMeshRadiusCm,
			SprayHeightCm / ConeMeshHeightCm));
	}
}

int32 ABRiskHazardVisualizer::GetHazardVisualCount() const
{
	int32 Count = 0;
	for (const UStaticMeshComponent* FireCone : FireConeComponents)
	{
		if (FireCone)
		{
			++Count;
		}
	}
	for (const UNiagaraComponent* FireNiagara : FireNiagaraComponents)
	{
		if (FireNiagara)
		{
			++Count;
		}
	}
	for (const UStaticMeshComponent* SprinklerCone : SprinklerConeComponents)
	{
		if (SprinklerCone)
		{
			++Count;
		}
	}
	return Count;
}
