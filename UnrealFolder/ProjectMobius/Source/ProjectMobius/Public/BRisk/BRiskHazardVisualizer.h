// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BRiskDataImporter.h"
#include "BRisk/BRiskVentFlow.h"
#include "BRiskHazardVisualizer.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Runtime visual state for one B-Risk fire source. */
struct FBRiskFireVisualState
{
	float HeatReleaseRateKw = 0.0f;
	float FlameHeightM = 0.0f;
	float FireBaseM = 0.0f;
};

/** Simple cone-based fire and sprinkler indicators for B-Risk playback. */
UCLASS()
class PROJECTMOBIUS_API ABRiskHazardVisualizer : public AActor
{
	GENERATED_BODY()

public:
	ABRiskHazardVisualizer();

	/**
	 * Rebuild fire, sprinkler and vent components for the active B-Risk scenario.
	 *
	 * Frame must be the scenario's FBRiskScenarioData::RoomFrame. Every placement here is relative
	 * to a room, so passing a different frame than the smoke volumes used puts the markers and
	 * vents in a rotated copy of the building.
	 */
	bool ConfigureFromScenario(
		const TArray<FBRiskRoomGeometry>& Rooms,
		const TArray<FBRiskFireGeometry>& Fires,
		const TArray<FBRiskSprinklerGeometry>& Sprinklers,
		const TArray<FBRiskVentGeometry>& Vents,
		float Scale,
		BRiskCoord::ERoomFrame Frame);

	/** Remove all generated hazard components. */
	void ClearHazardVisuals();

	/** Update one fire cone from sampled B-Risk HRR/flame channels. */
	bool SetFireState(int32 FireIndex, const FBRiskFireVisualState& FireState);

	/** Update all sprinkler indicators for the current simulation time. */
	void SetSimulationTime(float TimeSeconds);

	/**
	 * Update the per-vent in/out flow indicators from derived flow (one entry per vent,
	 * index-aligned with the Vents array passed to ConfigureFromScenario). Out and in
	 * streams are split at the neutral plane, sized by mass flow and coloured by stream
	 * temperature. See UBRiskDataSubsystem::ComputeWallVentFlow.
	 */
	void SetVentFlows(const TArray<FBRiskVentFlow>& VentFlows);

	/**
	 * Set the temperature range (Celsius) the vent-flow colourbar maps over. Pass the
	 * scenario's actual min/max so the hottest flow reaches red (Smokeview auto-scale).
	 */
	void SetFlowTemperatureRange(float MinC, float MaxC);

	/** Number of generated visual components. */
	int32 GetHazardVisualCount() const;

	/**
	 * Compute the world-space slab (centre + size, cm) for a B-Risk vent on its
	 * FromRoom wall. The wall is taken from room adjacency when ToRoom is a real
	 * room (the shared wall is geometrically unambiguous and matches Smokeview),
	 * and from the B-Risk/CFAST face id (1=-Y front, 2=+X right, 3=+Y rear,
	 * 4=-X left) only for vents to the exterior (ToRoom == nullptr). Returns
	 * false if FromRoom is missing or the opening has no area.
	 *
	 * Frame-dependent in two ways beyond the room box itself, because the B-Risk face ids and the
	 * along-wall offset are expressed in B-Risk axes:
	 *   - SmokeviewSwap: B-Risk +/-X -> UE +/-Y and +/-Y -> +/-X. Both are order-preserving, so an
	 *     offset measured from the box minimum stays correct on every wall.
	 *   - Revit: B-Risk +/-X -> UE +/-X unchanged, but +/-Y -> UE -/+Y. The Y direction REVERSES,
	 *     so on a YZ-plane wall the offset must be measured from the box maximum downwards.
	 *
	 * Known limitation, unchanged by the frame work: for a room with a real polygon the box is the
	 * footprint's bounding box, not a wall, so a vent on a non-convex room can sit off the true
	 * wall line. Fixing that needs vent XY in Zones-data.json.
	 */
	static bool ComputeVentSlab(
		const FBRiskVentGeometry& Vent,
		const FBRiskRoomGeometry* FromRoom,
		const FBRiskRoomGeometry* ToRoom,
		float Scale,
		BRiskCoord::ERoomFrame Frame,
		float ThicknessCm,
		FVector& OutCenterCm,
		FVector& OutSizeCm,
		int32* OutNormalAxis = nullptr,
		FVector* OutOutwardNormal = nullptr);

