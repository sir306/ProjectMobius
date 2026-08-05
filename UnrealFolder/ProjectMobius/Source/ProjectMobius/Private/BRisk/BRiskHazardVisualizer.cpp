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
#include "Math/RotationMatrix.h"

DEFINE_LOG_CATEGORY_STATIC(LogBRiskHazardVisualizer, Log, All);

namespace
{
	constexpr float FireActivationHrrKw = 1.0f;
	constexpr float ConeMeshRadiusCm = 50.0f;
	constexpr float ConeMeshHeightCm = 100.0f;
	constexpr int32 VentFlowBandsPerVent = 36; // height bands tracing the vent-flow velocity profile
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

	// Map a gas temperature to Smokeview's standard rainbow colourbar
	// (blue -> cyan -> green -> yellow -> red) over [MinC, MaxC]. The range is the scenario's
	// actual temperature span (auto-scaled like Smokeview) so the hottest flows reach red.
	FLinearColor TemperatureToColor(double TempC, float MinC, float MaxC)
	{
		const float Range = FMath::Max(MaxC - MinC, 1.0f);
		const float U = FMath::Clamp(static_cast<float>((TempC - MinC) / Range), 0.0f, 1.0f);
		// Five equal stops: blue, cyan, green, yellow, red.
		const FLinearColor Stops[5] = {
			FLinearColor(0.0f, 0.0f, 1.0f, 1.0f),
			FLinearColor(0.0f, 1.0f, 1.0f, 1.0f),
			FLinearColor(0.0f, 1.0f, 0.0f, 1.0f),
			FLinearColor(1.0f, 1.0f, 0.0f, 1.0f),
			FLinearColor(1.0f, 0.0f, 0.0f, 1.0f)
		};
		const float Scaled = U * 4.0f;
		const int32 Lo = FMath::Clamp(FMath::FloorToInt(Scaled), 0, 3);
		const float Frac = Scaled - static_cast<float>(Lo);
		return FMath::Lerp(Stops[Lo], Stops[Lo + 1], Frac);
	}

	/**
	 * World position in the B-Risk frame (metres). The conversion into Unreal space is applied by
	 * the caller via BRiskCoord::FootprintToUnreal, so there is no per-axis flip here — but the
	 * room-local offset itself may need transposing, because B-Risk measures it against the
	 * equivalent rectangle. See BRiskCoord::RoomLocalToWorld.
	 */
	FVector TransformHazardRoomLocalToWorldM(
		const FBRiskRoomGeometry* Room,
		const FVector& RoomLocalLocationM,
		BRiskCoord::ERoomLocalAxes& OutAxes)
	{
		OutAxes = BRiskCoord::ERoomLocalAxes::Unverified;
		if (!Room)
		{
			return RoomLocalLocationM;
		}

		return BRiskCoord::RoomLocalToWorld(*Room, RoomLocalLocationM, OutAxes);
	}

