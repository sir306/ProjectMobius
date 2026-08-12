// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

/**
 * A7 fallback JSON parse path: the engine's TJsonReader as a PULL parser (token stream), writing
 * straight into the final dense arrays. Replaces the FJsonSerializer DOM path entirely — even the
 * fallback never builds the ~one-TSharedPtr-per-node DOM that caused the +4 GB import transient.
 * Still goes through LoadFileToString, so any encoding UE can detect (UTF-16, BOMs) works here;
 * that is exactly why this parser backs up simdjson, which is UTF-8 only.
 *
 * Must stay semantics-identical to MobiusJsonSimdParser.cpp — see MobiusJsonParserCommon.h; the
 * ProjectMobius.SimData.JsonParserParity test compares the two bit-for-bit.
 */

#include "MobiusJsonParserCommon.h"

#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"

namespace
{
	using FMobiusJsonReader = TJsonReader<TCHAR>;

	/** Consume the remainder of the container just opened (call right after ObjectStart/ArrayStart). */
	bool SkipContainer(FMobiusJsonReader& Reader)
	{
		int32 Depth = 1;
		EJsonNotation Notation;
		while (Depth > 0 && Reader.ReadNext(Notation))
		{
			switch (Notation)
			{
			case EJsonNotation::ObjectStart:
			case EJsonNotation::ArrayStart:
				++Depth;
				break;
			case EJsonNotation::ObjectEnd:
			case EJsonNotation::ArrayEnd:
				--Depth;
				break;
			case EJsonNotation::Error:
				return false;
			default:
				break;
			}
		}
		return Depth == 0;
	}

	/** Inside an object: dispose of the value belonging to the notation just read (containers are
	 *  skipped whole, scalars are already consumed). */
	bool SkipValue(FMobiusJsonReader& Reader, EJsonNotation Notation)
	{
		if (Notation == EJsonNotation::ObjectStart || Notation == EJsonNotation::ArrayStart)
		{
			return SkipContainer(Reader);
		}
		return Notation != EJsonNotation::Error;
	}

	bool ParseMetadataObject(FMobiusJsonReader& Reader, FMobiusAgentSimulationMetadata& OutMetadata)
	{
		bool bSawCanonicalIsSI = false;
		bool bSawCanonicalIsDeg = false;

		EJsonNotation Notation;
		while (Reader.ReadNext(Notation))
		{
			if (Notation == EJsonNotation::ObjectEnd)
			{
				return true;
			}
			if (Notation == EJsonNotation::Error)
			{
				return false;
			}

			const FString& Key = Reader.GetIdentifier();
			if (Notation == EJsonNotation::Number)
			{
				const double Double = Reader.GetValueAsNumber();
				if (Key == TEXT("duration"))
				{
					OutMetadata.Duration = static_cast<float>(Double);
				}
				else if (Key == TEXT("sampling_rate"))
				{
					OutMetadata.SamplingRate = static_cast<float>(Double);
				}
				else if (Key == TEXT("max_num_entities"))
				{
					MobiusJsonImport::NumberToInt32(Double, OutMetadata.MaxNumEntities);
				}
			}
			else if (Notation == EJsonNotation::Boolean)
			{
				const bool Bool = Reader.GetValueAsBoolean();
				// Canonical spelling wins regardless of file order, like the DOM's
				// TryGetBoolField("isSI") || TryGetBoolField("is_si").
				if (Key == TEXT("isSI"))
				{
					OutMetadata.bIsSI = Bool;
					bSawCanonicalIsSI = true;
				}
				else if (Key == TEXT("is_si"))
				{
					if (!bSawCanonicalIsSI)
					{
						OutMetadata.bIsSI = Bool;
					}
				}
				else if (Key == TEXT("isDeg"))
				{
					OutMetadata.bIsDeg = Bool;
					bSawCanonicalIsDeg = true;
				}
				else if (Key == TEXT("is_deg"))
				{
					if (!bSawCanonicalIsDeg)
					{
						OutMetadata.bIsDeg = Bool;
					}
				}
			}
			else if (!SkipValue(Reader, Notation))
			{
				return false;
			}
		}
		return false; // ran out of tokens before ObjectEnd
	}

