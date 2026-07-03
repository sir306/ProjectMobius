// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

/**
 * A7 primary JSON parse path: simdjson (ondemand, error-code API) over the raw UTF-8 bytes.
 *
 * Why this exists: the old DOM path was `LoadFileToString` (UTF-8 -> UTF-16 doubles the bytes)
 * followed by `FJsonSerializer::Deserialize` building one TSharedPtr<FJsonValue> per JSON node
 * (~+4 GB transient on a 325 MB file, ~15 MB/s). This path never converts the document to TCHAR
 * and never builds a DOM — the ondemand cursor walks the tape once and writes straight into the
 * final dense arrays.
 *
 * Error policy (PRD §A7): shape mismatches inside the document (wrong-typed field, non-object
 * array element) degrade exactly like the old DOM TryGet* — leave the default / skip the element.
 * Anything harder (malformed JSON, bad UTF-8, truncation, UTF-16 source, >4 GiB document) fails
 * the whole parse, and ImportAgentFile retries with the TJsonReader pull-parser, which handles
 * exotic encodings via LoadFileToString.
 */

#include "MobiusJsonParserCommon.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "MobiusSimdJson.h"

static TAutoConsoleVariable<int32> CVarMobiusImportSimdJson(
	TEXT("mobius.Import.SimdJson"),
	1,
	TEXT("First JSON import parse path (perf task A7). 1 = simdjson SAX over raw UTF-8 (default; any\n")
	TEXT("simdjson failure still falls back to the engine pull-parser), 0 = always use the engine\n")
	TEXT("TJsonReader pull-parser. Both produce bit-identical data; 0 is a safety valve / A-B switch."),
	ECVF_Default);

bool MobiusJsonImport::IsSimdJsonEnabled()
{
	return CVarMobiusImportSimdJson.GetValueOnAnyThread() != 0;
}

namespace
{
	using namespace simdjson;

	FString Utf8ToFString(std::string_view Utf8)
	{
		const FUTF8ToTCHAR Converted(Utf8.data(), static_cast<int32>(Utf8.size()));
		return FString(Converted.Length(), Converted.Get());
	}

	/** Strict float field: JSON number -> (float)double (identical to TryGetNumberField(float&)).
	 *  Wrong type leaves Out untouched; only hard document errors return false. */
	bool ReadFloatField(ondemand::value& Value, float& Out, error_code& OutHardError)
	{
		double Double = 0.0;
		const error_code Error = Value.get_double().get(Double);
		if (Error == SUCCESS)
		{
			Out = static_cast<float>(Double);
		}
		else if (Error != INCORRECT_TYPE)
		{
			OutHardError = Error;
			return false;
		}
		return true;
	}

	bool ReadInt32Field(ondemand::value& Value, int32& Out, error_code& OutHardError)
	{
		double Double = 0.0;
		const error_code Error = Value.get_double().get(Double);
		if (Error == SUCCESS)
		{
			MobiusJsonImport::NumberToInt32(Double, Out);
		}
		else if (Error != INCORRECT_TYPE)
		{
			OutHardError = Error;
			return false;
		}
		return true;
	}

	bool ReadStringField(ondemand::value& Value, FString& Out, error_code& OutHardError)
	{
		std::string_view StringView;
		const error_code Error = Value.get_string().get(StringView);
		if (Error == SUCCESS)
		{
			Out = Utf8ToFString(StringView);
		}
		else if (Error != INCORRECT_TYPE)
		{
			OutHardError = Error;
			return false;
		}
		return true;
	}

	bool ReadBoolField(ondemand::value& Value, bool& Out, bool& bOutWasSet, error_code& OutHardError)
	{
		bool Bool = false;
		const error_code Error = Value.get_bool().get(Bool);
		if (Error == SUCCESS)
		{
			Out = Bool;
			bOutWasSet = true;
		}
		else if (Error != INCORRECT_TYPE)
		{
			OutHardError = Error;
			return false;
		}
		return true;
	}

