// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

#include "CoreMinimal.h"

enum class EMobiusAgentFileFormat : uint8
{
	Unknown,
	Json,
	MobiusHdf5,
	JuelichHdf5
};

struct MOBIUSDATAIMPORTER_API FMobiusAgentSimulationMetadata
{
	float Duration = 0.0f;
	float SamplingRate = 0.0f;
	int32 MaxNumEntities = 0;
	bool bIsSI = true;
	bool bIsDeg = true;
	bool bHasRotationData = true;
	bool bHasSpeedData = true;

	bool operator==(const FMobiusAgentSimulationMetadata& Other) const
	{
		return FMath::IsNearlyEqual(Duration, Other.Duration) &&
		       FMath::IsNearlyEqual(SamplingRate, Other.SamplingRate) &&
		       MaxNumEntities == Other.MaxNumEntities &&
		       bIsSI == Other.bIsSI &&
		       bIsDeg == Other.bIsDeg &&
		       bHasRotationData == Other.bHasRotationData &&
		       bHasSpeedData == Other.bHasSpeedData;
	}
};

struct MOBIUSDATAIMPORTER_API FMobiusAgentEntityData
{
	int32 Id = 0;
	FString Name;
	float SimTimeS = 0.0f;
	float MaxSpeed = 0.0f;
	FString MPlane;
	int32 Map = 0;
};

struct MOBIUSDATAIMPORTER_API FMobiusAgentSampleData
{
	int32 TimestepIndex = 0;
	int32 EntityId = 0;
	float PositionX = 0.0f;
	float PositionY = 0.0f;
	float PositionZ = 0.0f;
	float Rotation = 0.0f;
	float Speed = 0.0f;
	FString Mode;
};

struct MOBIUSDATAIMPORTER_API FMobiusAgentSimulationData
{
	FMobiusAgentSimulationMetadata Metadata;
	TArray<FMobiusAgentEntityData> Entities;
	TArray<FMobiusAgentSampleData> Samples;
	EMobiusAgentFileFormat SourceFormat = EMobiusAgentFileFormat::Unknown;
};

class MOBIUSDATAIMPORTER_API FMobiusAgentDataImporter
{
public:
	static EMobiusAgentFileFormat DetectFileFormat(const FString& FilePath);
	static bool ImportAgentFile(const FString& FilePath, FMobiusAgentSimulationData& OutData, FString* OutError = nullptr);
};