	/** One-line explanation of why a marker may be in the wrong spot inside its room. */
	void WarnIfRoomLocalAxesUnmappable(
		const TCHAR* MarkerKind,
		int32 MarkerId,
		const FBRiskRoomGeometry* Room,
		BRiskCoord::ERoomLocalAxes Axes)
	{
		if (Axes != BRiskCoord::ERoomLocalAxes::Unmappable || !Room)
		{
			return;
		}

		UE_LOG(LogBRiskHazardVisualizer, Warning,
			TEXT("B-Risk %s %d sits in room %d ('%s'), whose footprint is not a rectangle, so its ")
			TEXT("equivalent rectangle (%g x %g m) cannot be lined up with the real plan. The ")
			TEXT("in-room offset is used as authored and may be in the wrong part of the room."),
			MarkerKind, MarkerId, Room->RoomId, *Room->Label, Room->Size.X, Room->Size.Y);
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
		int32 NormalAxis,
		float WireThicknessCm,
		TArray<FVector>& OutCenters,
		TArray<FVector>& OutSizes)
	{
		// The wall normal is PASSED IN, not inferred from the box.
		//
		// It used to be taken as the thinnest axis, which is true only while the opening is wider
		// than the slab is thick. Every leakage vent breaks that: a 10 mm gap round a door leaf is
		// (1, 8, 213) cm, so the 1 cm OPENING is thinner than the 8 cm slab, the width axis was
		// mistaken for the wall normal, and the outline came out in the wrong plane - an 8 x 213 cm
		// rectangle lying across the wall instead of a sliver in it. Visually that turned each
		// hairline leakage path into a second door-sized ghost overlapping its parent door, which is
		// exactly how it looked on screen. 18 of the 34 openings in the 12-room test are that shape.
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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMeshFinder(
		TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMeshFinder.Succeeded())
	{
		PlaneMesh = PlaneMeshFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicShapeMaterialFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicShapeMaterialFinder.Succeeded())
	{
		BasicShapeMaterial = BasicShapeMaterialFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> VentFlowMaterialFinder(
		TEXT("/Game/B-Risk/Materials/M_BRiskVentFlow.M_BRiskVentFlow"));
	if (VentFlowMaterialFinder.Succeeded())
	{
		VentFlowMaterial = VentFlowMaterialFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SimpleFireNiagaraSystemFinder(
		TEXT("/Game/B-Risk/Niagara/NS_SimpleFire.NS_SimpleFire"));
	if (SimpleFireNiagaraSystemFinder.Succeeded())
	{
		SimpleFireNiagaraSystem = SimpleFireNiagaraSystemFinder.Object;
	}
}

FLinearColor ABRiskHazardVisualizer::VentColourForKind(EBRiskVentKind Kind) const
{
	switch (Kind)
	{
	case EBRiskVentKind::Door:    return DoorVentColour;
	case EBRiskVentKind::Window:  return WindowVentColour;
	case EBRiskVentKind::Leakage: return LeakageVentColour;
	default:                      return UnclassifiedVentColour;
	}
}

bool ABRiskHazardVisualizer::ComputeVentSlab(
	const FBRiskVentGeometry& Vent,
	const FBRiskRoomGeometry* FromRoom,
	const FBRiskRoomGeometry* ToRoom,
	float Scale,
	BRiskCoord::ERoomFrame Frame,
	float ThicknessCm,
	FVector& OutCenterCm,
	FVector& OutSizeCm,
	int32* OutNormalAxis)
{
	if (!FromRoom || Vent.Width <= 0.0 || Vent.Height <= 0.0 || Scale <= 0.0f)
	{
		return false;
	}

	// Which axis the wall's normal runs along. Reported rather than left for the caller to infer
	// from OutSizeCm - "the thinnest axis" is only the normal while the opening is wider than the
	// slab is thick, which no leakage vent is.
	const auto SetNormalAxis = [OutNormalAxis](int32 Axis)
	{
		if (OutNormalAxis)
		{
			*OutNormalAxis = Axis;
		}
	};

	// Same source of truth as the smoke volumes and egress bounds, so a vent cannot end up in a
	// different frame from the room it is cut into. Under SmokeviewSwap this returns exactly what
	// ToUnrealBox used to.
	const BRiskCoord::FRoomFootprintCm FromFootprint = BRiskCoord::MakeRoomFootprint(*FromRoom, Scale, Frame);
	const FBox FromBoxCm = FromFootprint.Bounds;
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
	// The TRUE opening when the add-in gave us one. Vent.Width is what B-Risk simulated, commonly
	// half the real leaf, so drawing it would show every door at half size.
	const double WidthCm = (Vent.PhysicalWidth > 0.0 ? Vent.PhysicalWidth : Vent.Width) * Scale;
	const double OffsetCm = Vent.Offset * Scale;

	// --- Real placement, when Zones-data.json openings[] supplied a centre ----------------------
	//
	// Everything below this block derives a wall from face/offset against the room's BOUNDING BOX.
	// That is a fallback for scenarios with no openings[], and it is wrong in three separate ways
	// for a non-rectangular room: the bbox walls are not the room's walls (Corridor 15's bbox is
	// 17.8 x 6.2 m around a 1 m-wide L), every .smv offset is 0 so vents stack, and face does not
	// identify a wall. A centre replaces all three at once.
	//
	// The centre is used VERBATIM. It sits on the wall centreline, about half a wall thickness
	// outside the room's footprint polygon, and projecting it onto the polygon would be wrong: for a
	// shared wall the centreline is the only point both rooms agree on. The polygon is consulted
	// solely to read off which way the opening runs.
	if (Vent.bHasPlacement)
	{
		const FVector CentreCm = BRiskCoord::FootprintToUnreal(Vent.CentreMetres, Scale);
		const FVector2D CentrePlan(CentreCm.X, CentreCm.Y);
		const TArray<FVector2D>& Ring = FromFootprint.Polygon;

		// No polygon means no wall to take an axis from - the room is an equivalent rectangle whose
		// orientation is already unreliable, so guessing an axis would place the opening confidently
		// wrong. Fall through to the legacy path rather than invent one.
		if (Ring.Num() >= 3)
		{
			// Nearest edge wins, with the full opening width having to fit as the tie-break. Ties are
			// real: a vent in a room corner is equidistant from two edges (measured: two of the 34
			// openings in the 12-room test sit exactly on a corner at 0.100 m from both). Without the
			// fit test the winner would be whichever edge came first in winding order.
			// Distances, not squared distances. A tolerance on squared centimetres is not a
			// tolerance on centimetres - at the measured 10 cm stand-off, two candidate edges a
			// visible 0.5 mm apart differ by ~1 cm^2, so a squared comparison would sort near-ties
			// by an amount nobody chose. TieToleranceCm is a deliberate 0.1 mm.
			constexpr double TieToleranceCm = 0.01;

			int32 BestEdge = INDEX_NONE;
			double BestDistanceCm = TNumericLimits<double>::Max();
			bool bBestFits = false;

			for (int32 EdgeIndex = 0; EdgeIndex < Ring.Num(); ++EdgeIndex)
			{
				const FVector2D& A = Ring[EdgeIndex];
				const FVector2D& B = Ring[(EdgeIndex + 1) % Ring.Num()];
				const FVector2D Along = B - A;
				const double LengthSq = Along.SizeSquared();
				if (LengthSq <= 0.0)
				{
					continue;
				}

				const double T = FMath::Clamp(FVector2D::DotProduct(CentrePlan - A, Along) / LengthSq, 0.0, 1.0);
				const FVector2D Closest = A + Along * T;
				const double DistanceCm = FVector2D::Distance(CentrePlan, Closest);

				const double Length = FMath::Sqrt(LengthSq);
				const double AlongCm = T * Length;
				const bool bFits = (AlongCm - WidthCm * 0.5 >= -TieToleranceCm)
					&& (AlongCm + WidthCm * 0.5 <= Length + TieToleranceCm);

				// Strictly nearer always wins; an equal-distance edge only takes the place of the
				// incumbent by being one the opening actually fits on.
				const bool bNearer = DistanceCm < BestDistanceCm - TieToleranceCm;
				const bool bTiedAndBetter = FMath::Abs(DistanceCm - BestDistanceCm) <= TieToleranceCm
					&& bFits && !bBestFits;

				if (BestEdge == INDEX_NONE || bNearer || bTiedAndBetter)
				{
					BestEdge = EdgeIndex;
					BestDistanceCm = DistanceCm;
					bBestFits = bFits;
				}
			}

			if (BestEdge != INDEX_NONE)
			{
				const FVector2D EdgeDirection = Ring[(BestEdge + 1) % Ring.Num()] - Ring[BestEdge];
				const bool bRunsAlongX = FMath::Abs(EdgeDirection.X) >= FMath::Abs(EdgeDirection.Y);

				OutCenterCm = FVector(CentrePlan.X, CentrePlan.Y, CenterZ);
				OutSizeCm = bRunsAlongX
					? FVector(WidthCm, ThicknessCm, HeightCm)
					: FVector(ThicknessCm, WidthCm, HeightCm);
				SetNormalAxis(bRunsAlongX ? 1 : 0);
				return true;
			}
		}
	}

	enum EVentWall { WallNegX, WallPosX, WallNegY, WallPosY };
	EVentWall Wall = WallNegY;
	bool bResolved = false;

	// Preferred: derive the shared wall from room adjacency. This is unambiguous
	// from the parsed room boxes and matches Smokeview regardless of how B-Risk
	// numbers vent faces.
	if (ToRoom)
	{
		const FBox ToBoxCm = BRiskCoord::MakeRoomFootprint(*ToRoom, Scale, Frame).Bounds;
		const FVector ToMinCm = ToBoxCm.Min;
		const FVector ToMaxCm = ToBoxCm.Max;
		constexpr double AdjacencyEpsCm = 1.0;
		if (FMath::Abs(MaxCm.X - ToMinCm.X) < AdjacencyEpsCm) { Wall = WallPosX; bResolved = true; }
		else if (FMath::Abs(MinCm.X - ToMaxCm.X) < AdjacencyEpsCm) { Wall = WallNegX; bResolved = true; }
		else if (FMath::Abs(MaxCm.Y - ToMinCm.Y) < AdjacencyEpsCm) { Wall = WallPosY; bResolved = true; }
		else if (FMath::Abs(MinCm.Y - ToMaxCm.Y) < AdjacencyEpsCm) { Wall = WallNegY; bResolved = true; }
	}

	// Fallback for exterior vents (ToRoom not a real room). The .smv/CFAST face id is in
	// B-Risk axes: 1=-Y(front) 2=+X(right) 3=+Y(rear) 4=-X(left), and which Unreal wall that
	// lands on depends on the scenario frame.
	if (!bResolved)
	{
		if (Frame == BRiskCoord::ERoomFrame::Revit)
		{
			// B-Risk +/-X -> UE +/-X unchanged; B-Risk +/-Y -> UE -/+Y (negated).
			switch (Vent.Face)
			{
			case 1: Wall = WallPosY; break; // B-Risk -Y -> UE +Y
			case 2: Wall = WallPosX; break; // B-Risk +X -> UE +X
			case 3: Wall = WallNegY; break; // B-Risk +Y -> UE -Y
			case 4: Wall = WallNegX; break; // B-Risk -X -> UE -X
			default: return false;
			}
		}
		else
		{
			// Legacy X<->Y swap: B-Risk +/-X -> UE +/-Y, B-Risk +/-Y -> UE +/-X.
			switch (Vent.Face)
			{
			case 1: Wall = WallNegX; break; // B-Risk -Y -> UE -X
			case 2: Wall = WallPosY; break; // B-Risk +X -> UE +Y
			case 3: Wall = WallPosX; break; // B-Risk +Y -> UE +X
			case 4: Wall = WallNegY; break; // B-Risk -X -> UE -Y
			default: return false;
			}
		}
	}

	if (Wall == WallNegX || Wall == WallPosX)
	{
		// Wall lies in the YZ plane; the opening runs along Y - the one axis the Revit mapping
		// reverses. B-Risk measures Offset from its own -Y end of the wall, which is UE MAX Y once
		// Y is negated, so the opening has to be laid out downwards from the maximum. Getting this
		// wrong mirrors every vent about its wall's midpoint, which looks plausible on a centred
		// opening and obviously wrong on an off-centre one.
		const double SpanMin = MinCm.Y;
		const double SpanMax = MaxCm.Y;
		double OpenStart = 0.0;
		double OpenEnd = 0.0;
		if (Frame == BRiskCoord::ERoomFrame::Revit)
		{
			OpenEnd = FMath::Clamp(SpanMax - OffsetCm, SpanMin, SpanMax);
			OpenStart = FMath::Clamp(OpenEnd - WidthCm, SpanMin, SpanMax);
		}
		else
		{
			OpenStart = FMath::Clamp(SpanMin + OffsetCm, SpanMin, SpanMax);
			OpenEnd = FMath::Clamp(OpenStart + WidthCm, SpanMin, SpanMax);
		}
		if (OpenEnd <= OpenStart)
		{
			return false;
		}
		const double WallX = (Wall == WallPosX) ? MaxCm.X : MinCm.X;
		OutCenterCm = FVector(WallX, (OpenStart + OpenEnd) * 0.5, CenterZ);
		OutSizeCm = FVector(ThicknessCm, OpenEnd - OpenStart, HeightCm);
		SetNormalAxis(0);
		return true;
	}

	// Wall lies in the XZ plane; the opening runs along X.
	//
	// Deliberately NOT frame-branched, unlike the YZ case above. Both mappings reach UE X through
	// an order-preserving step - Revit takes B-Risk X straight through, and the legacy swap maps
	// B-Risk +Y to UE +X - so measuring the offset from the minimum is correct either way.
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
	SetNormalAxis(1);
	return true;
}

bool ABRiskHazardVisualizer::ConfigureFromScenario(
	const TArray<FBRiskRoomGeometry>& Rooms,
	const TArray<FBRiskFireGeometry>& Fires,
	const TArray<FBRiskSprinklerGeometry>& Sprinklers,
	const TArray<FBRiskVentGeometry>& Vents,
	float Scale,
	BRiskCoord::ERoomFrame Frame)
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
		BRiskCoord::ERoomLocalAxes FireAxes = BRiskCoord::ERoomLocalAxes::Unverified;
		const FVector WorldLocationM = TransformHazardRoomLocalToWorldM(Room, Fire.Location, FireAxes);
		WarnIfRoomLocalAxesUnmappable(TEXT("fire"), FireIndex, Room, FireAxes);
		// Same frame as MakeRoomFootprint used for this scenario, so the fire lands inside its own
		// room's smoke volume in both the Revit and legacy Smokeview cases.
		FireBaseLocationsCm[FireIndex] = BRiskCoord::WorldToUnreal(WorldLocationM, Scale, Frame);

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
		// Scenario frame, matching MakeRoomFootprint - see the fire cone above.
		BRiskCoord::ERoomLocalAxes SprinklerAxes = BRiskCoord::ERoomLocalAxes::Unverified;
		const FVector SprinklerWorldM =
			TransformHazardRoomLocalToWorldM(Room, Sprinkler.Location, SprinklerAxes);
		WarnIfRoomLocalAxesUnmappable(TEXT("sprinkler"), Sprinkler.SprinklerId, Room, SprinklerAxes);
		SprinklerHeadLocationsCm[SprinklerIndex] = BRiskCoord::WorldToUnreal(SprinklerWorldM, Scale, Frame);
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
	// Per-vent flow indicators are index-aligned with Vents (SetVentFlows indexes by vent).
	VentFlowGeometry.SetNum(Vents.Num());
	VentFlowBandQuads.SetNum(Vents.Num() * VentFlowBandsPerVent);
	VentFlowBandMaterials.SetNum(Vents.Num() * VentFlowBandsPerVent);
	int32 CreatedVentCount = 0;

	for (int32 VentIndex = 0; VentIndex < Vents.Num(); ++VentIndex)
	{
		const FBRiskVentGeometry& Vent = Vents[VentIndex];
		const FBRiskRoomGeometry* FromRoom = FindRoomById(Rooms, Vent.FromRoomId);
		const FBRiskRoomGeometry* ToRoom = FindRoomById(Rooms, Vent.ToRoomId);

		FVector CenterCm = FVector::ZeroVector;
		FVector SizeCm = FVector::ZeroVector;
		int32 NormalAxis = 1;
		if (!CubeMesh || !ComputeVentSlab(
			Vent, FromRoom, ToRoom, Scale, Frame, VentSlabThicknessCm, CenterCm, SizeCm, &NormalAxis))
		{
			// Says which of the causes it was. The old text blamed "no FromRoom geometry or zero
			// opening" for every case, including the commonest one by far - a face id outside 1-4,
			// which real .smv files emit constantly (8 of 34 in the 12-room test) and which this
			// function rejects outright.
			const TCHAR* Reason =
				!CubeMesh                          ? TEXT("no cube mesh") :
				!FromRoom                          ? TEXT("no FromRoom geometry") :
				(Vent.Width <= 0.0 || Vent.Height <= 0.0) ? TEXT("zero-size opening") :
				(!Vent.bHasPlacement && (Vent.Face < 1 || Vent.Face > 4))
					? TEXT("unhandled .smv face id and no openings[] centre to place it from")
					: TEXT("degenerate after clamping to the room bounds");

			UE_LOG(LogBRiskHazardVisualizer, Warning,
				TEXT("Skipping B-Risk vent %d (fromRoom=%d toRoom=%d face=%d width=%g height=%g): %s."),
				VentIndex, Vent.FromRoomId, Vent.ToRoomId, Vent.Face, Vent.Width, Vent.Height, Reason);
			continue;
		}

		// Render the opening as a 4-edge wireframe rectangle to match the room zone outline
		// style, rather than a solid slab.
		// Matches SmokeOutlineThicknessCm in BRiskSmokeVisualizer.cpp, which draws the cyan room zone
		// outline: the two are read together and a vent wire three times heavier than the room it
		// sits in reads as a different class of object. It was 6 cm, which also swallowed the
		// openings it was drawing - a leakage path is 1-40 mm wide, so its whole outline fitted
		// inside its own wire. Openings are drawn TRUE TO SCALE; nothing here widens them.
		constexpr float VentWireThicknessCm = 2.0f;

		TArray<FVector> EdgeCenters;
		TArray<FVector> EdgeSizes;
		BuildVentOutlineEdges(CenterCm, SizeCm, NormalAxis, VentWireThicknessCm, EdgeCenters, EdgeSizes);

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
			// Leakage vents have no such convention and take their own colour; see VentColourForKind.
			UMaterialInstanceDynamic* VentMaterial = MakeColoredMaterial(
				BasicShapeMaterial,
				this,
				VentColourForKind(Vent.Kind));
			if (VentMaterial)
			{
				VentEdge->SetMaterial(0, VentMaterial);
			}

			VentComponents.Add(VentEdge);
			VentMaterials.Add(VentMaterial);
		}

		// Flow-indicator geometry + two hidden flat quad regions (out/in), driven per tick by
		// SetVentFlows. A flat Plane with the two-sided unlit M_BRiskVentFlow material gives a
		// flat, uniform, double-sided region (readable from any orbit angle), matching
		// Smokeview. Outward normal = the opening's thin (wall-normal) axis pointing out of
		// FromRoom.
		if (PlaneMesh && VentFlowMaterial)
		{
			// Both of these were independently wrong, in the same two ways the outline was.
			//
			// ToUnrealBox ignores the scenario's room frame, unlike everything else in this
			// function (see MakeRoomFootprint at the top of ComputeVentSlab) - and this box's
			// centre is what decides which way the flow band faces, so under the Revit frame it
			// could point the band out of the opposite wall.
			//
			// The axis was re-derived here as "thinnest", the same inference that put every leakage
			// vent's outline in the wrong plane. ComputeVentSlab now reports the real wall normal,
			// so use it rather than guessing again.
			const FBox FromBoxCm = BRiskCoord::MakeRoomFootprint(*FromRoom, Scale, Frame).Bounds;
			const FVector FromCenterCm = FromBoxCm.GetCenter();
			FVector OutwardNormal = FVector::ZeroVector;
			OutwardNormal[NormalAxis] = (CenterCm[NormalAxis] >= FromCenterCm[NormalAxis]) ? 1.0 : -1.0;

			FVentFlowGeom& Geom = VentFlowGeometry[VentIndex];
			Geom.bValid = true;
			Geom.OpeningCenterCm = CenterCm;
			Geom.OutwardNormal = OutwardNormal;
			Geom.SillZCm = static_cast<float>(CenterCm.Z - SizeCm.Z * 0.5);
			Geom.HeadZCm = static_cast<float>(CenterCm.Z + SizeCm.Z * 0.5);
			Geom.FloorZCm = static_cast<float>(FromRoom->Origin.Z * Scale);

			auto MakeFlowArrow = [&](const TCHAR* Suffix) -> UStaticMeshComponent*
			{
				UStaticMeshComponent* Arrow = NewObject<UStaticMeshComponent>(
					this, *FString::Printf(TEXT("BRiskVentFlow_%d_%s"), VentIndex, Suffix));
				Arrow->SetupAttachment(SceneRoot);
				Arrow->SetStaticMesh(PlaneMesh);
				Arrow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Arrow->SetCastShadow(false);
				Arrow->SetReceivesDecals(false);
				Arrow->SetMobility(EComponentMobility::Movable);
				Arrow->SetVisibility(false, true);
				Arrow->SetHiddenInGame(true);
				AddInstanceComponent(Arrow);
				Arrow->OnComponentCreated();
				Arrow->RegisterComponent();
				return Arrow;
			};

			for (int32 BandIndex = 0; BandIndex < VentFlowBandsPerVent; ++BandIndex)
			{
				UStaticMeshComponent* BandQuad = MakeFlowArrow(*FString::Printf(TEXT("Band%d"), BandIndex));
				UMaterialInstanceDynamic* BandMat = MakeColoredMaterial(VentFlowMaterial, this, FLinearColor::White);
				if (BandMat)
				{
					// Translucent material: set a visible opacity (the base material's default is 0).
					BandMat->SetScalarParameterValue(TEXT("Opacity"), 0.5f);
					BandQuad->SetMaterial(0, BandMat);
				}
				const int32 FlatIndex = VentIndex * VentFlowBandsPerVent + BandIndex;
				VentFlowBandQuads[FlatIndex] = BandQuad;
				VentFlowBandMaterials[FlatIndex] = BandMat;
			}
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

	for (UStaticMeshComponent* Band : VentFlowBandQuads)
	{
		if (Band) { Band->DestroyComponent(); }
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
	VentFlowBandQuads.Reset();
	VentFlowBandMaterials.Reset();
	VentFlowGeometry.Reset();
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

void ABRiskHazardVisualizer::SetVentFlows(const TArray<FBRiskVentFlow>& VentFlows)
{
	constexpr float PlaneMeshSizeCm = 100.0f; // /Engine/BasicShapes/Plane is 100x100 cm
	constexpr float MinShownWidthCm = 3.0f;   // hide the near-neutral-plane slivers (the pinch)
	constexpr float MaxWidthCm = 120.0f;
	constexpr float KgsToWidthCm = 80.0f;     // peak mass-flow -> bulge width

	for (int32 VentIndex = 0; VentIndex < VentFlowGeometry.Num(); ++VentIndex)
	{
		const FVentFlowGeom& Geom = VentFlowGeometry[VentIndex];
		const FBRiskVentFlow Flow = VentFlows.IsValidIndex(VentIndex) ? VentFlows[VentIndex] : FBRiskVentFlow();

		// Neutral plane in cm, clamped into the opening; if none, split at the opening centre.
		const float OpeningMidZ = (Geom.SillZCm + Geom.HeadZCm) * 0.5f;
		const float NeutralZCm = (Flow.NeutralPlaneHeightM >= 0.0)
			? FMath::Clamp(Geom.FloorZCm + static_cast<float>(Flow.NeutralPlaneHeightM) * ScenarioScale, Geom.SillZCm, Geom.HeadZCm)
			: OpeningMidZ;

		const float MaxOutWidthCm = FMath::Min(static_cast<float>(Flow.MassFlowOutKgs) * KgsToWidthCm, MaxWidthCm);
		const float MaxInWidthCm = FMath::Min(static_cast<float>(Flow.MassFlowInKgs) * KgsToWidthCm, MaxWidthCm);
		const float BandHeightCm = (Geom.HeadZCm - Geom.SillZCm) / static_cast<float>(VentFlowBandsPerVent);
		const float OutSpanCm = Geom.HeadZCm - NeutralZCm;
		const float InSpanCm = NeutralZCm - Geom.SillZCm;
		const FRotator BandRotation = FRotationMatrix::MakeFromXY(Geom.OutwardNormal, FVector::UpVector).Rotator();

		// Stack of thin bands tracing the velocity profile across the opening height. The band
		// width follows ~sqrt(distance from the neutral plane), so the outer edge curves and
		// pinches to zero at the neutral plane (hot gas bulging out the top, cool air in the
		// bottom) — the Smokeview look. Each band is coloured by its stream's gas temperature.
		for (int32 BandIndex = 0; BandIndex < VentFlowBandsPerVent; ++BandIndex)
		{
			const int32 FlatIndex = VentIndex * VentFlowBandsPerVent + BandIndex;
			UStaticMeshComponent* Band = VentFlowBandQuads.IsValidIndex(FlatIndex) ? VentFlowBandQuads[FlatIndex] : nullptr;
			if (!Band)
			{
				continue;
			}
			UMaterialInstanceDynamic* Mat = VentFlowBandMaterials.IsValidIndex(FlatIndex) ? VentFlowBandMaterials[FlatIndex] : nullptr;

			const float BandZ = Geom.SillZCm + (static_cast<float>(BandIndex) + 0.5f) * BandHeightCm;
			const bool bOut = BandZ >= NeutralZCm;
			const float Span = bOut ? OutSpanCm : InSpanCm;
			const float Frac = (Span > KINDA_SMALL_NUMBER)
				? FMath::Clamp((bOut ? (BandZ - NeutralZCm) : (NeutralZCm - BandZ)) / Span, 0.0f, 1.0f)
				: 0.0f;
			const float WidthCm = (bOut ? MaxOutWidthCm : MaxInWidthCm) * FMath::Sqrt(Frac);

			const bool bShow = Geom.bValid && Flow.bHasFlow && WidthCm > MinShownWidthCm
				&& !Geom.OutwardNormal.IsNearlyZero();
			Band->SetVisibility(bShow, true);
			Band->SetHiddenInGame(!bShow);
			if (!bShow)
			{
				continue;
			}

			const float SignSide = bOut ? 1.0f : -1.0f;
			FVector Location = Geom.OpeningCenterCm;
			Location.Z = BandZ;
			Location += Geom.OutwardNormal * (SignSide * WidthCm * 0.5f); // inner edge at the door
			Band->SetRelativeLocation(Location);
			Band->SetRelativeRotation(BandRotation);
			Band->SetRelativeScale3D(FVector(WidthCm / PlaneMeshSizeCm, BandHeightCm / PlaneMeshSizeCm, 1.0f));
			if (Mat)
			{
				const FLinearColor Color = TemperatureToColor(
					bOut ? Flow.OutTemperatureC : Flow.InTemperatureC, FlowTempMinC, FlowTempMaxC);
				Mat->SetVectorParameterValue(TEXT("Color"), Color);
				Mat->SetVectorParameterValue(TEXT("BaseColor"), Color);
			}
		}
	}
}

void ABRiskHazardVisualizer::SetFlowTemperatureRange(float MinC, float MaxC)
{
	FlowTempMinC = MinC;
	FlowTempMaxC = FMath::Max(MaxC, MinC + 1.0f);
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