	/** simTimeS ships as a string in some exporters and a number in others (long-standing quirk the
	 *  DOM path handled explicitly): string -> FCString::Atof, number -> (float)double. */
	bool ReadSimTimeField(ondemand::value& Value, float& Out, error_code& OutHardError)
	{
		ondemand::json_type Type;
		const error_code TypeError = Value.type().get(Type);
		if (TypeError != SUCCESS)
		{
			OutHardError = TypeError;
			return false;
		}
		if (Type == ondemand::json_type::string)
		{
			FString AsString;
			if (!ReadStringField(Value, AsString, OutHardError))
			{
				return false;
			}
			Out = FCString::Atof(*AsString);
			return true;
		}
		if (Type == ondemand::json_type::number)
		{
			return ReadFloatField(Value, Out, OutHardError);
		}
		return true; // other types: leave default, like the DOM TryGet*
	}

	bool ParseMetadataObject(ondemand::value& MetadataValue, FMobiusAgentSimulationMetadata& OutMetadata, error_code& OutHardError)
	{
		ondemand::object MetadataObject;
		const error_code ObjectError = MetadataValue.get_object().get(MetadataObject);
		if (ObjectError == INCORRECT_TYPE)
		{
			return true; // "metadata" is not an object -> skipped (old HasTypedField guard)
		}
		if (ObjectError != SUCCESS)
		{
			OutHardError = ObjectError;
			return false;
		}

		// The DOM preferred the canonical spelling regardless of file order: "isSI" beats "is_si".
		bool bSawCanonicalIsSI = false;
		bool bSawCanonicalIsDeg = false;

		for (auto FieldResult : MetadataObject)
		{
			ondemand::field Field;
			if ((OutHardError = std::move(FieldResult).get(Field)) != SUCCESS)
			{
				return false;
			}
			std::string_view Key;
			if ((OutHardError = Field.unescaped_key().get(Key)) != SUCCESS)
			{
				return false;
			}
			ondemand::value Value = Field.value();

			bool bBoolWasSet = false;
			if (Key == "duration")
			{
				if (!ReadFloatField(Value, OutMetadata.Duration, OutHardError)) { return false; }
			}
			else if (Key == "sampling_rate")
			{
				if (!ReadFloatField(Value, OutMetadata.SamplingRate, OutHardError)) { return false; }
			}
			else if (Key == "max_num_entities")
			{
				if (!ReadInt32Field(Value, OutMetadata.MaxNumEntities, OutHardError)) { return false; }
			}
			else if (Key == "isSI")
			{
				if (!ReadBoolField(Value, OutMetadata.bIsSI, bSawCanonicalIsSI, OutHardError)) { return false; }
			}
			else if (Key == "is_si")
			{
				bool bAlternate = OutMetadata.bIsSI;
				if (!ReadBoolField(Value, bAlternate, bBoolWasSet, OutHardError)) { return false; }
				if (bBoolWasSet && !bSawCanonicalIsSI)
				{
					OutMetadata.bIsSI = bAlternate;
				}
			}
			else if (Key == "isDeg")
			{
				if (!ReadBoolField(Value, OutMetadata.bIsDeg, bSawCanonicalIsDeg, OutHardError)) { return false; }
			}
			else if (Key == "is_deg")
			{
				bool bAlternate = OutMetadata.bIsDeg;
				if (!ReadBoolField(Value, bAlternate, bBoolWasSet, OutHardError)) { return false; }
				if (bBoolWasSet && !bSawCanonicalIsDeg)
				{
					OutMetadata.bIsDeg = bAlternate;
				}
			}
			// unknown keys: left unconsumed -> ondemand skips them on the next iteration
		}
		return true;
	}

