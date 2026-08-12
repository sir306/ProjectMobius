// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

#include "CoreMinimal.h"
#include "MobiusAgentDataImporter.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMobiusAgentDataImporter, Log, All);

/**
 * Shared pieces of the two A7 JSON parse paths (simdjson SAX + TJsonReader pull-parser). Both
 * parsers must produce BIT-IDENTICAL FMobiusAgentSimulationData for the same file — the
 * ProjectMobius.SimData.JsonParserParity automation test enforces it — so every value conversion
 * they share lives here rather than being written twice.
 *
 * Deliberate change from the FJsonSerializer DOM path these replace: field extraction is now
 * STRICTLY typed (a number field is filled only from a JSON number, a string field only from a
 * JSON string, a bool only from a JSON bool — plus the long-standing simTimeS string|number
 * special case). The DOM's TryGet* would silently coerce across types ("42" -> 42, 1 -> "1",
 * any string -> bool); no known Mobius source file relies on that, and strictness is what makes
 * two independent parsers provably agree.
 */
namespace MobiusJsonImport
{
	/** cvar mobius.Import.SimdJson (default 1; 0 = always use the engine pull-parser). */
	bool IsSimdJsonEnabled();

	inline void SetError(FString* OutError, const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
	}

	/** Mirrors the engine's FJsonValue TryConvertNumber<int32> exactly (range gate, then
	 *  RoundHalfFromZero) so int fields convert precisely like the DOM path they replaced. */
	inline bool NumberToInt32(double Value, int32& OutNumber)
	{
		if (Value >= static_cast<double>(TNumericLimits<int32>::Min()) &&
		    Value <= static_cast<double>(TNumericLimits<int32>::Max()))
		{
			OutNumber = static_cast<int32>(FMath::RoundHalfFromZero(Value));
			return true;
		}
		return false;
	}

	/** Shared post-parse step (both parsers, same as the old DOM path). */
	inline void FinalizeParsedJson(FMobiusAgentSimulationData& Data)
	{
		if (Data.Metadata.MaxNumEntities == 0)
		{
			Data.Metadata.MaxNumEntities = Data.Entities.Num();
		}
	}
}
