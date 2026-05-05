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
	constexpr int32 SmokeOutlineEdgesPerBox = 12;
	constexpr int32 SmokeOutlineEdgesPerRoom = SmokeOutlineEdgesPerBox * 2;
	constexpr float SmokeOutlineThicknessCm = 2.0f;
	const FLinearColor HotSmokeOutlineColor(0.018f, 0.014f, 0.010f, 1.0f);
	const FLinearColor CoolSmokeOutlineColor(0.035f, 0.18f, 0.85f, 1.0f);

	bool ShouldRunSmokeNiagara(const FBRiskSmokeVisualState& SmokeState)
	{
		return FMath::Clamp(SmokeState.SmokeDensity, 0.0f, 1.0f) >= SmokeNiagaraActivationDensityThreshold;
	}

	void ConfigureOutlineEdge(
		UStaticMeshComponent* EdgeComponent,
		const FVector& CenterCm,
		const FVector& SizeCm)
	{
		if (!EdgeComponent)
		{
			return;
		}

		EdgeComponent->SetRelativeLocation(CenterCm);
		EdgeComponent->SetRelativeScale3D(SizeCm / 100.0f);
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

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SmokeOutlineOverlayMaterialFinder(
		TEXT("/Game/B-Risk/Materials/M_SimpleBoxFireOutlineOverlay.M_SimpleBoxFireOutlineOverlay"));
	if (SmokeOutlineOverlayMaterialFinder.Succeeded())
	{
		SmokeOutlineOverlayMaterial = SmokeOutlineOverlayMaterialFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SmokeOutlineEdgeMaterialFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (SmokeOutlineEdgeMaterialFinder.Succeeded())
	{
		SmokeOutlineEdgeMaterial = SmokeOutlineEdgeMaterialFinder.Object;
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

	if (!SmokeOutlineOverlayMaterial)
	{
		SmokeOutlineOverlayMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/B-Risk/Materials/M_SimpleBoxFireOutlineOverlay.M_SimpleBoxFireOutlineOverlay"));
	}

	if (!SmokeOutlineEdgeMaterial)
	{
		SmokeOutlineEdgeMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
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
	SmokeOutlineOverlayMaterialInstances.Reserve(Rooms.Num());
	SmokeOutlineEdgeComponents.Reserve(Rooms.Num() * SmokeOutlineEdgesPerRoom);
	SmokeOutlineEdgeMaterialInstances.Reserve(Rooms.Num() * SmokeOutlineEdgesPerRoom);
	SmokeNiagaraComponents.Reserve(Rooms.Num());
	SmokeRoomOriginsCm.Reserve(Rooms.Num());
	SmokeRoomSizesCm.Reserve(Rooms.Num());
	LastSmokeStates.Reserve(Rooms.Num());
	SmokeVolumeComponents.SetNum(Rooms.Num());
	SmokeMaterialInstances.SetNum(Rooms.Num());
	SmokeOutlineOverlayMaterialInstances.SetNum(Rooms.Num());
	SmokeOutlineEdgeComponents.SetNum(Rooms.Num() * SmokeOutlineEdgesPerRoom);
	SmokeOutlineEdgeMaterialInstances.SetNum(Rooms.Num() * SmokeOutlineEdgesPerRoom);
	SmokeNiagaraComponents.SetNum(Rooms.Num());
	SmokeRoomOriginsCm.SetNum(Rooms.Num());
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
		SmokeRoomOriginsCm[RoomIndex] = OriginCm;
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
				DynamicMaterial->SetScalarParameterValue(TEXT("UpperOpticalDensity"), 0.0f);
				DynamicMaterial->SetScalarParameterValue(TEXT("SmokeDensity"), 0.0f);
				DynamicMaterial->SetScalarParameterValue(TEXT("SmokeHeat"), 0.0f);
				SmokeComponent->SetMaterial(0, DynamicMaterial);
			}

			for (int32 EdgeIndex = 0; EdgeIndex < SmokeOutlineEdgesPerRoom; ++EdgeIndex)
			{
				UStaticMeshComponent* EdgeComponent = NewObject<UStaticMeshComponent>(
					this,
					*FString::Printf(TEXT("BRiskSmokeOutlineEdge_%d_%d"), RoomIndex, EdgeIndex));
				EdgeComponent->SetupAttachment(SceneRoot);
				EdgeComponent->SetStaticMesh(CubeMesh);
				EdgeComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				EdgeComponent->SetCastShadow(false);
				EdgeComponent->SetReceivesDecals(false);
				EdgeComponent->SetVisibility(false, true);
				EdgeComponent->SetHiddenInGame(true);
				EdgeComponent->SetMobility(EComponentMobility::Movable);

				AddInstanceComponent(EdgeComponent);
				EdgeComponent->OnComponentCreated();
				EdgeComponent->RegisterComponent();

				UMaterialInstanceDynamic* EdgeMaterial = nullptr;
				if (SmokeOutlineEdgeMaterial)
				{
					EdgeMaterial = UMaterialInstanceDynamic::Create(SmokeOutlineEdgeMaterial, this);
					if (EdgeMaterial)
					{
						const FLinearColor EdgeColor = EdgeIndex < SmokeOutlineEdgesPerBox
							? HotSmokeOutlineColor
							: CoolSmokeOutlineColor;
						EdgeMaterial->SetVectorParameterValue(TEXT("Color"), EdgeColor);
						EdgeMaterial->SetVectorParameterValue(TEXT("BaseColor"), EdgeColor);
						EdgeComponent->SetMaterial(0, EdgeMaterial);
					}
				}

				const int32 FlatEdgeIndex = RoomIndex * SmokeOutlineEdgesPerRoom + EdgeIndex;
				SmokeOutlineEdgeComponents[FlatEdgeIndex] = EdgeComponent;
				SmokeOutlineEdgeMaterialInstances[FlatEdgeIndex] = EdgeMaterial;
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
			NiagaraComponent->SetVariableFloat(TEXT("User.UpperOpticalDensity"), 0.0f);
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
	SmokeOutlineOverlayMaterialInstances.Reset();
	for (UStaticMeshComponent* EdgeComponent : SmokeOutlineEdgeComponents)
	{
		if (EdgeComponent)
		{
			EdgeComponent->DestroyComponent();
		}
	}
	SmokeOutlineEdgeComponents.Reset();
	SmokeOutlineEdgeMaterialInstances.Reset();

	for (UNiagaraComponent* NiagaraComponent : SmokeNiagaraComponents)
	{
		if (NiagaraComponent)
		{
			NiagaraComponent->DeactivateImmediate();
			NiagaraComponent->DestroyComponent();
		}
	}

	SmokeNiagaraComponents.Reset();
	SmokeRoomOriginsCm.Reset();
	SmokeRoomSizesCm.Reset();
	LastSmokeStates.Reset();
	bSmokeSimulationPaused = true;
}

bool ABRiskSmokeVisualizer::SetRoomSmokeState(int32 RoomIndex, const FBRiskSmokeVisualState& SmokeState)
{
	const float RoomSmoke = FMath::Clamp(SmokeState.RoomSmoke, 0.0f, 1.0f);
	const float UpperOpticalDensity = FMath::Max(SmokeState.UpperOpticalDensity, 0.0f);
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
		NiagaraComponent->SetVariableFloat(TEXT("User.UpperOpticalDensity"), UpperOpticalDensity);
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
		SmokeMaterialInstances[RoomIndex]->SetScalarParameterValue(TEXT("UpperOpticalDensity"), UpperOpticalDensity);
		SmokeMaterialInstances[RoomIndex]->SetScalarParameterValue(TEXT("SmokeDensity"), SmokeDensity);
		SmokeMaterialInstances[RoomIndex]->SetScalarParameterValue(TEXT("SmokeHeat"), SmokeHeat);
		bUpdated = true;
	}

	if (SmokeOutlineOverlayMaterialInstances.IsValidIndex(RoomIndex)
		&& SmokeOutlineOverlayMaterialInstances[RoomIndex])
	{
		SmokeOutlineOverlayMaterialInstances[RoomIndex]->SetScalarParameterValue(TEXT("RoomSmoke"), RoomSmoke);
		SmokeOutlineOverlayMaterialInstances[RoomIndex]->SetScalarParameterValue(TEXT("SmokeDensity"), SmokeDensity);
		SmokeOutlineOverlayMaterialInstances[RoomIndex]->SetScalarParameterValue(TEXT("SmokeHeat"), SmokeHeat);
		bUpdated = true;
	}

	UpdateSmokeOutlineEdges(RoomIndex, SmokeState);

	return bUpdated;
}

void ABRiskSmokeVisualizer::UpdateSmokeOutlineEdges(int32 RoomIndex, const FBRiskSmokeVisualState& SmokeState)
{
	if (!SmokeRoomOriginsCm.IsValidIndex(RoomIndex) || !SmokeRoomSizesCm.IsValidIndex(RoomIndex))
	{
		return;
	}

	const float Density = FMath::Clamp(SmokeState.SmokeDensity, 0.0f, 1.0f);
	const float RoomSmoke = FMath::Clamp(SmokeState.RoomSmoke, 0.0f, 1.0f);
	const bool bShowHotOutline = Density >= SmokeNiagaraActivationDensityThreshold && RoomSmoke < 0.995f;
	const bool bShowCoolOutline = RoomSmoke > 0.005f;

	const FVector OriginCm = SmokeRoomOriginsCm[RoomIndex];
	const FVector SizeCm = SmokeRoomSizesCm[RoomIndex];
	const float FloorZ = OriginCm.Z;
	const float LayerZ = OriginCm.Z + SizeCm.Z * RoomSmoke;
	const float TopZ = OriginCm.Z + SizeCm.Z;

	const float MinX = OriginCm.X;
	const float MaxX = OriginCm.X + SizeCm.X;
	const float MinY = OriginCm.Y;
	const float MaxY = OriginCm.Y + SizeCm.Y;
	const float MidX = (MinX + MaxX) * 0.5f;
	const float MidY = (MinY + MaxY) * 0.5f;
	const float T = SmokeOutlineThicknessCm;

	const auto ConfigureBoxOutline = [&](
		int32 EdgeBaseIndex,
		bool bVisible,
		float BottomZ,
		float BoxTopZ)
	{
		const float HeightCm = FMath::Max(BoxTopZ - BottomZ, SmokeOutlineThicknessCm);
		const float MidZ = (BottomZ + BoxTopZ) * 0.5f;

		for (int32 LocalEdgeIndex = 0; LocalEdgeIndex < SmokeOutlineEdgesPerBox; ++LocalEdgeIndex)
		{
			const int32 FlatEdgeIndex = RoomIndex * SmokeOutlineEdgesPerRoom + EdgeBaseIndex + LocalEdgeIndex;
			if (!SmokeOutlineEdgeComponents.IsValidIndex(FlatEdgeIndex) || !SmokeOutlineEdgeComponents[FlatEdgeIndex])
			{
				continue;
			}

			UStaticMeshComponent* EdgeComponent = SmokeOutlineEdgeComponents[FlatEdgeIndex];
			EdgeComponent->SetVisibility(bVisible, true);
			EdgeComponent->SetHiddenInGame(!bVisible);
			if (!bVisible)
			{
				continue;
			}

			switch (LocalEdgeIndex)
			{
			case 0: ConfigureOutlineEdge(EdgeComponent, FVector(MidX, MinY, BottomZ), FVector(SizeCm.X, T, T)); break;
			case 1: ConfigureOutlineEdge(EdgeComponent, FVector(MidX, MaxY, BottomZ), FVector(SizeCm.X, T, T)); break;
			case 2: ConfigureOutlineEdge(EdgeComponent, FVector(MinX, MidY, BottomZ), FVector(T, SizeCm.Y, T)); break;
			case 3: ConfigureOutlineEdge(EdgeComponent, FVector(MaxX, MidY, BottomZ), FVector(T, SizeCm.Y, T)); break;
			case 4: ConfigureOutlineEdge(EdgeComponent, FVector(MidX, MinY, BoxTopZ), FVector(SizeCm.X, T, T)); break;
			case 5: ConfigureOutlineEdge(EdgeComponent, FVector(MidX, MaxY, BoxTopZ), FVector(SizeCm.X, T, T)); break;
			case 6: ConfigureOutlineEdge(EdgeComponent, FVector(MinX, MidY, BoxTopZ), FVector(T, SizeCm.Y, T)); break;
			case 7: ConfigureOutlineEdge(EdgeComponent, FVector(MaxX, MidY, BoxTopZ), FVector(T, SizeCm.Y, T)); break;
			case 8: ConfigureOutlineEdge(EdgeComponent, FVector(MinX, MinY, MidZ), FVector(T, T, HeightCm)); break;
			case 9: ConfigureOutlineEdge(EdgeComponent, FVector(MinX, MaxY, MidZ), FVector(T, T, HeightCm)); break;
			case 10: ConfigureOutlineEdge(EdgeComponent, FVector(MaxX, MinY, MidZ), FVector(T, T, HeightCm)); break;
			case 11: ConfigureOutlineEdge(EdgeComponent, FVector(MaxX, MaxY, MidZ), FVector(T, T, HeightCm)); break;
			default: break;
			}
		}
	};

	ConfigureBoxOutline(0, bShowHotOutline, LayerZ, TopZ);
	ConfigureBoxOutline(SmokeOutlineEdgesPerBox, bShowCoolOutline, FloorZ, LayerZ);
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