	bool ParseEntitiesArray(ondemand::value& EntitiesValue, TArray<FMobiusAgentEntityData>& OutEntities, error_code& OutHardError)
	{
		ondemand::array EntitiesArray;
		const error_code ArrayError = EntitiesValue.get_array().get(EntitiesArray);
		if (ArrayError == INCORRECT_TYPE)
		{
			return true; // "entities" is not an array -> skipped (old TryGetArrayField)
		}
		if (ArrayError != SUCCESS)
		{
			OutHardError = ArrayError;
			return false;
		}

		for (auto ElementResult : EntitiesArray)
		{
			ondemand::value Element;
			if ((OutHardError = std::move(ElementResult).get(Element)) != SUCCESS)
			{
				return false;
			}
			ondemand::object EntityObject;
			if (Element.get_object().get(EntityObject) != SUCCESS)
			{
				continue; // non-object element: DOM skipped it without adding an entity
			}

			FMobiusAgentEntityData& Entity = OutEntities.AddDefaulted_GetRef();
			for (auto FieldResult : EntityObject)
			{
				ondemand::field Field;
				if ((OutHardError = std::move(FieldResult).get(Field)) != SUCCESS)
				{
					return false;
				}
				std::string_view Key;
				if ((OutHardError = Field.unescaped_key().get(Key)) != SUCCESS)
				{
					return false;
				}
				ondemand::value Value = Field.value();

				if (Key == "id")
				{
					if (!ReadInt32Field(Value, Entity.Id, OutHardError)) { return false; }
				}
				else if (Key == "name")
				{
					if (!ReadStringField(Value, Entity.Name, OutHardError)) { return false; }
				}
				else if (Key == "simTimeS")
				{
					if (!ReadSimTimeField(Value, Entity.SimTimeS, OutHardError)) { return false; }
				}
				else if (Key == "max_speed")
				{
					if (!ReadFloatField(Value, Entity.MaxSpeed, OutHardError)) { return false; }
				}
				else if (Key == "m_plane")
				{
					if (!ReadStringField(Value, Entity.MPlane, OutHardError)) { return false; }
				}
				else if (Key == "map")
				{
					if (!ReadInt32Field(Value, Entity.Map, OutHardError)) { return false; }
				}
			}
		}
		return true;
	}

	bool ParseSampleObject(ondemand::object& SampleObject, FMobiusAgentSampleData& Sample, error_code& OutHardError)
	{
		for (auto FieldResult : SampleObject)
		{
			ondemand::field Field;
			if ((OutHardError = std::move(FieldResult).get(Field)) != SUCCESS)
			{
				return false;
			}
			std::string_view Key;
			if ((OutHardError = Field.unescaped_key().get(Key)) != SUCCESS)
			{
				return false;
			}
			ondemand::value Value = Field.value();

			if (Key == "entity")
			{
				if (!ReadInt32Field(Value, Sample.EntityId, OutHardError)) { return false; }
			}
			else if (Key == "rotation")
			{
				if (!ReadFloatField(Value, Sample.Rotation, OutHardError)) { return false; }
			}
			else if (Key == "speed")
			{
				if (!ReadFloatField(Value, Sample.Speed, OutHardError)) { return false; }
			}
			else if (Key == "mode")
			{
				if (!ReadStringField(Value, Sample.Mode, OutHardError)) { return false; }
			}
			else if (Key == "position")
			{
				ondemand::object PositionObject;
				if (Value.get_object().get(PositionObject) != SUCCESS)
				{
					continue; // wrong-typed position: DOM left the defaults
				}
				for (auto PositionFieldResult : PositionObject)
				{
					ondemand::field PositionField;
					if ((OutHardError = std::move(PositionFieldResult).get(PositionField)) != SUCCESS)
					{
						return false;
					}
					std::string_view PositionKey;
					if ((OutHardError = PositionField.unescaped_key().get(PositionKey)) != SUCCESS)
					{
						return false;
					}
					ondemand::value PositionValue = PositionField.value();
					if (PositionKey == "x")
					{
						if (!ReadFloatField(PositionValue, Sample.PositionX, OutHardError)) { return false; }
					}
					else if (PositionKey == "y")
					{
						if (!ReadFloatField(PositionValue, Sample.PositionY, OutHardError)) { return false; }
					}
					else if (PositionKey == "z")
					{
						if (!ReadFloatField(PositionValue, Sample.PositionZ, OutHardError)) { return false; }
					}
				}
			}
		}
		return true;
	}

