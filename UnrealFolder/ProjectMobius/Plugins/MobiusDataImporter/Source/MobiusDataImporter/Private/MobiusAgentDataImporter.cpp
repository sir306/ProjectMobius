// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "MobiusAgentDataImporter.h"

#include "Hdf5SimulationReader.h"
#include "Misc/Paths.h"
#include "MobiusJsonParserCommon.h"

DEFINE_LOG_CATEGORY(LogMobiusAgentDataImporter);

namespace
{
	void SetMobiusAgentImportError(FString* OutError, const FString& Message)
	{
		MobiusJsonImport::SetError(OutError, Message);
	}

	/**
	 * JSON dispatch (perf task A7): simdjson SAX over raw UTF-8 first; ANY simdjson failure
	 * (malformed document, unsupported encoding, size limit) retries with the engine TJsonReader
	 * pull-parser. The two engines live in MobiusJsonSimdParser.cpp / MobiusJsonPullParser.cpp and
	 * produce bit-identical output (ProjectMobius.SimData.JsonParserParity).
	 */
	class FMobiusJsonAgentDataParser
	{
	public:
		static bool ParseFile(const FString& FilePath, FMobiusAgentSimulationData& OutData, FString* OutError)
		{
			if (MobiusJsonImport::IsSimdJsonEnabled())
			{
				FString SimdJsonError;
				if (FMobiusAgentDataImporter::ParseJsonWithSimdjson(FilePath, OutData, &SimdJsonError))
				{
					return true;
				}
				UE_LOG(LogMobiusAgentDataImporter, Warning,
				       TEXT("simdjson JSON parse failed (%s), retrying with the engine pull-parser: %s"),
				       *SimdJsonError, *FilePath);
			}
			return FMobiusAgentDataImporter::ParseJsonWithPullParser(FilePath, OutData, OutError);
		}
	};

