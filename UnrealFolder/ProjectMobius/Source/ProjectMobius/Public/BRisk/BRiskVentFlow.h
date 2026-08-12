// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "BRiskVentFlow.generated.h"

/**
 * Pre-sampled two-layer state for ONE side of a wall vent at a single time. The caller
 * fills this from the zone CSV per room id (ULT/LLT/HGT/PRS via
 * UBRiskDataSubsystem::SampleRoomChannelAtTime) plus the room floor Z
 * (FBRiskRoomGeometry::Origin.Z). Heights are metres (FloorZM is absolute world datum,
 * LayerHeightM is above that floor); temperatures are Celsius; pressure is Pa gauge.
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FBRiskVentSideState
{
	GENERATED_BODY()

	/** Absolute Z of this room's floor (m). For an exterior side, the From-room floor Z. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Vent") double FloorZM = 0.0;
	/** Upper-layer temperature, Celsius (ULT). */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Vent") double UpperTempC = 20.0;
	/** Lower-layer temperature, Celsius (LLT). */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Vent") double LowerTempC = 20.0;
	/** Layer interface height above THIS room's floor (m, HGT). */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Vent") double LayerHeightM = 0.0;
	/** Room over-pressure relative to ambient at floor level (Pa, PRS). */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Vent") double PressurePa = 0.0;
	/** True when this side opens to outside; layers are forced to ambient. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Vent") bool bIsExterior = false;
};

/**
 * Smokeview-style derived flow through one wall vent at a single time. Bidirectional: a
 * doorway usually pushes hot gas OUT the top while drawing cool air IN the bottom, so both
 * gross streams and a representative colour temperature for each are reported. QUALITATIVE
 * fallback only (B-Risk does not export per-vent flow; Smokeview itself computes it) —
 * validated visually, not numerically. See UBRiskDataSubsystem::ComputeWallVentFlow.
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FBRiskVentFlow
{
	GENERATED_BODY()

	/** Gross mass flow From-room -> To-room (kg/s), >= 0. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Vent") double MassFlowOutKgs = 0.0;
	/** Gross mass flow To-room -> From-room (kg/s), >= 0. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Vent") double MassFlowInKgs = 0.0;
	/** Mass-weighted donor temperature of the OUT stream (Celsius); for colour. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Vent") double OutTemperatureC = 20.0;
	/** Mass-weighted donor temperature of the IN stream (Celsius); for colour. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Vent") double InTemperatureC = 20.0;
	/** Neutral-plane height above the From-room floor (m); split point for the in/out arrows. Negative = none. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Vent") double NeutralPlaneHeightM = -1.0;
	/** True if any non-trivial flux was found this step. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Vent") bool bHasFlow = false;
};