	bool ParseEntityObject(FMobiusJsonReader& Reader, FMobiusAgentEntityData& OutEntity)
	{
		EJsonNotation Notation;
		while (Reader.ReadNext(Notation))
		{
			if (Notation == EJsonNotation::ObjectEnd)
			{
				return true;
			}
			if (Notation == EJsonNotation::Error)
			{
				return false;
			}

			const FString& Key = Reader.GetIdentifier();
			if (Notation == EJsonNotation::Number)
			{
				const double Double = Reader.GetValueAsNumber();
				if (Key == TEXT("id"))
				{
					MobiusJsonImport::NumberToInt32(Double, OutEntity.Id);
				}
				else if (Key == TEXT("simTimeS"))
				{
					OutEntity.SimTimeS = static_cast<float>(Double);
				}
				else if (Key == TEXT("max_speed"))
				{
					OutEntity.MaxSpeed = static_cast<float>(Double);
				}
				else if (Key == TEXT("map"))
				{
					MobiusJsonImport::NumberToInt32(Double, OutEntity.Map);
				}
			}
			else if (Notation == EJsonNotation::String)
			{
				if (Key == TEXT("name"))
				{
					OutEntity.Name = Reader.GetValueAsString();
				}
				else if (Key == TEXT("simTimeS"))
				{
					OutEntity.SimTimeS = FCString::Atof(*Reader.GetValueAsString());
				}
				else if (Key == TEXT("m_plane"))
				{
					OutEntity.MPlane = Reader.GetValueAsString();
				}
			}
			else if (!SkipValue(Reader, Notation))
			{
				return false;
			}
		}
		return false;
	}

	bool ParseEntitiesArray(FMobiusJsonReader& Reader, TArray<FMobiusAgentEntityData>& OutEntities)
	{
		EJsonNotation Notation;
		while (Reader.ReadNext(Notation))
		{
			if (Notation == EJsonNotation::ArrayEnd)
			{
				return true;
			}
			if (Notation == EJsonNotation::ObjectStart)
			{
				if (!ParseEntityObject(Reader, OutEntities.AddDefaulted_GetRef()))
				{
					return false;
				}
			}
			else if (!SkipValue(Reader, Notation))
			{
				return false; // non-object elements: skipped without adding an entity
			}
		}
		return false;
	}

	bool ParsePositionObject(FMobiusJsonReader& Reader, FMobiusAgentSampleData& Sample)
	{
		EJsonNotation Notation;
		while (Reader.ReadNext(Notation))
		{
			if (Notation == EJsonNotation::ObjectEnd)
			{
				return true;
			}
			if (Notation == EJsonNotation::Number)
			{
				const FString& Key = Reader.GetIdentifier();
				const float Float = static_cast<float>(Reader.GetValueAsNumber());
				if (Key == TEXT("x"))
				{
					Sample.PositionX = Float;
				}
				else if (Key == TEXT("y"))
				{
					Sample.PositionY = Float;
				}
				else if (Key == TEXT("z"))
				{
					Sample.PositionZ = Float;
				}
			}
			else if (!SkipValue(Reader, Notation))
			{
				return false;
			}
		}
		return false;
	}

	bool ParseSampleObject(FMobiusJsonReader& Reader, FMobiusAgentSampleData& Sample)
	{
		EJsonNotation Notation;
		while (Reader.ReadNext(Notation))
		{
			if (Notation == EJsonNotation::ObjectEnd)
			{
				return true;
			}
			if (Notation == EJsonNotation::Error)
			{
				return false;
			}

			const FString& Key = Reader.GetIdentifier();
			if (Notation == EJsonNotation::Number)
			{
				const double Double = Reader.GetValueAsNumber();
				if (Key == TEXT("entity"))
				{
					MobiusJsonImport::NumberToInt32(Double, Sample.EntityId);
				}
				else if (Key == TEXT("rotation"))
				{
					Sample.Rotation = static_cast<float>(Double);
				}
				else if (Key == TEXT("speed"))
				{
					Sample.Speed = static_cast<float>(Double);
				}
			}
			else if (Notation == EJsonNotation::String && Key == TEXT("mode"))
			{
				Sample.Mode = Reader.GetValueAsString();
			}
			else if (Notation == EJsonNotation::ObjectStart && Key == TEXT("position"))
			{
				if (!ParsePositionObject(Reader, Sample))
				{
					return false;
				}
			}
			else if (!SkipValue(Reader, Notation))
			{
				return false;
			}
		}
		return false;
	}

	bool ParseSamplesArray(FMobiusJsonReader& Reader, int32 TimestepIndex, TArray<FMobiusAgentSampleData>& OutSamples)
	{
		EJsonNotation Notation;
		while (Reader.ReadNext(Notation))
		{
			if (Notation == EJsonNotation::ArrayEnd)
			{
				return true;
			}
			if (Notation == EJsonNotation::ObjectStart)
			{
				FMobiusAgentSampleData& Sample = OutSamples.AddDefaulted_GetRef();
				Sample.TimestepIndex = TimestepIndex;
				if (!ParseSampleObject(Reader, Sample))
				{
					return false;
				}
			}
			else if (!SkipValue(Reader, Notation))
			{
				return false;
			}
		}
		return false;
	}