	bool ParseSimulationArray(ondemand::value& SimulationValue, TArray<FMobiusAgentSampleData>& OutSamples, error_code& OutHardError)
	{
		ondemand::array TimestepsArray;
		const error_code ArrayError = SimulationValue.get_array().get(TimestepsArray);
		if (ArrayError == INCORRECT_TYPE)
		{
			return true;
		}
		if (ArrayError != SUCCESS)
		{
			OutHardError = ArrayError;
			return false;
		}

		// Every element of "simulation" advances the timestep index, valid or not — the DOM loop
		// indexed by array position.
		int32 TimestepIndex = -1;
		for (auto TimestepResult : TimestepsArray)
		{
			++TimestepIndex;
			ondemand::value TimestepValue;
			if ((OutHardError = std::move(TimestepResult).get(TimestepValue)) != SUCCESS)
			{
				return false;
			}
			ondemand::object TimestepObject;
			if (TimestepValue.get_object().get(TimestepObject) != SUCCESS)
			{
				continue;
			}

			for (auto FieldResult : TimestepObject)
			{
				ondemand::field Field;
				if ((OutHardError = std::move(FieldResult).get(Field)) != SUCCESS)
				{
					return false;
				}
				std::string_view Key;
				if ((OutHardError = Field.unescaped_key().get(Key)) != SUCCESS)
				{
					return false;
				}
				if (Key != "samples")
				{
					continue;
				}

				ondemand::value SamplesValue = Field.value();
				ondemand::array SamplesArray;
				if (SamplesValue.get_array().get(SamplesArray) != SUCCESS)
				{
					continue; // wrong-typed "samples": DOM skipped the timestep
				}
				for (auto SampleResult : SamplesArray)
				{
					ondemand::value SampleValue;
					if ((OutHardError = std::move(SampleResult).get(SampleValue)) != SUCCESS)
					{
						return false;
					}
					ondemand::object SampleObject;
					if (SampleValue.get_object().get(SampleObject) != SUCCESS)
					{
						continue;
					}
					FMobiusAgentSampleData& Sample = OutSamples.AddDefaulted_GetRef();
					Sample.TimestepIndex = TimestepIndex;
					if (!ParseSampleObject(SampleObject, Sample, OutHardError))
					{
						return false;
					}
				}
			}
		}
		return true;
	}
}