	/**
	 * Marker colour per opening kind.
	 *
	 * Exposed rather than written as literals at the call site - unlike the fire cone, the sprinkler
	 * cone and the vent outline, which are hard-coded because they are Smokeview's own documented
	 * defaults (FIRECOLOR 255,128,0; SPRINKONCOLOR 0,1,0; VENTCOLOR 1,0,1) and an engineer reads them
	 * without a legend. Leakage has no Smokeview convention to inherit, so it is a project choice and
	 * belongs where a future colour-customisation UI can reach it.
	 *
	 * Defaults keep doors and windows on Smokeview magenta. Leakage is red-violet - the same family,
	 * pushed toward red in linear space. It started at (1, 0, 0.5) and was moved to (1, 0, 0.2)
	 * because at 0.5 it was not separable from magenta on screen against the sky, and separability is
	 * the entire point: this is a DEBUGGING aid, expected to revert to magenta once vent placement is
	 * trusted. The per-kind mapping is the part that stays.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "B-Risk|Colours")
	FLinearColor DoorVentColour = FLinearColor(1.0f, 0.0f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "B-Risk|Colours")
	FLinearColor WindowVentColour = FLinearColor(1.0f, 0.0f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "B-Risk|Colours")
	FLinearColor LeakageVentColour = FLinearColor(1.0f, 0.0f, 0.2f, 1.0f);

	/** Colour for an opening whose kind is unknown - i.e. every vent from a .smv-only scenario. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "B-Risk|Colours")
	FLinearColor UnclassifiedVentColour = FLinearColor(1.0f, 0.0f, 1.0f, 1.0f);

	/** Resolve the marker colour for an opening kind. */
	FLinearColor VentColourForKind(EBRiskVentKind Kind) const;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaneMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BasicShapeMaterial;

	/** Unlit, two-sided material (Color param -> emissive) for the flat vent-flow regions. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> VentFlowMaterial;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> SimpleFireNiagaraSystem;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> FireConeComponents;

	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> FireNiagaraComponents;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FireMaterials;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SprinklerConeComponents;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SprinklerMaterials;

	/** One thin slab marking each B-Risk vent/opening on its FromRoom wall. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> VentComponents;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> VentMaterials;

	/**
	 * One flat, double-sided panel filling each opening, shown only while B-Risk has that opening
	 * SHUT. Index-aligned with the scenario's vent array (null where the vent could not be placed),
	 * unlike VentComponents which holds four outline edges per vent.
	 *
	 * This lives here rather than as a hole-filling quad in the room mesh for two reasons: the room
	 * mesh is built once per geometry toggle into a single procedural mesh, so making it
	 * time-varying would mean rebuilding the whole building every time a door moves; and this way it
	 * still works with the room-geometry checkbox OFF, against an imported building.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> VentClosedPanels;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> VentClosedPanelMaterials;

	/** Open/close schedule per vent, index-aligned with VentClosedPanels. See FBRiskVentGeometry::IsOpenAtTime. */
	TArray<FBRiskVentGeometry> VentData;

	/**
	 * Per-vent stack of thin flat bands tracing the in/out flow velocity profile across the
	 * opening height (curved: width ~ sqrt(distance from the neutral plane), pinched to zero
	 * at the neutral plane). Flattened: band b of vent v is at index v*<bands> + b.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> VentFlowBandQuads;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> VentFlowBandMaterials;

	/** Per-vent opening geometry (cm, world) used to place the flow indicators. */
	struct FVentFlowGeom
	{
		bool bValid = false;
		FVector OpeningCenterCm = FVector::ZeroVector;
		FVector OutwardNormal = FVector::ZeroVector; // unit, points out of FromRoom through the wall
		float SillZCm = 0.0f;
		float HeadZCm = 0.0f;
		float FloorZCm = 0.0f; // FromRoom floor Z in cm (datum for the neutral-plane height)
	};
	TArray<FVentFlowGeom> VentFlowGeometry;

	TArray<FVector> FireBaseLocationsCm;
	TArray<FBRiskSprinklerGeometry> SprinklerData;
	TArray<FVector> SprinklerHeadLocationsCm;
	TArray<float> SprinklerRoomHeightsCm;
	float ScenarioScale = 100.0f;

	/** Temperature range (Celsius) the vent-flow colourbar maps over (Smokeview-style auto-scale). */
	float FlowTempMinC = 20.0f;
	float FlowTempMaxC = 200.0f;
};
