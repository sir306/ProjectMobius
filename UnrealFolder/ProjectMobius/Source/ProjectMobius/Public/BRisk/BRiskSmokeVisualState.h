// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "BRiskSmokeVisualState.generated.h"

/** Runtime smoke material state derived from B-Risk zone CSV channels. */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FBRiskSmokeVisualState
{
	GENERATED_BODY()

	/** Smoke layer boundary as a normalized room height. 1 = clear, 0 = fully smoke-filled. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Smoke")
	float RoomSmoke = 1.0f;

	/** Raw upper-layer optical density sampled from ULOD_1. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Smoke")
	float UpperOpticalDensity = 0.0f;

	/** Raw lower-layer optical density sampled from LLOD_1. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Smoke")
	float LowerOpticalDensity = 0.0f;

	/** Napierian upper-layer extinction coefficient converted to Unreal units (1/cm). */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Smoke")
	float UpperExtinctionPerCm = 0.0f;

	/** Napierian lower-layer extinction coefficient converted to Unreal units (1/cm). */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Smoke")
	float LowerExtinctionPerCm = 0.0f;

	/** Normalized visual smoke density derived from upper-layer optical density. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Smoke")
	float SmokeDensity = 0.0f;

	/** Raw upper-layer temperature in Celsius sampled from ULT_1. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Smoke")
	float UpperTemperatureC = 24.0f;

	/** Raw lower-layer temperature in Celsius sampled from LLT_1. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Smoke")
	float LowerTemperatureC = 24.0f;

	/** Normalized heat tint derived from upper-layer temperature. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Smoke")
	float SmokeHeat = 0.0f;

	/** World-space Z (cm) of the upper-layer interface. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Smoke")
	float LayerHeightWorldCm = 0.0f;

	/** Half-width (cm) of smoothstep transition band around the layer interface. */
	UPROPERTY(BlueprintReadOnly, Category = "B-Risk|Smoke")
	float LayerSoftnessCm = 5.0f;
};