bool FMobiusAgentDataImporter::ParseJsonWithSimdjson(const FString& FilePath, FMobiusAgentSimulationData& OutData, FString* OutError)
{
	const double StartSeconds = FPlatformTime::Seconds();

	// Raw bytes with simdjson's required tail padding allocated up front (Reserve before
	// SetNumUninitialized so no second allocation/copy of a multi-hundred-MB file happens).
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*FilePath, FILEREAD_Silent));
	if (!Reader)
	{
		MobiusJsonImport::SetError(OutError, FString::Printf(TEXT("Unable to read JSON data from: %s"), *FilePath));
		return false;
	}
	const int64 FileSize = Reader->TotalSize();
	if (FileSize <= 0)
	{
		MobiusJsonImport::SetError(OutError, FString::Printf(TEXT("JSON file is empty: %s"), *FilePath));
		return false;
	}

	TArray64<uint8> Bytes;
	Bytes.Reserve(FileSize + simdjson::SIMDJSON_PADDING);
	Bytes.SetNumUninitialized(FileSize);
	Reader->Serialize(Bytes.GetData(), FileSize);
	const bool bReadOk = Reader->Close() && !Reader->IsError();
	Reader.Reset();
	if (!bReadOk)
	{
		MobiusJsonImport::SetError(OutError, FString::Printf(TEXT("Unable to read JSON data from: %s"), *FilePath));
		return false;
	}

	// simdjson consumes UTF-8 only. Skip a UTF-8 BOM; hand anything UTF-16 to the pull-parser
	// fallback (LoadFileToString is BOM/encoding aware).
	int64 ByteOffset = 0;
	if (FileSize >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF)
	{
		ByteOffset = 3;
	}
	else if (FileSize >= 2 && ((Bytes[0] == 0xFF && Bytes[1] == 0xFE) || (Bytes[0] == 0xFE && Bytes[1] == 0xFF)))
	{
		MobiusJsonImport::SetError(OutError, TEXT("UTF-16 BOM detected - not a UTF-8 document"));
		return false;
	}
	const int64 DocumentLength = FileSize - ByteOffset;
	if (DocumentLength <= 0 || DocumentLength > static_cast<int64>(0xFFFFFFFFu) - simdjson::SIMDJSON_PADDING)
	{
		MobiusJsonImport::SetError(OutError, FString::Printf(TEXT("JSON document size %lld outside simdjson's supported range"), DocumentLength));
		return false;
	}

	OutData = FMobiusAgentSimulationData();
	OutData.SourceFormat = EMobiusAgentFileFormat::Json;

	const simdjson::padded_string_view PaddedView(
		reinterpret_cast<const char*>(Bytes.GetData() + ByteOffset),
		static_cast<size_t>(DocumentLength),
		static_cast<size_t>(Bytes.Max() - ByteOffset));

	simdjson::ondemand::parser Parser;
	simdjson::ondemand::document Document;
	simdjson::error_code HardError = Parser.iterate(PaddedView).get(Document);
	simdjson::ondemand::object Root;
	if (HardError == simdjson::SUCCESS)
	{
		HardError = Document.get_object().get(Root);
	}

	if (HardError == simdjson::SUCCESS)
	{
		for (auto FieldResult : Root)
		{
			simdjson::ondemand::field Field;
			if ((HardError = std::move(FieldResult).get(Field)) != simdjson::SUCCESS)
			{
				break;
			}
			std::string_view Key;
			if ((HardError = Field.unescaped_key().get(Key)) != simdjson::SUCCESS)
			{
				break;
			}
			simdjson::ondemand::value Value = Field.value();

			if (Key == "metadata")
			{
				if (!ParseMetadataObject(Value, OutData.Metadata, HardError)) { break; }
			}
			else if (Key == "entities")
			{
				if (!ParseEntitiesArray(Value, OutData.Entities, HardError)) { break; }
			}
			else if (Key == "simulation")
			{
				if (!ParseSimulationArray(Value, OutData.Samples, HardError)) { break; }
			}
		}
	}

	if (HardError != simdjson::SUCCESS)
	{
		MobiusJsonImport::SetError(OutError, FString::Printf(TEXT("simdjson: %hs"), simdjson::error_message(HardError)));
		OutData = FMobiusAgentSimulationData(); // no partial results into the fallback parse
		return false;
	}

	MobiusJsonImport::FinalizeParsedJson(OutData);

	const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
	const double Megabytes = static_cast<double>(FileSize) / (1024.0 * 1024.0);
	UE_LOG(LogMobiusAgentDataImporter, Log,
	       TEXT("simdjson JSON parse: %.1f MB in %.2f s (%.0f MB/s) - %d entities, %d samples: %s"),
	       Megabytes, ElapsedSeconds, ElapsedSeconds > 0.0 ? Megabytes / ElapsedSeconds : 0.0,
	       OutData.Entities.Num(), OutData.Samples.Num(), *FPaths::GetCleanFilename(FilePath));
	return true;
}