	bool ParseTimestepObject(FMobiusJsonReader& Reader, int32 TimestepIndex, TArray<FMobiusAgentSampleData>& OutSamples)
	{
		EJsonNotation Notation;
		while (Reader.ReadNext(Notation))
		{
			if (Notation == EJsonNotation::ObjectEnd)
			{
				return true;
			}
			if (Notation == EJsonNotation::ArrayStart && Reader.GetIdentifier() == TEXT("samples"))
			{
				if (!ParseSamplesArray(Reader, TimestepIndex, OutSamples))
				{
					return false;
				}
			}
			else if (!SkipValue(Reader, Notation))
			{
				return false;
			}
		}
		return false;
	}

	bool ParseSimulationArray(FMobiusJsonReader& Reader, TArray<FMobiusAgentSampleData>& OutSamples)
	{
		// Every element advances the timestep index, valid or not — the DOM loop indexed by
		// array position.
		int32 TimestepIndex = -1;
		EJsonNotation Notation;
		while (Reader.ReadNext(Notation))
		{
			if (Notation == EJsonNotation::ArrayEnd)
			{
				return true;
			}
			++TimestepIndex;
			if (Notation == EJsonNotation::ObjectStart)
			{
				if (!ParseTimestepObject(Reader, TimestepIndex, OutSamples))
				{
					return false;
				}
			}
			else if (!SkipValue(Reader, Notation))
			{
				return false;
			}
		}
		return false;
	}
}

bool FMobiusAgentDataImporter::ParseJsonWithPullParser(const FString& FilePath, FMobiusAgentSimulationData& OutData, FString* OutError)
{
	const double StartSeconds = FPlatformTime::Seconds();

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		MobiusJsonImport::SetError(OutError, FString::Printf(TEXT("Unable to read JSON data from: %s"), *FilePath));
		return false;
	}

	OutData = FMobiusAgentSimulationData();
	OutData.SourceFormat = EMobiusAgentFileFormat::Json;

	// View reader: no second copy of a multi-hundred-MB string. JsonString must outlive Reader.
	const TSharedRef<FMobiusJsonReader> ReaderRef = TJsonReaderFactory<TCHAR>::CreateFromView(JsonString);
	FMobiusJsonReader& Reader = ReaderRef.Get();

	EJsonNotation Notation;
	bool bParsedOk = Reader.ReadNext(Notation) && Notation == EJsonNotation::ObjectStart;
	while (bParsedOk)
	{
		if (!Reader.ReadNext(Notation))
		{
			bParsedOk = false;
			break;
		}
		if (Notation == EJsonNotation::ObjectEnd)
		{
			break; // root object complete
		}
		if (Notation == EJsonNotation::Error)
		{
			bParsedOk = false;
			break;
		}

		const FString& Key = Reader.GetIdentifier();
		if (Notation == EJsonNotation::ObjectStart && Key == TEXT("metadata"))
		{
			bParsedOk = ParseMetadataObject(Reader, OutData.Metadata);
		}
		else if (Notation == EJsonNotation::ArrayStart && Key == TEXT("entities"))
		{
			bParsedOk = ParseEntitiesArray(Reader, OutData.Entities);
		}
		else if (Notation == EJsonNotation::ArrayStart && Key == TEXT("simulation"))
		{
			bParsedOk = ParseSimulationArray(Reader, OutData.Samples);
		}
		else
		{
			bParsedOk = SkipValue(Reader, Notation);
		}
	}

	if (!bParsedOk)
	{
		const FString& ReaderError = Reader.GetErrorMessage();
		MobiusJsonImport::SetError(OutError, FString::Printf(TEXT("Failed to deserialize JSON data from: %s%s%s"),
			*FilePath, ReaderError.IsEmpty() ? TEXT("") : TEXT(" - "), *ReaderError));
		OutData = FMobiusAgentSimulationData();
		return false;
	}

	MobiusJsonImport::FinalizeParsedJson(OutData);

	const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
	const double Megabytes = static_cast<double>(JsonString.Len()) * sizeof(TCHAR) / (1024.0 * 1024.0);
	UE_LOG(LogMobiusAgentDataImporter, Log,
	       TEXT("pull-parser JSON parse: %.1f MB (UTF-16) in %.2f s (%.0f MB/s) - %d entities, %d samples: %s"),
	       Megabytes, ElapsedSeconds, ElapsedSeconds > 0.0 ? Megabytes / ElapsedSeconds : 0.0,
	       OutData.Entities.Num(), OutData.Samples.Num(), *FPaths::GetCleanFilename(FilePath));
	return true;
}