	class FMobiusHdf5AgentDataParser
	{
	public:
		static bool ParseFile(const FString& FilePath, FMobiusAgentSimulationData& OutData, FString* OutError)
		{
			FHdf5SimulationReader Reader;
			if (!Reader.OpenFile(FilePath))
			{
				SetMobiusAgentImportError(OutError, FString::Printf(TEXT("Unable to read HDF5 data from: %s"), *FilePath));
				return false;
			}

			OutData = FMobiusAgentSimulationData();
			const EHdf5FormatType Hdf5Format = Reader.GetDetectedFormat();
			OutData.SourceFormat = Hdf5Format == EHdf5FormatType::Juelich
				                       ? EMobiusAgentFileFormat::JuelichHdf5
				                       : Hdf5Format == EHdf5FormatType::Mobius
				                       ? EMobiusAgentFileFormat::MobiusHdf5
				                       : EMobiusAgentFileFormat::Unknown;

			FHdf5SimulationMetadata Hdf5Metadata;
			TArray<FHdf5EntityData> Hdf5Entities;
			TArray<FHdf5SampleData> Hdf5Samples;

			if (Hdf5Format == EHdf5FormatType::Juelich)
			{
				FHdf5JuelichMetadata JuelichMetadata;
				TArray<FHdf5JuelichTrajectoryRecord> Trajectories;
				if (!Reader.ReadJuelichMetadata(JuelichMetadata) || !Reader.ReadJuelichTrajectories(Trajectories) ||
				    !FHdf5SimulationReader::ConvertJuelichToMobiusFormat(JuelichMetadata, Trajectories, Hdf5Metadata, Hdf5Entities, Hdf5Samples))
				{
					SetMobiusAgentImportError(OutError, FString::Printf(TEXT("Unable to import Juelich HDF5 data from: %s"), *FilePath));
					Reader.CloseFile();
					return false;
				}
			}
			else if (Hdf5Format == EHdf5FormatType::Mobius)
			{
				bool bHasRotationField = true;
				bool bHasSpeedField = true;
				if (!Reader.ReadMetadata(Hdf5Metadata) || !Reader.ReadEntities(Hdf5Entities) ||
				    !Reader.ReadAllSamples(Hdf5Samples, &bHasRotationField, &bHasSpeedField))
				{
					SetMobiusAgentImportError(OutError, FString::Printf(TEXT("Unable to import Mobius HDF5 data from: %s"), *FilePath));
					Reader.CloseFile();
					return false;
				}
				Hdf5Metadata.bHasRotationData = bHasRotationField;
				Hdf5Metadata.bHasSpeedData = bHasSpeedField;
			}
			else
			{
				SetMobiusAgentImportError(OutError, FString::Printf(TEXT("HDF5 file has unrecognized format: %s"), *FilePath));
				Reader.CloseFile();
				return false;
			}

			Reader.CloseFile();

			OutData.Metadata.Duration = Hdf5Metadata.Duration;
			OutData.Metadata.SamplingRate = Hdf5Metadata.SamplingRate;
			OutData.Metadata.MaxNumEntities = Hdf5Metadata.MaxNumEntities;
			OutData.Metadata.bIsSI = Hdf5Metadata.bIsSI;
			OutData.Metadata.bIsDeg = Hdf5Metadata.bIsDeg;
			OutData.Metadata.bHasRotationData = Hdf5Metadata.bHasRotationData;
			OutData.Metadata.bHasSpeedData = Hdf5Metadata.bHasSpeedData;

			OutData.Entities.Reserve(Hdf5Entities.Num());
			for (const FHdf5EntityData& Hdf5Entity : Hdf5Entities)
			{
				FMobiusAgentEntityData& Entity = OutData.Entities.AddDefaulted_GetRef();
				Entity.Id = Hdf5Entity.Id;
				Entity.Name = Hdf5Entity.Name;
				Entity.SimTimeS = Hdf5Entity.SimTimeS;
				Entity.MaxSpeed = Hdf5Entity.MaxSpeed;
				Entity.MPlane = Hdf5Entity.MPlane;
				Entity.Map = Hdf5Entity.Map;
			}

			OutData.Samples.Reserve(Hdf5Samples.Num());
			for (const FHdf5SampleData& Hdf5Sample : Hdf5Samples)
			{
				FMobiusAgentSampleData& Sample = OutData.Samples.AddDefaulted_GetRef();
				Sample.TimestepIndex = Hdf5Sample.TimestepIndex;
				Sample.EntityId = Hdf5Sample.EntityId;
				Sample.PositionX = Hdf5Sample.PositionX;
				Sample.PositionY = Hdf5Sample.PositionY;
				Sample.PositionZ = Hdf5Sample.PositionZ;
				Sample.Rotation = Hdf5Sample.Rotation;
				Sample.Speed = Hdf5Sample.Speed;
				Sample.Mode = Hdf5Sample.Mode;
			}

			UE_LOG(LogMobiusAgentDataImporter, Log, TEXT("Imported %s: %d entities, %d samples"),
			       *FilePath, OutData.Entities.Num(), OutData.Samples.Num());
			return true;
		}
	};
}

EMobiusAgentFileFormat FMobiusAgentDataImporter::DetectFileFormat(const FString& FilePath)
{
	const FString Extension = FPaths::GetExtension(FilePath).ToLower();
	if (Extension == TEXT("json"))
	{
		return EMobiusAgentFileFormat::Json;
	}
	if (Extension == TEXT("h5"))
	{
		switch (FHdf5SimulationReader::DetectFormat(FilePath))
		{
		case EHdf5FormatType::Mobius:
			return EMobiusAgentFileFormat::MobiusHdf5;
		case EHdf5FormatType::Juelich:
			return EMobiusAgentFileFormat::JuelichHdf5;
		default:
			return EMobiusAgentFileFormat::Unknown;
		}
	}
	return EMobiusAgentFileFormat::Unknown;
}

bool FMobiusAgentDataImporter::ImportAgentFile(const FString& FilePath, FMobiusAgentSimulationData& OutData, FString* OutError)
{
	if (FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
	{
		SetMobiusAgentImportError(OutError, FString::Printf(TEXT("File path does not exist: %s"), *FilePath));
		return false;
	}

	const FString Extension = FPaths::GetExtension(FilePath).ToLower();
	if (Extension == TEXT("json"))
	{
		return FMobiusJsonAgentDataParser::ParseFile(FilePath, OutData, OutError);
	}
	if (Extension == TEXT("h5"))
	{
		return FMobiusHdf5AgentDataParser::ParseFile(FilePath, OutData, OutError);
	}

	SetMobiusAgentImportError(OutError, FString::Printf(TEXT("File format not supported: %s"), *FilePath));
	return false;
}
