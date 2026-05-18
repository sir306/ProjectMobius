// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "MobiusAgentDataImporter.h"

#include "Hdf5SimulationReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobiusAgentDataImporter, Log, All);

namespace
{
	void SetMobiusAgentImportError(FString* OutError, const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
	}

	class FMobiusJsonAgentDataParser
	{
	public:
		static bool ParseFile(const FString& FilePath, FMobiusAgentSimulationData& OutData, FString* OutError)
		{
			FString JsonString;
			if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
			{
				SetMobiusAgentImportError(OutError, FString::Printf(TEXT("Unable to read JSON data from: %s"), *FilePath));
				return false;
			}

			TSharedPtr<FJsonObject> RootObject;
			const TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonString);
			if (!FJsonSerializer::Deserialize(JsonReader, RootObject) || !RootObject.IsValid())
			{
				SetMobiusAgentImportError(OutError, FString::Printf(TEXT("Failed to deserialize JSON data from: %s"), *FilePath));
				return false;
			}

			OutData = FMobiusAgentSimulationData();
			OutData.SourceFormat = EMobiusAgentFileFormat::Json;

			if (RootObject->HasTypedField<EJson::Object>(TEXT("metadata")))
			{
				const TSharedPtr<FJsonObject> Metadata = RootObject->GetObjectField(TEXT("metadata"));
				Metadata->TryGetNumberField(TEXT("duration"), OutData.Metadata.Duration);
				Metadata->TryGetNumberField(TEXT("sampling_rate"), OutData.Metadata.SamplingRate);
				Metadata->TryGetNumberField(TEXT("max_num_entities"), OutData.Metadata.MaxNumEntities);

				bool bBoolValue = true;
				if (Metadata->TryGetBoolField(TEXT("isSI"), bBoolValue) || Metadata->TryGetBoolField(TEXT("is_si"), bBoolValue))
				{
					OutData.Metadata.bIsSI = bBoolValue;
				}
				if (Metadata->TryGetBoolField(TEXT("isDeg"), bBoolValue) || Metadata->TryGetBoolField(TEXT("is_deg"), bBoolValue))
				{
					OutData.Metadata.bIsDeg = bBoolValue;
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* JsonEntities = nullptr;
			if (RootObject->TryGetArrayField(TEXT("entities"), JsonEntities))
			{
				OutData.Entities.Reserve(JsonEntities->Num());
				for (const TSharedPtr<FJsonValue>& EntityValue : *JsonEntities)
				{
					const TSharedPtr<FJsonObject> EntityObject = EntityValue.IsValid() ? EntityValue->AsObject() : nullptr;
					if (!EntityObject.IsValid())
					{
						continue;
					}

					FMobiusAgentEntityData& Entity = OutData.Entities.AddDefaulted_GetRef();
					EntityObject->TryGetNumberField(TEXT("id"), Entity.Id);
					EntityObject->TryGetStringField(TEXT("name"), Entity.Name);
					FString SimTimeString;
					if (EntityObject->TryGetStringField(TEXT("simTimeS"), SimTimeString))
					{
						Entity.SimTimeS = FCString::Atof(*SimTimeString);
					}
					else
					{
						EntityObject->TryGetNumberField(TEXT("simTimeS"), Entity.SimTimeS);
					}
					EntityObject->TryGetNumberField(TEXT("max_speed"), Entity.MaxSpeed);
					EntityObject->TryGetStringField(TEXT("m_plane"), Entity.MPlane);
					EntityObject->TryGetNumberField(TEXT("map"), Entity.Map);
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* JsonTimesteps = nullptr;
			if (RootObject->TryGetArrayField(TEXT("simulation"), JsonTimesteps))
			{
				for (int32 TimestepIndex = 0; TimestepIndex < JsonTimesteps->Num(); ++TimestepIndex)
				{
					const TSharedPtr<FJsonObject> TimestepObject = (*JsonTimesteps)[TimestepIndex].IsValid()
						                                               ? (*JsonTimesteps)[TimestepIndex]->AsObject()
						                                               : nullptr;
					if (!TimestepObject.IsValid())
					{
						continue;
					}

					const TArray<TSharedPtr<FJsonValue>>* JsonSamples = nullptr;
					if (!TimestepObject->TryGetArrayField(TEXT("samples"), JsonSamples))
					{
						continue;
					}

					for (const TSharedPtr<FJsonValue>& SampleValue : *JsonSamples)
					{
						const TSharedPtr<FJsonObject> SampleObject = SampleValue.IsValid() ? SampleValue->AsObject() : nullptr;
						if (!SampleObject.IsValid())
						{
							continue;
						}

						FMobiusAgentSampleData& Sample = OutData.Samples.AddDefaulted_GetRef();
						Sample.TimestepIndex = TimestepIndex;
						SampleObject->TryGetNumberField(TEXT("entity"), Sample.EntityId);
						SampleObject->TryGetNumberField(TEXT("rotation"), Sample.Rotation);
						SampleObject->TryGetNumberField(TEXT("speed"), Sample.Speed);
						SampleObject->TryGetStringField(TEXT("mode"), Sample.Mode);

						const TSharedPtr<FJsonObject>* PositionObject = nullptr;
						if (SampleObject->TryGetObjectField(TEXT("position"), PositionObject) && PositionObject && PositionObject->IsValid())
						{
							(*PositionObject)->TryGetNumberField(TEXT("x"), Sample.PositionX);
							(*PositionObject)->TryGetNumberField(TEXT("y"), Sample.PositionY);
							(*PositionObject)->TryGetNumberField(TEXT("z"), Sample.PositionZ);
						}
					}
				}
			}

			if (OutData.Metadata.MaxNumEntities == 0)
			{
				OutData.Metadata.MaxNumEntities = OutData.Entities.Num();
			}

			return true;
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
