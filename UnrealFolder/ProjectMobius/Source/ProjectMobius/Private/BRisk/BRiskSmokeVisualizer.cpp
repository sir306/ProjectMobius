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
	// Activation/visibility gate on the 0-1 SmokeDensity proxy. Kept tiny on purpose:
	// SmokeDensity = 1-exp(-0.35*ULOD) ignores path length, so the old 0.02 gate
	// (ULOD ~= 0.058) is already ~47% obscuration across a 4 m room. For a well-mixed
	// B-Risk room (layer pinned at the floor, e.g. 3_RoomFire room 2) the smoke region
	// is the whole room from t=0, so that gate made it pop from invisible straight to
	// half-opaque ("nothing -> full room"). A near-zero gate lets the per-room
	// extinction ramp the opacity in smoothly from barely-visible. Rooms with a
	// descending layer are unaffected (they already looked gradual).
	constexpr float SmokeNiagaraActivationDensityThreshold = 0.001f;
	// Per room the outline is a CONSTANT zone box (full room, floor->ceiling, 12 edges,
	// always visible) plus a MOVING layer-interface rectangle (4 horizontal edges at the
	// current layer height). The zone box never changes; only the layer line tracks the
	// interface, so a well-mixed room (layer pinned near the floor) still shows its full
	// room outline with the layer line sitting low.
	constexpr int32 SmokeOutlineEdgesPerBox = 12;
	constexpr int32 SmokeOutlineLayerEdges = 4;
	constexpr int32 SmokeOutlineEdgesPerRoom = SmokeOutlineEdgesPerBox + SmokeOutlineLayerEdges;
	constexpr float SmokeOutlineThicknessCm = 2.0f;
	const TCHAR* HeterogeneousSmokeNiagaraSystemPath =
		TEXT("/Game/B-Risk/Niagara/NS_HeterogenousSmokeVol.NS_HeterogenousSmokeVol");
	// Colours follow Smokeview defaults so fire engineers see a familiar palette
	// (B-RISK delegates all visualisation to Smokeview; SR282 defines no colours of
	// its own). Constant zone box uses Smokeview's geometry-outline cyan (OUTLINECOLOR
	// 0 255 255 — also matches the cyan room edges in SR282 Fig.19). The moving
	// layer-interface line has no Smokeview equivalent (Smokeview shows the layer by
	// temperature-colouring the upper region), so it uses neutral white (FOREGROUNDCOLOR)
	// to avoid clashing with the magenta vents / orange fire / yellow sensors palette.
	const FLinearColor RoomZoneOutlineColor(0.0f, 1.0f, 1.0f, 1.0f);
	const FLinearColor LayerInterfaceOutlineColor(1.0f, 1.0f, 1.0f, 1.0f);

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

	void SetSmokeNiagaraUserParameters(UNiagaraComponent* NiagaraComponent, const FBRiskSmokeVisualState& SmokeState)
	{
		if (!NiagaraComponent)
		{
			return;
		}

		const float RoomSmoke = FMath::Clamp(SmokeState.RoomSmoke, 0.0f, 1.0f);
		const float UpperOpticalDensity = FMath::Max(SmokeState.UpperOpticalDensity, 0.0f);
		const float LowerOpticalDensity = FMath::Max(SmokeState.LowerOpticalDensity, 0.0f);
		const float UpperExtinctionPerCm = FMath::Max(SmokeState.UpperExtinctionPerCm, 0.0f);
		const float LowerExtinctionPerCm = FMath::Max(SmokeState.LowerExtinctionPerCm, 0.0f);
		const float SmokeDensity = FMath::Clamp(SmokeState.SmokeDensity, 0.0f, 1.0f);
		const float SmokeHeat = FMath::Clamp(SmokeState.SmokeHeat, 0.0f, 1.0f);
		const float LayerHeightWorldCm = SmokeState.LayerHeightWorldCm;
		const float LayerSoftnessCm = FMath::Max(SmokeState.LayerSoftnessCm, 0.0f);

		NiagaraComponent->SetVariableFloat(TEXT("User.RoomSmoke"), RoomSmoke);
		NiagaraComponent->SetVariableFloat(TEXT("User.UpperOpticalDensity"), UpperOpticalDensity);
		NiagaraComponent->SetVariableFloat(TEXT("User.LowerOpticalDensity"), LowerOpticalDensity);
		NiagaraComponent->SetVariableFloat(TEXT("User.UpperExtinctionPerCm"), UpperExtinctionPerCm);
		NiagaraComponent->SetVariableFloat(TEXT("User.LowerExtinctionPerCm"), LowerExtinctionPerCm);
		NiagaraComponent->SetVariableFloat(TEXT("User.SmokeDensity"), SmokeDensity);
		NiagaraComponent->SetVariableFloat(TEXT("User.SmokeHeat"), SmokeHeat);
		NiagaraComponent->SetVariableFloat(TEXT("User.LayerHeightWorldCm"), LayerHeightWorldCm);
		NiagaraComponent->SetVariableFloat(TEXT("User.LayerHeightWorld"), LayerHeightWorldCm);
		NiagaraComponent->SetVariableFloat(TEXT("User.LayerSoftnessCm"), LayerSoftnessCm);
	}

	void SetSmokeMaterialParameters(UMaterialInstanceDynamic* DynamicMaterial, const FBRiskSmokeVisualState& SmokeState)
	{
		if (!DynamicMaterial)
		{
			return;
		}

		const float RoomSmoke = FMath::Clamp(SmokeState.RoomSmoke, 0.0f, 1.0f);
		const float UpperOpticalDensity = FMath::Max(SmokeState.UpperOpticalDensity, 0.0f);
		const float LowerOpticalDensity = FMath::Max(SmokeState.LowerOpticalDensity, 0.0f);
		const float UpperExtinctionPerCm = FMath::Max(SmokeState.UpperExtinctionPerCm, 0.0f);
		const float LowerExtinctionPerCm = FMath::Max(SmokeState.LowerExtinctionPerCm, 0.0f);
		const float SmokeDensity = FMath::Clamp(SmokeState.SmokeDensity, 0.0f, 1.0f);
		const float SmokeHeat = FMath::Clamp(SmokeState.SmokeHeat, 0.0f, 1.0f);
		const float LayerHeightWorldCm = SmokeState.LayerHeightWorldCm;
		const float LayerSoftnessCm = FMath::Max(SmokeState.LayerSoftnessCm, 0.0f);

		DynamicMaterial->SetScalarParameterValue(TEXT("RoomSmoke"), RoomSmoke);
		DynamicMaterial->SetScalarParameterValue(TEXT("UpperOpticalDensity"), UpperOpticalDensity);
		DynamicMaterial->SetScalarParameterValue(TEXT("LowerOpticalDensity"), LowerOpticalDensity);
		DynamicMaterial->SetScalarParameterValue(TEXT("UpperExtinctionPerCm"), UpperExtinctionPerCm);
		DynamicMaterial->SetScalarParameterValue(TEXT("LowerExtinctionPerCm"), LowerExtinctionPerCm);
		DynamicMaterial->SetScalarParameterValue(TEXT("SmokeDensity"), SmokeDensity);
		DynamicMaterial->SetScalarParameterValue(TEXT("SmokeHeat"), SmokeHeat);
		DynamicMaterial->SetScalarParameterValue(TEXT("LayerHeightWorldCm"), LayerHeightWorldCm);
		DynamicMaterial->SetScalarParameterValue(TEXT("LayerHeightWorld"), LayerHeightWorldCm);
		DynamicMaterial->SetScalarParameterValue(TEXT("LayerSoftnessCm"), LayerSoftnessCm);
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
		HeterogeneousSmokeNiagaraSystemPath);
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
			HeterogeneousSmokeNiagaraSystemPath);
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

		FBRiskSmokeVisualState ClearSmokeState;
		ClearSmokeState.RoomSmoke = 1.0f;
		ClearSmokeState.LayerHeightWorldCm = OriginCm.Z + SizeCm.Z;
		ClearSmokeState.LayerSoftnessCm = 5.0f;
		LastSmokeStates[RoomIndex] = ClearSmokeState;

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
						? RoomZoneOutlineColor
						: LayerInterfaceOutlineColor;
					EdgeMaterial->SetVectorParameterValue(TEXT("Color"), EdgeColor);
					EdgeMaterial->SetVectorParameterValue(TEXT("BaseColor"), EdgeColor);
					EdgeComponent->SetMaterial(0, EdgeMaterial);
				}
			}

			const int32 FlatEdgeIndex = RoomIndex * SmokeOutlineEdgesPerRoom + EdgeIndex;
			SmokeOutlineEdgeComponents[FlatEdgeIndex] = EdgeComponent;
			SmokeOutlineEdgeMaterialInstances[FlatEdgeIndex] = EdgeMaterial;
		}

		if (SmokeNiagaraSystem)
		{
			UNiagaraComponent* NiagaraComponent = NewObject<UNiagaraComponent>(
				this,
				*FString::Printf(TEXT("BRiskSmokeNiagara_%d"), RoomIndex));
			NiagaraComponent->SetupAttachment(SceneRoot);
			NiagaraComponent->SetAsset(SmokeNiagaraSystem);
			NiagaraComponent->SetAutoActivate(false);
			NiagaraComponent->SetRelativeLocation(OriginCm + SizeCm * 0.5f);
			NiagaraComponent->SetMobility(EComponentMobility::Movable);
			NiagaraComponent->SetVariableVec3(TEXT("User.SmokeExtents"), SizeCm * 0.5f);
			SetSmokeNiagaraUserParameters(NiagaraComponent, ClearSmokeState);

			AddInstanceComponent(NiagaraComponent);
			NiagaraComponent->OnComponentCreated();
			NiagaraComponent->RegisterComponent();

			// NS_HeterogenousSmokeVol is a GPU sim (heterogeneous volume); GPU sims
			// cannot compute CPU-side dynamic bounds, so without an explicit fixed
			// bound the volume is culled/clipped against the asset's origin-anchored
			// bounds. That renders the room at the world origin but malforms every
			// offset room. Pin per-room LOCAL-space bounds (component sits at the room
			// centre, so the volume spans +/- half the room size) to override it.
			const FVector HalfExtentsCm = SizeCm * 0.5f;
			NiagaraComponent->SetSystemFixedBounds(FBox(-HalfExtentsCm, HalfExtentsCm));

			NiagaraComponent->DeactivateImmediate();

			SmokeNiagaraComponents[RoomIndex] = NiagaraComponent;
			++CreatedNiagaraCount;
		}
		else if (CubeMesh && SmokeMaterial)
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
				SetSmokeMaterialParameters(DynamicMaterial, ClearSmokeState);
				SmokeComponent->SetMaterial(0, DynamicMaterial);
			}

			SmokeVolumeComponents[RoomIndex] = SmokeComponent;
			SmokeMaterialInstances[RoomIndex] = DynamicMaterial;
			++CreatedVolumeCount;
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
		SetSmokeNiagaraUserParameters(NiagaraComponent, SmokeState);

		if (SmokeRoomSizesCm.IsValidIndex(RoomIndex))
		{
			NiagaraComponent->SetVariableVec3(TEXT("User.SmokeExtents"), SmokeRoomSizesCm[RoomIndex] * 0.5f);
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
		SetSmokeMaterialParameters(SmokeMaterialInstances[RoomIndex], SmokeState);
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

	const float RoomSmoke = FMath::Clamp(SmokeState.RoomSmoke, 0.0f, 1.0f);
	// Show the layer line only when smoke is actually present (same gate as the visible
	// volume), AND the interface sits meaningfully between floor and ceiling. Without the
	// smoke-presence check a well-mixed room (layer pinned low from t=0, e.g. room 2)
	// would show the layer line before any smoke develops. The zone box is always shown.
	const bool bShowLayerLine = ShouldRunSmokeNiagara(SmokeState)
		&& RoomSmoke > 0.005f && RoomSmoke < 0.995f;

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

	// Constant zone outline: the full room box (floor -> ceiling), always visible.
	ConfigureBoxOutline(0, true, FloorZ, TopZ);

	// Moving layer-interface rectangle: 4 horizontal edges at the current layer height.
	const FVector LayerEdgeCenters[SmokeOutlineLayerEdges] = {
		FVector(MidX, MinY, LayerZ),
		FVector(MidX, MaxY, LayerZ),
		FVector(MinX, MidY, LayerZ),
		FVector(MaxX, MidY, LayerZ),
	};
	const FVector LayerEdgeSizes[SmokeOutlineLayerEdges] = {
		FVector(SizeCm.X, T, T),
		FVector(SizeCm.X, T, T),
		FVector(T, SizeCm.Y, T),
		FVector(T, SizeCm.Y, T),
	};
	for (int32 LayerEdgeIndex = 0; LayerEdgeIndex < SmokeOutlineLayerEdges; ++LayerEdgeIndex)
	{
		const int32 FlatEdgeIndex = RoomIndex * SmokeOutlineEdgesPerRoom + SmokeOutlineEdgesPerBox + LayerEdgeIndex;
		if (!SmokeOutlineEdgeComponents.IsValidIndex(FlatEdgeIndex) || !SmokeOutlineEdgeComponents[FlatEdgeIndex])
		{
			continue;
		}

		UStaticMeshComponent* EdgeComponent = SmokeOutlineEdgeComponents[FlatEdgeIndex];
		EdgeComponent->SetVisibility(bShowLayerLine, true);
		EdgeComponent->SetHiddenInGame(!bShowLayerLine);
		if (bShowLayerLine)
		{
			ConfigureOutlineEdge(EdgeComponent, LayerEdgeCenters[LayerEdgeIndex], LayerEdgeSizes[LayerEdgeIndex]);
		}
	}
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
