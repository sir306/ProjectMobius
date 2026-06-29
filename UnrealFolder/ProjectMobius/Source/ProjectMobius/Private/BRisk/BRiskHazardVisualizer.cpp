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

		// World position in the B-Risk frame (metres). The X<->Y swap into Unreal space is
		// applied by the caller via BRiskCoord::ToUnreal, so there is no per-axis flip here.
		// (Previously this mirrored X to patch a coordinate mismatch; that is now handled
		// correctly and consistently for all geometry by BRiskCoord.)
		return Room->Origin + RoomLocalLocationM;
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

	// Decompose a vent opening box (CenterCm/SizeCm, thin in the wall-normal axis) into the
	// 4 thin "wire" edges of its rectangular perimeter, so vents read as a wireframe outline
	// matching the room zone outlines instead of a solid slab. Outputs per-edge centre+size.
	void BuildVentOutlineEdges(
		const FVector& CenterCm,
		const FVector& SizeCm,
		float WireThicknessCm,
		TArray<FVector>& OutCenters,
		TArray<FVector>& OutSizes)
	{
		// Wall normal = thinnest axis (the slab thickness).
		int32 NormalAxis = 0;
		if (SizeCm[1] < SizeCm[NormalAxis]) { NormalAxis = 1; }
		if (SizeCm[2] < SizeCm[NormalAxis]) { NormalAxis = 2; }
		const int32 UAxis = (NormalAxis + 1) % 3;
		const int32 VAxis = (NormalAxis + 2) % 3;

		const double HalfU = SizeCm[UAxis] * 0.5;
		const double HalfV = SizeCm[VAxis] * 0.5;
		const double NormalCm = FMath::Min(static_cast<double>(WireThicknessCm), static_cast<double>(SizeCm[NormalAxis]));

		auto AddEdge = [&](double UOffset, double VOffset, bool bSpanU)
		{
			FVector C = CenterCm;
			C[UAxis] = CenterCm[UAxis] + UOffset;
			C[VAxis] = CenterCm[VAxis] + VOffset;
			FVector S = FVector::ZeroVector;
			S[NormalAxis] = NormalCm;
			S[UAxis] = bSpanU ? SizeCm[UAxis] : WireThicknessCm;
			S[VAxis] = bSpanU ? WireThicknessCm : SizeCm[VAxis];
			OutCenters.Add(C);
			OutSizes.Add(S);
		};

		AddEdge(0.0, +HalfV, true);  // top edge (spans U)
		AddEdge(0.0, -HalfV, true);  // bottom edge
		AddEdge(+HalfU, 0.0, false); // one side (spans V)
		AddEdge(-HalfU, 0.0, false); // other side
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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		CubeMesh = CubeMeshFinder.Object;
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

bool ABRiskHazardVisualizer::ComputeVentSlab(
	const FBRiskVentGeometry& Vent,
	const FBRiskRoomGeometry* FromRoom,
	const FBRiskRoomGeometry* ToRoom,
	float Scale,
	float ThicknessCm,
	FVector& OutCenterCm,
	FVector& OutSizeCm)
{
	if (!FromRoom || Vent.Width <= 0.0 || Vent.Height <= 0.0 || Scale <= 0.0f)
	{
		return false;
	}

	const FBox FromBoxCm = BRiskCoord::ToUnrealBox(FromRoom->Origin, FromRoom->Size, Scale);
	const FVector MinCm = FromBoxCm.Min;
	const FVector MaxCm = FromBoxCm.Max;

	const double Sill = FMath::Clamp(
		(FromRoom->Origin.Z + Vent.SillHeight) * Scale,
		static_cast<double>(MinCm.Z), static_cast<double>(MaxCm.Z));
	const double Head = FMath::Clamp(
		(FromRoom->Origin.Z + Vent.SillHeight + Vent.Height) * Scale,
		static_cast<double>(MinCm.Z), static_cast<double>(MaxCm.Z));
	if (Head <= Sill)
	{
		return false;
	}
	const double CenterZ = (Sill + Head) * 0.5;
	const double HeightCm = Head - Sill;
	const double WidthCm = Vent.Width * Scale;
	const double OffsetCm = Vent.Offset * Scale;

	enum EVentWall { WallNegX, WallPosX, WallNegY, WallPosY };
	EVentWall Wall = WallNegY;
	bool bResolved = false;

	// Preferred: derive the shared wall from room adjacency. This is unambiguous
	// from the parsed room boxes and matches Smokeview regardless of how B-Risk
	// numbers vent faces.
	if (ToRoom)
	{
		const FBox ToBoxCm = BRiskCoord::ToUnrealBox(ToRoom->Origin, ToRoom->Size, Scale);
		const FVector ToMinCm = ToBoxCm.Min;
		const FVector ToMaxCm = ToBoxCm.Max;
		constexpr double AdjacencyEpsCm = 1.0;
		if (FMath::Abs(MaxCm.X - ToMinCm.X) < AdjacencyEpsCm) { Wall = WallPosX; bResolved = true; }
		else if (FMath::Abs(MinCm.X - ToMaxCm.X) < AdjacencyEpsCm) { Wall = WallNegX; bResolved = true; }
		else if (FMath::Abs(MaxCm.Y - ToMinCm.Y) < AdjacencyEpsCm) { Wall = WallPosY; bResolved = true; }
		else if (FMath::Abs(MinCm.Y - ToMaxCm.Y) < AdjacencyEpsCm) { Wall = WallNegY; bResolved = true; }
	}

	// Fallback for exterior vents (ToRoom not a real room). The .smv/CFAST face id is in
	// B-Risk axes: 1=-Y(front) 2=+X(right) 3=+Y(rear) 4=-X(left). Map to Unreal walls
	// under the X<->Y swap (BRiskCoord): B-Risk +/-X -> UE +/-Y, B-Risk +/-Y -> UE +/-X.
	if (!bResolved)
	{
		switch (Vent.Face)
		{
		case 1: Wall = WallNegX; break; // B-Risk -Y -> UE -X
		case 2: Wall = WallPosY; break; // B-Risk +X -> UE +Y
		case 3: Wall = WallPosX; break; // B-Risk +Y -> UE +X
		case 4: Wall = WallNegY; break; // B-Risk -X -> UE -Y
		default: return false;
		}
	}

	if (Wall == WallNegX || Wall == WallPosX)
	{
		// Wall lies in the YZ plane; the opening runs along Y.
		const double SpanMin = MinCm.Y;
		const double SpanMax = MaxCm.Y;
		const double OpenStart = FMath::Clamp(SpanMin + OffsetCm, SpanMin, SpanMax);
		const double OpenEnd = FMath::Clamp(OpenStart + WidthCm, SpanMin, SpanMax);
		if (OpenEnd <= OpenStart)
		{
			return false;
		}
		const double WallX = (Wall == WallPosX) ? MaxCm.X : MinCm.X;
		OutCenterCm = FVector(WallX, (OpenStart + OpenEnd) * 0.5, CenterZ);
		OutSizeCm = FVector(ThicknessCm, OpenEnd - OpenStart, HeightCm);
		return true;
	}

	// Wall lies in the XZ plane; the opening runs along X.
	const double SpanMin = MinCm.X;
	const double SpanMax = MaxCm.X;
	const double OpenStart = FMath::Clamp(SpanMin + OffsetCm, SpanMin, SpanMax);
	const double OpenEnd = FMath::Clamp(OpenStart + WidthCm, SpanMin, SpanMax);
	if (OpenEnd <= OpenStart)
	{
		return false;
	}
	const double WallY = (Wall == WallPosY) ? MaxCm.Y : MinCm.Y;
	OutCenterCm = FVector((OpenStart + OpenEnd) * 0.5, WallY, CenterZ);
	OutSizeCm = FVector(OpenEnd - OpenStart, ThicknessCm, HeightCm);
	return true;
}

bool ABRiskHazardVisualizer::ConfigureFromScenario(
	const TArray<FBRiskRoomGeometry>& Rooms,
	const TArray<FBRiskFireGeometry>& Fires,
	const TArray<FBRiskSprinklerGeometry>& Sprinklers,
	const TArray<FBRiskVentGeometry>& Vents,
	float Scale)
{
	ClearHazardVisuals();

	if (!ConeMesh)
	{
		ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	}
	if (!CubeMesh)
	{
		CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
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
		FireBaseLocationsCm[FireIndex] = BRiskCoord::ToUnreal(WorldLocationM, Scale);

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

		// Smokeview FIRECOLOR default (255,128,0). Fallback cone only — hidden whenever
		// the NS_SimpleFire Niagara system loads, so rarely seen, but kept consistent.
		UMaterialInstanceDynamic* FireMaterial = MakeColoredMaterial(
			BasicShapeMaterial,
			this,
			FLinearColor(1.0f, 0.502f, 0.0f, 1.0f));
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
			BRiskCoord::ToUnreal(TransformHazardRoomLocalToWorldM(Room, Sprinkler.Location), Scale);
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
		// Cone apex at the sprinkler head (top), base spreading toward the floor — the
		// shape of downward water spray. (Was FRotator(180,...), which put the apex on the
		// floor i.e. upside down.) Positioning in SetSimulationTime centres it half a spray
		// height below the head, leaving the apex at the head.
		SprinklerCone->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
		SprinklerCone->SetVisibility(false, true);
		SprinklerCone->SetHiddenInGame(true);

		AddInstanceComponent(SprinklerCone);
		SprinklerCone->OnComponentCreated();
		SprinklerCone->RegisterComponent();

		// Smokeview SPRINKONCOLOR default (0,1,0) — green = activated. The cone only
		// appears at sprinkler activation, so the "on" colour is the correct state.
		UMaterialInstanceDynamic* SprinklerMaterial = MakeColoredMaterial(
			BasicShapeMaterial,
			this,
			FLinearColor(0.0f, 1.0f, 0.0f, 1.0f));
		if (SprinklerMaterial)
		{
			SprinklerCone->SetMaterial(0, SprinklerMaterial);
		}

		SprinklerConeComponents[SprinklerIndex] = SprinklerCone;
		SprinklerMaterials[SprinklerIndex] = SprinklerMaterial;
	}

	// Vents/openings: one thin slab per VENTGEOM, placed on its FromRoom wall.
	// Static markers (no time dependence) so they stay visible for the whole run.
	constexpr float VentSlabThicknessCm = 8.0f;
	VentComponents.Reserve(Vents.Num());
	VentMaterials.Reserve(Vents.Num());
	int32 CreatedVentCount = 0;

	for (int32 VentIndex = 0; VentIndex < Vents.Num(); ++VentIndex)
	{
		const FBRiskVentGeometry& Vent = Vents[VentIndex];
		const FBRiskRoomGeometry* FromRoom = FindRoomById(Rooms, Vent.FromRoomId);
		const FBRiskRoomGeometry* ToRoom = FindRoomById(Rooms, Vent.ToRoomId);

		FVector CenterCm = FVector::ZeroVector;
		FVector SizeCm = FVector::ZeroVector;
		if (!CubeMesh || !ComputeVentSlab(Vent, FromRoom, ToRoom, Scale, VentSlabThicknessCm, CenterCm, SizeCm))
		{
			UE_LOG(LogBRiskHazardVisualizer, Warning,
				TEXT("Skipping B-Risk vent %d (fromRoom=%d toRoom=%d face=%d width=%g height=%g): no FromRoom geometry or zero opening."),
				VentIndex, Vent.FromRoomId, Vent.ToRoomId, Vent.Face, Vent.Width, Vent.Height);
			continue;
		}

		// Render the opening as a 4-edge wireframe rectangle to match the room zone outline
		// style, rather than a solid slab.
		constexpr float VentWireThicknessCm = 6.0f;
		TArray<FVector> EdgeCenters;
		TArray<FVector> EdgeSizes;
		BuildVentOutlineEdges(CenterCm, SizeCm, VentWireThicknessCm, EdgeCenters, EdgeSizes);

		for (int32 EdgeIndex = 0; EdgeIndex < EdgeCenters.Num(); ++EdgeIndex)
		{
			UStaticMeshComponent* VentEdge = NewObject<UStaticMeshComponent>(
				this,
				*FString::Printf(TEXT("BRiskVent_%d_Edge_%d"), VentIndex, EdgeIndex));
			VentEdge->SetupAttachment(SceneRoot);
			VentEdge->SetStaticMesh(CubeMesh);
			VentEdge->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			VentEdge->SetCastShadow(false);
			VentEdge->SetReceivesDecals(false);
			VentEdge->SetMobility(EComponentMobility::Movable);
			VentEdge->SetRelativeLocation(EdgeCenters[EdgeIndex]);
			VentEdge->SetRelativeScale3D(EdgeSizes[EdgeIndex] / 100.0f);
			VentEdge->SetVisibility(true, true);
			VentEdge->SetHiddenInGame(false);

			AddInstanceComponent(VentEdge);
			VentEdge->OnComponentCreated();
			VentEdge->RegisterComponent();

			// Smokeview VENTCOLOR default (1,0,1) — magenta is the standard vent/door colour
			// fire engineers recognise (SR282 Fig.19 shows magenta vent outlines in Smokeview).
			UMaterialInstanceDynamic* VentMaterial = MakeColoredMaterial(
				BasicShapeMaterial,
				this,
				FLinearColor(1.0f, 0.0f, 1.0f, 1.0f));
			if (VentMaterial)
			{
				VentEdge->SetMaterial(0, VentMaterial);
			}

			VentComponents.Add(VentEdge);
			VentMaterials.Add(VentMaterial);
		}
		++CreatedVentCount;
	}

	UE_LOG(LogBRiskHazardVisualizer, Log,
		TEXT("Configured B-Risk hazard visualizer: fires=%d sprinklers=%d vents=%d/%d scale=%g"),
		Fires.Num(),
		Sprinklers.Num(),
		CreatedVentCount,
		Vents.Num(),
		Scale);

	return Fires.Num() > 0 || Sprinklers.Num() > 0 || CreatedVentCount > 0;
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

	for (UStaticMeshComponent* VentComponent : VentComponents)
	{
		if (VentComponent)
		{
			VentComponent->DestroyComponent();
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
	VentComponents.Reset();
	VentMaterials.Reset();
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
	for (const UStaticMeshComponent* VentComponent : VentComponents)
	{
		if (VentComponent)
		{
			++Count;
		}
	}
	return Count;
}
