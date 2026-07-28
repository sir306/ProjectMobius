// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "BRisk/BRiskSmokeVisualizer.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "UObject/ConstructorHelpers.h"

/**
 * Single-scattering albedo handed to M_CustomHeterogeneousVolume as User.SmokeAlbedo.
 *
 * Always pushed from here rather than left to the material default, because the Niagara renderer
 * binds User.SmokeAlbedo -> the material's Albedo unconditionally: whatever the user parameter
 * happens to default to in the asset WILL win, and a 0.0 default renders the smoke pure black.
 * Driving it from a CVar makes the value deterministic and tunable in a running session.
 *
 * Deliberately NOT derived from B-Risk soot yield. Albedo is a property of the particulate and is
 * roughly constant; soot *concentration* drives extinction, which already reaches the material via
 * ULOD -> kappa. SR282 gives no soot-yield-to-albedo relation, so any curve here would be invented.
 */
static TAutoConsoleVariable<float> CVarBRiskSmokeAlbedo(
	TEXT("Mobius.BRisk.SmokeAlbedo"),
	0.5f,
	TEXT("Single-scattering albedo for B-Risk smoke volumes (0..1). 0.5 reads as mid-grey smoke;\n")
	TEXT("lower is sootier/darker. Does not affect extinction, only how much light scatters."),
	ECVF_Default);

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

		// Explicitly identity: polygon rooms yaw their edges, so a reused component could
		// otherwise carry a stale rotation into an axis-aligned box edge.
		EdgeComponent->SetRelativeRotation(FRotator::ZeroRotator);
		EdgeComponent->SetRelativeLocation(CenterCm);
		EdgeComponent->SetRelativeScale3D(SizeCm / 100.0f);
	}

	/**
	 * Places a cube-mesh edge spanning StartCm -> EndCm at height ZCm, yawed to follow the
	 * segment. Polygon footprint edges are not axis-aligned in general, so the box-outline
	 * form above cannot express them.
	 */
	void ConfigureSegmentOutlineEdge(
		UStaticMeshComponent* EdgeComponent,
		const FVector2D& StartCm,
		const FVector2D& EndCm,
		double ZCm,
		double ThicknessCm)
	{
		if (!EdgeComponent)
		{
			return;
		}

		const FVector2D Delta = EndCm - StartCm;
		const double LengthCm = Delta.Size();
		if (LengthCm <= UE_KINDA_SMALL_NUMBER)
		{
			// MakeRoomFootprint welds coincident vertices, so this should be unreachable. Hide
			// rather than bare-return: the caller has already made the component visible, and a
			// silent return would leave it showing at whatever transform it last had.
			EdgeComponent->SetVisibility(false, true);
			EdgeComponent->SetHiddenInGame(true);
			return;
		}

		const FVector2D MidCm = (StartCm + EndCm) * 0.5;
		EdgeComponent->SetRelativeRotation(
			FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)), 0.0));
		EdgeComponent->SetRelativeLocation(FVector(MidCm.X, MidCm.Y, ZCm));
		// Overlong by one thickness so consecutive edges close the corner instead of gapping.
		EdgeComponent->SetRelativeScale3D(
			FVector(LengthCm + ThicknessCm, ThicknessCm, ThicknessCm) / 100.0);
	}

	/** Stand-in when a room index has no stored ring, so the box path can be reached safely. */
	const TArray<FVector2D> EmptyFootprintRing;

	/**
	 * Texels per side of the footprint mask. 256 over the widest room here (Corridor 15, 17.8 m)
	 * is ~7 cm/texel — far finer than a smoke voxel, so the mask edge is limited by the grid, not
	 * by this. Bilinear sampling then softens the boundary instead of stair-stepping it.
	 */
	constexpr int32 FootprintMaskResolution = 256;

	/** Build the transient coverage texture Niagara samples to clip smoke to the real plan. */
	UTexture2D* CreateFootprintMaskTexture(const BRiskCoord::FRoomFootprintCm& Footprint)
	{
		TArray<uint8> Mask;
		BRiskCoord::RasteriseFootprintMask(Footprint, FootprintMaskResolution, Mask);

		UTexture2D* MaskTexture = UTexture2D::CreateTransient(
			FootprintMaskResolution, FootprintMaskResolution, PF_G8);
		if (!MaskTexture)
		{
			return nullptr;
		}

		// Deliberately not renamed into Outer: on a reconfigure the previous mask for this room
		// index may still be alive awaiting GC, and a name collision on Rename is an assert for
		// no benefit beyond a nicer name in a profiler.
		// Coverage, not colour: sRGB would gamma-curve the 0/255 edge.
		MaskTexture->SRGB = false;
		MaskTexture->Filter = TF_Bilinear;
		MaskTexture->AddressX = TA_Clamp;
		MaskTexture->AddressY = TA_Clamp;
		MaskTexture->NeverStream = true;

		FTexturePlatformData* PlatformData = MaskTexture->GetPlatformData();
		if (!PlatformData || PlatformData->Mips.Num() == 0)
		{
			return nullptr;
		}

		FTexture2DMipMap& Mip = PlatformData->Mips[0];
		void* Destination = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Destination, Mask.GetData(), Mask.Num());
		Mip.BulkData.Unlock();
		MaskTexture->UpdateResource();

		return MaskTexture;
	}

	/** Layer-interface edges for one room: one per polygon edge, or the 4 sides of a box. */
	int32 SmokeOutlineLayerEdgeCountForRoom(int32 PolygonVertexCount)
	{
		return PolygonVertexCount >= 3 ? PolygonVertexCount : SmokeOutlineLayerEdges;
	}

	/** Total outline edges for one room: a polygon prism is floor + ceiling + posts + layer. */
	int32 SmokeOutlineEdgeCountForRoom(int32 PolygonVertexCount)
	{
		return PolygonVertexCount >= 3 ? PolygonVertexCount * 4 : SmokeOutlineEdgesPerRoom;
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

		// Scenario-level render setting rather than sampled state, but pushed on the same path so a
		// live CVar change takes effect on the next scrub without a reconfigure.
		NiagaraComponent->SetVariableFloat(
			TEXT("User.SmokeAlbedo"),
			FMath::Clamp(CVarBRiskSmokeAlbedo.GetValueOnGameThread(), 0.0f, 1.0f));
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

bool ABRiskSmokeVisualizer::ConfigureFromRooms(
	const TArray<FBRiskRoomGeometry>& Rooms,
	float Scale,
	BRiskCoord::ERoomFrame Frame)
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

	// Resolve every footprint before sizing anything: the outline edge count is now per-room
	// (4 per polygon vertex, or 16 for a box), so there is no stride to multiply by.
	TArray<BRiskCoord::FRoomFootprintCm> Footprints;
	Footprints.SetNum(Rooms.Num());
	SmokeRoomPolygonsCm.Reset();
	SmokeRoomPolygonsCm.SetNum(Rooms.Num());
	SmokeOutlineEdgeOffsets.Reset();
	SmokeOutlineEdgeOffsets.SetNum(Rooms.Num());

	int32 TotalOutlineEdges = 0;
	int32 PolygonRoomCount = 0;
	for (int32 RoomIndex = 0; RoomIndex < Rooms.Num(); ++RoomIndex)
	{
		SmokeOutlineEdgeOffsets[RoomIndex] = TotalOutlineEdges;

		const FBRiskRoomGeometry& Room = Rooms[RoomIndex];
		if (Room.Size.X <= 0.0 || Room.Size.Y <= 0.0 || Room.Size.Z <= 0.0)
		{
			// Skipped below with a warning; reserve no edges for it.
			continue;
		}

		Footprints[RoomIndex] = BRiskCoord::MakeRoomFootprint(Room, Scale, Frame);
		SmokeRoomPolygonsCm[RoomIndex] = Footprints[RoomIndex].Polygon;
		PolygonRoomCount += Footprints[RoomIndex].bFromPolygon ? 1 : 0;
		TotalOutlineEdges += SmokeOutlineEdgeCountForRoom(SmokeRoomPolygonsCm[RoomIndex].Num());
	}

	SmokeVolumeComponents.Reserve(Rooms.Num());
	SmokeMaterialInstances.Reserve(Rooms.Num());
	SmokeOutlineEdgeComponents.Reserve(TotalOutlineEdges);
	SmokeOutlineEdgeMaterialInstances.Reserve(TotalOutlineEdges);
	SmokeNiagaraComponents.Reserve(Rooms.Num());
	SmokeRoomOriginsCm.Reserve(Rooms.Num());
	SmokeRoomSizesCm.Reserve(Rooms.Num());
	LastSmokeStates.Reserve(Rooms.Num());
	SmokeVolumeComponents.SetNum(Rooms.Num());
	SmokeMaterialInstances.SetNum(Rooms.Num());
	SmokeOutlineEdgeComponents.SetNum(TotalOutlineEdges);
	SmokeOutlineEdgeMaterialInstances.SetNum(TotalOutlineEdges);
	SmokeNiagaraComponents.SetNum(Rooms.Num());
	SmokeRoomOriginsCm.SetNum(Rooms.Num());
	SmokeRoomSizesCm.SetNum(Rooms.Num());
	SmokeFootprintMasks.SetNum(Rooms.Num());
	LastSmokeStates.SetNum(Rooms.Num());

	int32 CreatedVolumeCount = 0;
	int32 CreatedNiagaraCount = 0;
	int32 MaskBoundCount = 0;

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

		// Where the room actually is: the Zones-data.json footprint when there is one, else the
		// equivalent rectangle - both in the measured Revit frame, not the legacy X<->Y swap.
		// The Niagara volume stays a BOX over these bounds; for a non-convex room it therefore
		// spills outside the real footprint until the polygon mask lands. The outline drawn by
		// UpdateSmokeOutlineEdges traces the true polygon, so the two disagree on purpose.
		const FBox RoomBoxCm = Footprints[RoomIndex].Bounds;
		const FVector OriginCm = RoomBoxCm.Min;
		const FVector SizeCm = RoomBoxCm.GetSize();
		SmokeRoomOriginsCm[RoomIndex] = OriginCm;
		SmokeRoomSizesCm[RoomIndex] = SizeCm;

		FBRiskSmokeVisualState ClearSmokeState;
		ClearSmokeState.RoomSmoke = 1.0f;
		ClearSmokeState.LayerHeightWorldCm = OriginCm.Z + SizeCm.Z;
		ClearSmokeState.LayerSoftnessCm = 5.0f;
		LastSmokeStates[RoomIndex] = ClearSmokeState;

		const int32 RoomRingVertexCount = SmokeRoomPolygonsCm[RoomIndex].Num();
		const int32 RoomEdgeCount = SmokeOutlineEdgeCountForRoom(RoomRingVertexCount);
		const int32 RoomLayerEdgeCount = SmokeOutlineLayerEdgeCountForRoom(RoomRingVertexCount);
		const int32 RoomZoneEdgeCount = RoomEdgeCount - RoomLayerEdgeCount;
		const int32 RoomEdgeOffset = SmokeOutlineEdgeOffsets[RoomIndex];

		for (int32 EdgeIndex = 0; EdgeIndex < RoomEdgeCount; ++EdgeIndex)
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
					const FLinearColor EdgeColor = EdgeIndex < RoomZoneEdgeCount
						? RoomZoneOutlineColor
						: LayerInterfaceOutlineColor;
					EdgeMaterial->SetVectorParameterValue(TEXT("Color"), EdgeColor);
					EdgeMaterial->SetVectorParameterValue(TEXT("BaseColor"), EdgeColor);
					EdgeComponent->SetMaterial(0, EdgeMaterial);
				}
			}

			const int32 FlatEdgeIndex = RoomEdgeOffset + EdgeIndex;
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

			// Clip the box volume to the real footprint. The mask spans exactly this room's
			// bounding box, so the shader samples it at the voxel's own normalised XY with no
			// transform. Rooms with no polygon get an all-inside mask, not a special case.
			if (UTexture2D* MaskTexture = CreateFootprintMaskTexture(Footprints[RoomIndex]))
			{
				SmokeFootprintMasks[RoomIndex] = MaskTexture;
				// SetVariableTexture, NOT UNiagaraFunctionLibrary::SetTextureObject. The two
				// target different parameter types and only one of them exists here:
				//   - SetTextureObject keys on FNiagaraTypeDefinition(UNiagaraDataInterfaceTexture),
				//     i.e. a user parameter that IS a Texture Sample data interface.
				//   - SetVariableTexture keys on GetUTextureDef(), a plain UTexture object param.
				// User.FootprintMask is the latter: the Texture Sample DI lives inline on the
				// module input, and its TextureUserParameter binding is declared as
				// FNiagaraTypeDefinition(UTexture::StaticClass()) (NiagaraDataInterfaceTexture.cpp
				// ctor), so the user parameter it points at is a UTexture. Calling the DI setter
				// misses on type and logs "Could not find index of variable" whatever the name is.
				// Name keeps the "User." prefix - the redirection store stores it that way.
				NiagaraComponent->SetVariableTexture(TEXT("User.FootprintMask"), MaskTexture);

				// SetVariableTexture is silent when the parameter is absent or is a different
				// type, so a clean log would otherwise be indistinguishable from a successful
				// bind. Count the resolved bindings instead of trusting the absence of a warning.
				MaskBoundCount += NiagaraComponent->GetOverrideParameters().IndexOf(
					FNiagaraVariable(FNiagaraTypeDefinition::GetUTextureDef(),
						TEXT("User.FootprintMask"))) != INDEX_NONE ? 1 : 0;
			}

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

	// polygonRooms/outlineEdges make the outline shape self-reporting: a polygon room contributes
	// 4 edges per vertex, a rectangle room 16. If the outline ever looks like a box when a
	// footprint exists, this line says whether the geometry or the build is at fault.
	UE_LOG(LogBRiskSmokeVisualizer, Log,
		TEXT("Configured B-Risk smoke visualizer: requestedRooms=%d polygonRooms=%d outlineEdges=%d ")
		TEXT("createdNiagara=%d maskBound=%d createdFallbackVolumes=%d scale=%g niagara=%s material=%s"),
		Rooms.Num(),
		PolygonRoomCount,
		TotalOutlineEdges,
		CreatedNiagaraCount,
		MaskBoundCount,
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
	for (UStaticMeshComponent* EdgeComponent : SmokeOutlineEdgeComponents)
	{
		if (EdgeComponent)
		{
			EdgeComponent->DestroyComponent();
		}
	}
	SmokeOutlineEdgeComponents.Reset();
	SmokeOutlineEdgeMaterialInstances.Reset();
	SmokeRoomPolygonsCm.Reset();
	SmokeOutlineEdgeOffsets.Reset();
	SmokeFootprintMasks.Reset();

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

	const int32 RoomEdgeOffset = SmokeOutlineEdgeOffsets.IsValidIndex(RoomIndex)
		? SmokeOutlineEdgeOffsets[RoomIndex]
		: 0;

	const auto EdgeComponentAt = [this, RoomEdgeOffset](int32 LocalEdgeIndex) -> UStaticMeshComponent*
	{
		const int32 FlatEdgeIndex = RoomEdgeOffset + LocalEdgeIndex;
		return SmokeOutlineEdgeComponents.IsValidIndex(FlatEdgeIndex)
			? SmokeOutlineEdgeComponents[FlatEdgeIndex].Get()
			: nullptr;
	};

	const auto SetEdgeVisible = [](UStaticMeshComponent* EdgeComponent, bool bVisible)
	{
		EdgeComponent->SetVisibility(bVisible, true);
		EdgeComponent->SetHiddenInGame(!bVisible);
	};

	// Polygon rooms trace the real Zones-data.json footprint: a floor ring, a ceiling ring, a
	// corner post per vertex, then the moving layer-interface ring. The Niagara volume behind
	// this is still the bounding box, so on a non-convex room the smoke deliberately overhangs
	// the outline until the polygon mask lands.
	const TArray<FVector2D>& Ring = SmokeRoomPolygonsCm.IsValidIndex(RoomIndex)
		? SmokeRoomPolygonsCm[RoomIndex]
		: EmptyFootprintRing;

	if (Ring.Num() >= 3)
	{
		const int32 RingVertexCount = Ring.Num();
		const float PostHeightCm = FMath::Max(TopZ - FloorZ, SmokeOutlineThicknessCm);
		const float PostMidZ = (FloorZ + TopZ) * 0.5f;

		for (int32 VertexIndex = 0; VertexIndex < RingVertexCount; ++VertexIndex)
		{
			const FVector2D& SegmentStart = Ring[VertexIndex];
			const FVector2D& SegmentEnd = Ring[(VertexIndex + 1) % RingVertexCount];

			if (UStaticMeshComponent* FloorEdge = EdgeComponentAt(VertexIndex))
			{
				SetEdgeVisible(FloorEdge, true);
				ConfigureSegmentOutlineEdge(FloorEdge, SegmentStart, SegmentEnd, FloorZ, SmokeOutlineThicknessCm);
			}

			if (UStaticMeshComponent* CeilingEdge = EdgeComponentAt(RingVertexCount + VertexIndex))
			{
				SetEdgeVisible(CeilingEdge, true);
				ConfigureSegmentOutlineEdge(CeilingEdge, SegmentStart, SegmentEnd, TopZ, SmokeOutlineThicknessCm);
			}

			if (UStaticMeshComponent* CornerPost = EdgeComponentAt(2 * RingVertexCount + VertexIndex))
			{
				SetEdgeVisible(CornerPost, true);
				ConfigureOutlineEdge(
					CornerPost,
					FVector(SegmentStart.X, SegmentStart.Y, PostMidZ),
					FVector(SmokeOutlineThicknessCm, SmokeOutlineThicknessCm, PostHeightCm));
			}

			if (UStaticMeshComponent* LayerEdge = EdgeComponentAt(3 * RingVertexCount + VertexIndex))
			{
				SetEdgeVisible(LayerEdge, bShowLayerLine);
				if (bShowLayerLine)
				{
					ConfigureSegmentOutlineEdge(LayerEdge, SegmentStart, SegmentEnd, LayerZ, SmokeOutlineThicknessCm);
				}
			}
		}

		return;
	}

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
			UStaticMeshComponent* EdgeComponent = EdgeComponentAt(EdgeBaseIndex + LocalEdgeIndex);
			if (!EdgeComponent)
			{
				continue;
			}

			SetEdgeVisible(EdgeComponent, bVisible);
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
		UStaticMeshComponent* EdgeComponent = EdgeComponentAt(SmokeOutlineEdgesPerBox + LayerEdgeIndex);
		if (!EdgeComponent)
		{
			continue;
		}

		SetEdgeVisible(EdgeComponent, bShowLayerLine);
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
