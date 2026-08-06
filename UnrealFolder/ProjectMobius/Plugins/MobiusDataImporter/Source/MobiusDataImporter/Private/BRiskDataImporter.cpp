// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "BRiskDataImporter.h"

#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "XmlFile.h"

DEFINE_LOG_CATEGORY_STATIC(LogBRiskDataImporter, Log, All);

namespace
{
	void SetBRiskImportError(FString* OutError, const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
	}

	FString TrimCell(const FString& InCell)
	{
		FString Result = InCell;
		Result.TrimStartAndEndInline();
		return Result;
	}

	bool TryParseDouble(const FString& InValue, double& OutValue)
	{
		const FString Trimmed = TrimCell(InValue);
		if (Trimmed.IsEmpty())
		{
			return false;
		}
		return LexTryParseString(OutValue, *Trimmed);
	}

	TArray<FString> SplitWhitespace(const FString& InLine)
	{
		TArray<FString> Tokens;
		InLine.ParseIntoArrayWS(Tokens);
		for (FString& Token : Tokens)
		{
			Token = TrimCell(Token);
		}
		return Tokens;
	}

	/**
	 * Split a numeric .smv data line into number tokens.
	 *
	 * B-Risk writes these lines in a fixed-width Fortran scientific format, where a
	 * negative value's sign consumes the separating space:
	 *
	 *     " 3.2505E+000-1.9019E+001 0.0000E+000"     <- a room origin, 3 values
	 *
	 * Whitespace splitting yields two tokens there, not three, so any block with a
	 * negative coordinate is silently rejected. Split on whitespace AND on any '+'/'-'
	 * that begins a new number - that is, a sign not immediately preceded by an
	 * exponent marker (E/e/D/d), which would make it the exponent's own sign.
	 */
	TArray<FString> SplitNumericTokens(const FString& InLine)
	{
		auto IsExponentMarker = [](TCHAR Character)
		{
			return Character == TEXT('E') || Character == TEXT('e')
				|| Character == TEXT('D') || Character == TEXT('d');
		};

		TArray<FString> Tokens;
		const int32 Length = InLine.Len();
		int32 TokenStart = INDEX_NONE;

		for (int32 Index = 0; Index < Length; ++Index)
		{
			const TCHAR Character = InLine[Index];

			if (FChar::IsWhitespace(Character))
			{
				if (TokenStart != INDEX_NONE)
				{
					Tokens.Add(InLine.Mid(TokenStart, Index - TokenStart));
					TokenStart = INDEX_NONE;
				}
				continue;
			}

			const bool bIsSign = (Character == TEXT('+') || Character == TEXT('-'));
			if (bIsSign && TokenStart != INDEX_NONE && !IsExponentMarker(InLine[Index - 1]))
			{
				Tokens.Add(InLine.Mid(TokenStart, Index - TokenStart));
				TokenStart = Index;
				continue;
			}

			if (TokenStart == INDEX_NONE)
			{
				TokenStart = Index;
			}
		}

		if (TokenStart != INDEX_NONE)
		{
			Tokens.Add(InLine.Mid(TokenStart, Length - TokenStart));
		}

		return Tokens;
	}

	void SplitCsvRow(const FString& InLine, TArray<FString>& OutCells)
	{
		OutCells.Reset();
		InLine.ParseIntoArray(OutCells, TEXT(","), false);
		for (FString& Cell : OutCells)
		{
			Cell = TrimCell(Cell);
		}

		while (OutCells.Num() > 0 && OutCells.Last().IsEmpty())
		{
			OutCells.Pop();
		}
	}

	int32 FindNextDataLine(const TArray<FString>& Lines, int32 StartIndex)
	{
		for (int32 LineIndex = StartIndex; LineIndex < Lines.Num(); ++LineIndex)
		{
			if (!TrimCell(Lines[LineIndex]).IsEmpty())
			{
				return LineIndex;
			}
		}
		return INDEX_NONE;
	}

	bool TryParseRoom(const TArray<FString>& Lines, int32& InOutIndex, FBRiskRoomGeometry& OutRoom)
	{
		const TArray<FString> RoomTokens = SplitWhitespace(TrimCell(Lines[InOutIndex]));
		if (RoomTokens.Num() < 2)
		{
			return false;
		}

		OutRoom = FBRiskRoomGeometry();
		OutRoom.RoomId = FCString::Atoi(*RoomTokens[1]);

		const int32 DimsIndex = FindNextDataLine(Lines, InOutIndex + 1);
		const int32 OriginIndex = DimsIndex != INDEX_NONE ? FindNextDataLine(Lines, DimsIndex + 1) : INDEX_NONE;
		if (DimsIndex == INDEX_NONE || OriginIndex == INDEX_NONE)
		{
			return false;
		}

		const TArray<FString> DimTokens = SplitNumericTokens(TrimCell(Lines[DimsIndex]));
		const TArray<FString> OriginTokens = SplitNumericTokens(TrimCell(Lines[OriginIndex]));
		if (DimTokens.Num() < 3 || OriginTokens.Num() < 3)
		{
			UE_LOG(LogBRiskDataImporter, Warning,
				TEXT("B-Risk ROOM %d has a malformed geometry block (dims=%d values, origin=%d values, expected 3 each): ")
				TEXT("dims '%s' origin '%s'"),
				OutRoom.RoomId, DimTokens.Num(), OriginTokens.Num(),
				*TrimCell(Lines[DimsIndex]), *TrimCell(Lines[OriginIndex]));
			return false;
		}

		double Value = 0.0;
		if (TryParseDouble(DimTokens[0], Value)) OutRoom.Size.X = Value;
		if (TryParseDouble(DimTokens[1], Value)) OutRoom.Size.Y = Value;
		if (TryParseDouble(DimTokens[2], Value)) OutRoom.Size.Z = Value;

		if (TryParseDouble(OriginTokens[0], Value)) OutRoom.Origin.X = Value;
		if (TryParseDouble(OriginTokens[1], Value)) OutRoom.Origin.Y = Value;
		if (TryParseDouble(OriginTokens[2], Value)) OutRoom.Origin.Z = Value;

		InOutIndex = OriginIndex;
		return true;
	}

	bool TryParseFire(const TArray<FString>& Lines, int32& InOutIndex, FBRiskFireGeometry& OutFire)
	{
		const int32 DataIndex = FindNextDataLine(Lines, InOutIndex + 1);
		if (DataIndex == INDEX_NONE)
		{
			return false;
		}

		const TArray<FString> Tokens = SplitNumericTokens(TrimCell(Lines[DataIndex]));
		if (Tokens.Num() < 4)
		{
			return false;
		}

		OutFire = FBRiskFireGeometry();
		OutFire.RoomId = FCString::Atoi(*Tokens[0]);

		double Value = 0.0;
		if (TryParseDouble(Tokens[1], Value)) OutFire.Location.X = Value;
		if (TryParseDouble(Tokens[2], Value)) OutFire.Location.Y = Value;
		if (TryParseDouble(Tokens[3], Value)) OutFire.Location.Z = Value;

		InOutIndex = DataIndex;
		return true;
	}

	bool TryParseVentGeom(const TArray<FString>& Lines, int32& InOutIndex, FBRiskVentGeometry& OutVent)
	{
		const int32 DataIndex = FindNextDataLine(Lines, InOutIndex + 1);
		if (DataIndex == INDEX_NONE)
		{
			return false;
		}

		const TArray<FString> Tokens = SplitNumericTokens(TrimCell(Lines[DataIndex]));
		if (Tokens.Num() < 7)
		{
			return false;
		}

		OutVent = FBRiskVentGeometry();
		OutVent.FromRoomId = FCString::Atoi(*Tokens[0]);
		OutVent.ToRoomId = FCString::Atoi(*Tokens[1]);
		OutVent.Face = FCString::Atoi(*Tokens[2]);

		double Value = 0.0;
		if (TryParseDouble(Tokens[3], Value)) OutVent.Width = Value;
		if (TryParseDouble(Tokens[4], Value)) OutVent.Offset = Value;
		if (TryParseDouble(Tokens[5], Value)) OutVent.SillHeight = Value;

		// Token[6] is the HEAD height (sill + opening height), not the opening height.
		// Confirmed against vents.xml (window: sill 0.9, height 1.35, token[6] 2.25) and
		// against B-Risk's own vent area in zone.csv (HVENT = 1.05 * 1.35 = 1.4175 m^2).
		// Doors have sill 0, which is why head == height hid this until a window appeared.
		if (TryParseDouble(Tokens[6], Value))
		{
			const double HeadHeight = Value;
			OutVent.Height = HeadHeight - OutVent.SillHeight;
			if (OutVent.Height <= 0.0)
			{
				UE_LOG(LogBRiskDataImporter, Warning,
					TEXT("B-Risk VENTGEOM (from room %d to room %d, face %d) has head %.3f m at or below sill %.3f m; ")
					TEXT("treating the opening as degenerate."),
					OutVent.FromRoomId, OutVent.ToRoomId, OutVent.Face, HeadHeight, OutVent.SillHeight);
				OutVent.Height = 0.0;
			}
		}

		InOutIndex = DataIndex;
		return true;
	}

	bool ParseZoneCsv(const FString& CsvPath, FBRiskZoneTable& OutTable, FString* OutError)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *CsvPath))
		{
			SetBRiskImportError(OutError, FString::Printf(TEXT("Unable to read B-Risk zone CSV: %s"), *CsvPath));
			return false;
		}
		if (Lines.Num() < 2)
		{
			SetBRiskImportError(OutError, FString::Printf(
				TEXT("Invalid B-Risk zone CSV (expected at least 2 header lines): %s"), *CsvPath));
			return false;
		}

		OutTable = FBRiskZoneTable();
		OutTable.SourceCsvPath = CsvPath;

		TArray<FString> Units;
		TArray<FString> Headers;
		SplitCsvRow(Lines[0], Units);
		SplitCsvRow(Lines[1], Headers);

		if (Headers.Num() == 0)
		{
			SetBRiskImportError(OutError, FString::Printf(TEXT("Invalid B-Risk zone CSV headers: %s"), *CsvPath));
			return false;
		}

		int32 TimeColumnIndex = INDEX_NONE;
		for (int32 HeaderIndex = 0; HeaderIndex < Headers.Num(); ++HeaderIndex)
		{
			if (Headers[HeaderIndex].Equals(TEXT("Time"), ESearchCase::IgnoreCase))
			{
				TimeColumnIndex = HeaderIndex;
				break;
			}
		}
		if (TimeColumnIndex == INDEX_NONE)
		{
			SetBRiskImportError(OutError, FString::Printf(
				TEXT("B-Risk zone CSV is missing required Time column: %s"), *CsvPath));
			return false;
		}

		TArray<int32> ColumnToSeries;
		ColumnToSeries.Init(INDEX_NONE, Headers.Num());
		for (int32 HeaderIndex = 0; HeaderIndex < Headers.Num(); ++HeaderIndex)
		{
			if (HeaderIndex == TimeColumnIndex || Headers[HeaderIndex].IsEmpty())
			{
				continue;
			}

			FBRiskSeries& Series = OutTable.Series.AddDefaulted_GetRef();
			Series.Name = Headers[HeaderIndex];
			Series.Unit = Units.IsValidIndex(HeaderIndex) ? Units[HeaderIndex] : FString();
			ColumnToSeries[HeaderIndex] = OutTable.Series.Num() - 1;
		}
		if (OutTable.Series.Num() == 0)
		{
			SetBRiskImportError(OutError, FString::Printf(
				TEXT("B-Risk zone CSV has no non-Time data series: %s"), *CsvPath));
			return false;
		}

		for (int32 LineIndex = 2; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString TrimmedLine = TrimCell(Lines[LineIndex]);
			if (TrimmedLine.IsEmpty())
			{
				continue;
			}

			TArray<FString> Cells;
			SplitCsvRow(TrimmedLine, Cells);
			if (Cells.Num() == 0)
			{
				continue;
			}

			double TimeValue = 0.0;
			if (!Cells.IsValidIndex(TimeColumnIndex) || !TryParseDouble(Cells[TimeColumnIndex], TimeValue))
			{
				SetBRiskImportError(OutError, FString::Printf(
					TEXT("Malformed Time value in B-Risk zone CSV %s at row %d"),
					*CsvPath, LineIndex + 1));
				return false;
			}
			OutTable.TimeSeconds.Add(TimeValue);

			for (int32 HeaderIndex = 0; HeaderIndex < ColumnToSeries.Num(); ++HeaderIndex)
			{
				const int32 SeriesIndex = ColumnToSeries[HeaderIndex];
				if (SeriesIndex == INDEX_NONE)
				{
					continue;
				}

				double Value = 0.0;
				if (!Cells.IsValidIndex(HeaderIndex) || !TryParseDouble(Cells[HeaderIndex], Value))
				{
					SetBRiskImportError(OutError, FString::Printf(
						TEXT("Malformed numeric value for column '%s' in B-Risk zone CSV %s at row %d"),
						*Headers[HeaderIndex], *CsvPath, LineIndex + 1));
					return false;
				}
				OutTable.Series[SeriesIndex].Values.Add(Value);
			}
		}

		if (OutTable.TimeSeconds.Num() == 0)
		{
			SetBRiskImportError(OutError, FString::Printf(
				TEXT("B-Risk zone CSV has no data rows: %s"), *CsvPath));
			return false;
		}

		for (const FBRiskSeries& Series : OutTable.Series)
		{
			if (Series.Values.Num() != OutTable.TimeSeconds.Num())
			{
				SetBRiskImportError(OutError, FString::Printf(
					TEXT("B-Risk zone CSV series '%s' has %d samples but Time has %d samples: %s"),
					*Series.Name, Series.Values.Num(), OutTable.TimeSeconds.Num(), *CsvPath));
				return false;
			}
		}

		return true;
	}

	const FXmlNode* FindFirstChildByTag(const FXmlNode* Parent, const FString& Tag)
	{
		if (!Parent)
		{
			return nullptr;
		}

		for (const FXmlNode* Child : Parent->GetChildrenNodes())
		{
			if (Child && Child->GetTag().Equals(Tag, ESearchCase::IgnoreCase))
			{
				return Child;
			}
		}

		return nullptr;
	}

	FString GetChildContent(const FXmlNode* Parent, const FString& Tag)
	{
		if (const FXmlNode* Child = FindFirstChildByTag(Parent, Tag))
		{
			return TrimCell(Child->GetContent());
		}

		return FString();
	}

	double GetChildDouble(const FXmlNode* Parent, const FString& Tag, double DefaultValue = 0.0)
	{
		double Value = DefaultValue;
		const FString Content = GetChildContent(Parent, Tag);
		return TryParseDouble(Content, Value) ? Value : DefaultValue;
	}

	int32 GetChildInt(const FXmlNode* Parent, const FString& Tag, int32 DefaultValue = INDEX_NONE)
	{
		const FString Content = GetChildContent(Parent, Tag);
		return Content.IsEmpty() ? DefaultValue : FCString::Atoi(*Content);
	}

	double GetSprinklerDistributionValue(const FXmlNode* SprinklerNode, const FString& VarName, double DefaultValue = 0.0)
	{
		if (!SprinklerNode)
		{
			return DefaultValue;
		}

		for (const FXmlNode* Child : SprinklerNode->GetChildrenNodes())
		{
			if (!Child || !Child->GetTag().Equals(TEXT("sdistribution"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			if (GetChildContent(Child, TEXT("varname")).Equals(VarName, ESearchCase::IgnoreCase))
			{
				return GetChildDouble(Child, TEXT("value"), DefaultValue);
			}
		}

		return DefaultValue;
	}

	/** Signed area of a closed ring (shoelace). Positive = counter-clockwise. */
	double SignedRingArea(const TArray<FVector2D>& Ring)
	{
		double Twice = 0.0;
		for (int32 i = 0, n = Ring.Num(); i < n; ++i)
		{
			const FVector2D& A = Ring[i];
			const FVector2D& B = Ring[(i + 1) % n];
			Twice += (A.X * B.Y) - (B.X * A.Y);
		}
		return Twice * 0.5;
	}

	/**
	 * Parse the companion Zones-data.json emitted by the OMA Revit add-in and attach each
	 * space's true plan footprint to the matching room.
	 *
	 * Why this file exists: B-Risk collapses every room to an area- and perimeter-equivalent
	 * rectangle (SR282 eq. 1-2), and because L always takes the +sqrt branch it is just the
	 * larger root - so the .smv carries no room orientation and, for non-rectangular spaces,
	 * no real footprint either. This JSON is the only source of both.
	 *
	 * Joined on spaces[].roomNumber == FBRiskRoomGeometry::RoomId. Non-fatal throughout: a
	 * missing or malformed file leaves every room on the legacy Origin/Size rectangle.
	 */
	/**
	 * Replace the VENTGEOM-derived vent list with the openings[] array when the Revit add-in emitted
	 * one. REPLACE, not merge, and deliberately so.
	 *
	 * The .smv gives connectivity and modelled sizes but places openings by (face, offset) inside
	 * B-Risk's equivalent rectangle, and both halves of that pair are unusable against a real floor
	 * plan: every VENTGEOM record in both test scenarios carries offset 0, so all vents on a wall
	 * stack at one point, and face is not a wall id at all (face 2 and face 3 each map to three
	 * different normals across these 34 openings). openings[] carries a real centre per vent, so it
	 * is strictly better information about the same set of openings - keeping both and joining them
	 * would only add an ambiguous float match over records that are otherwise identical.
	 *
	 * A count disagreement between the two is reported but does NOT stop the replacement. It is worth
	 * knowing about - the real export is exactly 1:1 - but the alternative is falling back to
	 * placement that is known to be wrong, and if the two files really described different models
	 * the spaces[] room join would already have failed and said so.
	 */
	void ParseZonesDataOpenings(
		const TSharedPtr<FJsonObject>& Root,
		const FString& JsonPath,
		const TArray<FBRiskRoomGeometry>& Rooms,
		TArray<FBRiskVentGeometry>& InOutVents)
	{
		const TArray<TSharedPtr<FJsonValue>>* Openings = nullptr;
		if (!Root->TryGetArrayField(TEXT("openings"), Openings) || !Openings || Openings->Num() == 0)
		{
			// Pre-openings export. Every earlier scenario is this shape; not worth a warning.
			return;
		}

		if (InOutVents.Num() > 0 && Openings->Num() != InOutVents.Num())
		{
			UE_LOG(LogBRiskDataImporter, Warning,
				TEXT("B-Risk zones JSON declares %d openings but the .smv has %d VENTGEOM records (%s). ")
				TEXT("Using the openings anyway - they are the only real placement - but the two files ")
				TEXT("should agree, so check the export."),
				Openings->Num(), InOutVents.Num(), *JsonPath);
		}

		// B-Risk numbers the outside as one past the last room, which is what VENTGEOM's toRoom
		// carries for an exterior opening. Matching it keeps FindRoomById returning null for the
		// outside exactly as before, so adjacency and flow behave identically.
		const int32 ExteriorRoomId = Rooms.Num() + 1;

		TArray<FBRiskVentGeometry> Parsed;
		Parsed.Reserve(Openings->Num());

		for (const TSharedPtr<FJsonValue>& OpeningValue : *Openings)
		{
			const TSharedPtr<FJsonObject> Opening = OpeningValue.IsValid() ? OpeningValue->AsObject() : nullptr;
			if (!Opening.IsValid())
			{
				continue;
			}

			int32 RoomA = INDEX_NONE;
			if (!Opening->TryGetNumberField(TEXT("roomA"), RoomA))
			{
				UE_LOG(LogBRiskDataImporter, Warning, TEXT("B-Risk opening has no 'roomA'; skipped."));
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* Centre = nullptr;
			if (!Opening->TryGetArrayField(TEXT("centre"), Centre) || !Centre || Centre->Num() < 3)
			{
				UE_LOG(LogBRiskDataImporter, Warning,
					TEXT("B-Risk opening in room %d has no usable 'centre'; skipped."), RoomA);
				continue;
			}

			FBRiskVentGeometry Vent;
			Vent.FromRoomId = RoomA;
			Vent.CentreMetres = FVector(
				(*Centre)[0]->AsNumber(), (*Centre)[1]->AsNumber(), (*Centre)[2]->AsNumber());
			Vent.bHasPlacement = true;

			int32 RoomB = INDEX_NONE;
			Opening->TryGetBoolField(TEXT("exterior"), Vent.bExterior);
			Vent.ToRoomId = Opening->TryGetNumberField(TEXT("roomB"), RoomB) ? RoomB : ExteriorRoomId;

			Opening->TryGetNumberField(TEXT("ventId"), Vent.VentId);
			Opening->TryGetNumberField(TEXT("sillHeight"), Vent.SillHeight);
			Opening->TryGetNumberField(TEXT("width"), Vent.PhysicalWidth);
			Opening->TryGetNumberField(TEXT("height"), Vent.PhysicalHeight);
			Opening->TryGetNumberField(TEXT("openTimeS"), Vent.OpenTimeSeconds);
			Opening->TryGetNumberField(TEXT("closeTimeS"), Vent.CloseTimeSeconds);
			// v2 and later. Absent in v1, which leaves it 0 and lets the renderer keep its own default.
			Opening->TryGetNumberField(TEXT("hostThickness"), Vent.HostThicknessMetres);

			// Width and Height keep meaning "what B-Risk simulated", so anything reasoning about flow
			// area is unaffected by this file appearing - their product is the CSV's HVENT. The
			// modelled* fields are those figures; fall back to the true size only when the add-in
			// omitted them, so neither is left at zero and silently culled by the renderer's own
			// zero-size check. Both axes are handled the same way on purpose: filling one from the
			// modelled value and the other from the real one is how HVENT would quietly stop meaning
			// what it says.
			if (!Opening->TryGetNumberField(TEXT("modelledWidth"), Vent.Width))
			{
				Vent.Width = Vent.PhysicalWidth;
			}
			if (!Opening->TryGetNumberField(TEXT("modelledHeight"), Vent.Height))
			{
				Vent.Height = Vent.PhysicalHeight;
			}

			FString TypeName;
			if (Opening->TryGetStringField(TEXT("type"), TypeName))
			{
				if (TypeName.Equals(TEXT("door"), ESearchCase::IgnoreCase))          Vent.Kind = EBRiskVentKind::Door;
				else if (TypeName.Equals(TEXT("window"), ESearchCase::IgnoreCase))   Vent.Kind = EBRiskVentKind::Window;
				else if (TypeName.Equals(TEXT("leakage"), ESearchCase::IgnoreCase))  Vent.Kind = EBRiskVentKind::Leakage;
				else
				{
					UE_LOG(LogBRiskDataImporter, Warning,
						TEXT("B-Risk opening %d has unrecognised type '%s'; treated as unclassified."),
						Vent.VentId, *TypeName);
				}
			}

			// Face is meaningless once a centre exists, and leaving a stale value would invite a
			// future reader to treat it as a fallback. Offset likewise.
			Vent.Face = INDEX_NONE;
			Vent.Offset = 0.0;

			Parsed.Add(MoveTemp(Vent));
		}

		if (Parsed.Num() == 0)
		{
			UE_LOG(LogBRiskDataImporter, Warning,
				TEXT("B-Risk zones JSON had %d openings but none were usable (%s); keeping the .smv vents."),
				Openings->Num(), *JsonPath);
			return;
		}

		int32 Doors = 0, Windows = 0, Leakage = 0;
		for (const FBRiskVentGeometry& Vent : Parsed)
		{
			Doors   += (Vent.Kind == EBRiskVentKind::Door)    ? 1 : 0;
			Windows += (Vent.Kind == EBRiskVentKind::Window)  ? 1 : 0;
			Leakage += (Vent.Kind == EBRiskVentKind::Leakage) ? 1 : 0;
		}

		UE_LOG(LogBRiskDataImporter, Log,
			TEXT("Applied %d B-Risk openings from %s (%d doors, %d windows, %d leakage), replacing %d ")
			TEXT("VENTGEOM records: real centres supersede the equivalent-rectangle face/offset."),
			Parsed.Num(), *JsonPath, Doors, Windows, Leakage, InOutVents.Num());

		InOutVents = MoveTemp(Parsed);
	}

	void ParseZonesDataJson(
		const FString& JsonPath,
		TArray<FBRiskRoomGeometry>& InOutRooms,
		TArray<FBRiskVentGeometry>& InOutVents)
	{
		if (!FPaths::FileExists(JsonPath))
		{
			return;
		}

		FString RawJson;
		if (!FFileHelper::LoadFileToString(RawJson, *JsonPath))
		{
			UE_LOG(LogBRiskDataImporter, Warning, TEXT("Unable to read B-Risk zones JSON: %s"), *JsonPath);
			return;
		}

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawJson);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(LogBRiskDataImporter, Warning, TEXT("Malformed B-Risk zones JSON: %s"), *JsonPath);
			return;
		}

		// The add-in writes coordinates in Revit's internal frame expressed in metres - the
		// same frame and units as room_absx / room_absy. Anything else needs a transform we
		// have not derived, so refuse rather than silently mis-place every room.
		const TSharedPtr<FJsonObject>* SourceObject = nullptr;
		if (Root->TryGetObjectField(TEXT("source"), SourceObject) && SourceObject && SourceObject->IsValid())
		{
			FString CoordinateSystem;
			if ((*SourceObject)->TryGetStringField(TEXT("coordinateSystem"), CoordinateSystem)
				&& !CoordinateSystem.Equals(TEXT("revit-internal"), ESearchCase::IgnoreCase))
			{
				UE_LOG(LogBRiskDataImporter, Warning,
					TEXT("B-Risk zones JSON declares an unsupported coordinateSystem '%s' (expected 'revit-internal'); ")
					TEXT("ignoring footprints in %s"),
					*CoordinateSystem, *JsonPath);
				return;
			}

			int32 Iteration = 1;
			if ((*SourceObject)->TryGetNumberField(TEXT("iteration"), Iteration) && Iteration != 1)
			{
				UE_LOG(LogBRiskDataImporter, Warning,
					TEXT("B-Risk zones JSON reports iteration %d; Mobius assumes one geometry set per scenario. ")
					TEXT("Verify the footprints match the loaded results."),
					Iteration);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Spaces = nullptr;
		if (!Root->TryGetArrayField(TEXT("spaces"), Spaces) || !Spaces)
		{
			UE_LOG(LogBRiskDataImporter, Warning, TEXT("B-Risk zones JSON has no 'spaces' array: %s"), *JsonPath);
			return;
		}

		int32 MatchedRooms = 0;

		for (const TSharedPtr<FJsonValue>& SpaceValue : *Spaces)
		{
			const TSharedPtr<FJsonObject> Space = SpaceValue.IsValid() ? SpaceValue->AsObject() : nullptr;
			if (!Space.IsValid())
			{
				continue;
			}

			int32 RoomNumber = INDEX_NONE;
			if (!Space->TryGetNumberField(TEXT("roomNumber"), RoomNumber))
			{
				UE_LOG(LogBRiskDataImporter, Warning, TEXT("B-Risk zones JSON space has no 'roomNumber'; skipped."));
				continue;
			}

			FBRiskRoomGeometry* Room = InOutRooms.FindByPredicate(
				[RoomNumber](const FBRiskRoomGeometry& Candidate) { return Candidate.RoomId == RoomNumber; });
			if (!Room)
			{
				UE_LOG(LogBRiskDataImporter, Warning,
					TEXT("B-Risk zones JSON describes space %d, which has no matching room in the .smv; skipped."),
					RoomNumber);
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* Polygons = nullptr;
			if (!Space->TryGetArrayField(TEXT("polygons"), Polygons) || !Polygons || Polygons->Num() == 0)
			{
				UE_LOG(LogBRiskDataImporter, Warning,
					TEXT("B-Risk zones JSON space %d has no polygons; room stays on the equivalent rectangle."),
					RoomNumber);
				continue;
			}

			// The add-in emits one contiguous outer ring per space. More than one ring would
			// mean holes or disjoint parts, which nothing downstream handles yet - say so
			// rather than silently rendering only part of the room.
			if (Polygons->Num() > 1)
			{
				UE_LOG(LogBRiskDataImporter, Warning,
					TEXT("B-Risk zones JSON space %d has %d polygons; only the first is used. ")
					TEXT("Holes and multi-part rooms are not supported."),
					RoomNumber, Polygons->Num());
			}

			const TSharedPtr<FJsonObject> FirstPolygon = (*Polygons)[0].IsValid() ? (*Polygons)[0]->AsObject() : nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Vertices = nullptr;
			if (!FirstPolygon.IsValid() || !FirstPolygon->TryGetArrayField(TEXT("vertices"), Vertices) || !Vertices)
			{
				UE_LOG(LogBRiskDataImporter, Warning, TEXT("B-Risk zones JSON space %d has no vertices."), RoomNumber);
				continue;
			}

			TArray<FVector2D> Ring;
			Ring.Reserve(Vertices->Num());
			bool bVerticesValid = true;
			for (const TSharedPtr<FJsonValue>& VertexValue : *Vertices)
			{
				const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
				if (!VertexValue.IsValid() || !VertexValue->TryGetArray(Pair) || !Pair || Pair->Num() < 2)
				{
					bVerticesValid = false;
					break;
				}
				Ring.Emplace((*Pair)[0]->AsNumber(), (*Pair)[1]->AsNumber());
			}

			if (!bVerticesValid || Ring.Num() < 3)
			{
				UE_LOG(LogBRiskDataImporter, Warning,
					TEXT("B-Risk zones JSON space %d has a malformed ring (%d usable vertices); skipped."),
					RoomNumber, Ring.Num());
				continue;
			}

			// Normalise winding so downstream triangulation and point-in-polygon tests get a
			// single convention regardless of what the exporter produced.
			if (SignedRingArea(Ring) < 0.0)
			{
				Algo::Reverse(Ring);
			}

			Room->FootprintPolygon = MoveTemp(Ring);

			// Record presence, not just value: an omitted height would otherwise read as 0 m and
			// look like a disagreement with the .smv to anyone cross-checking the two.
			const bool bHasElevation = Space->TryGetNumberField(TEXT("floorElevation"), Room->FootprintFloorElevationM);
			const bool bHasHeight = Space->TryGetNumberField(TEXT("height"), Room->FootprintHeightM);
			Room->bHasFootprintExtents = bHasElevation && bHasHeight;

			const TSharedPtr<FJsonObject>* Tenability = nullptr;
			if (Space->TryGetObjectField(TEXT("tenability"), Tenability) && Tenability && Tenability->IsValid())
			{
				Room->bHasOdLimitPerM = (*Tenability)->TryGetNumberField(TEXT("odLimitPerM"), Room->OdLimitPerM);
			}

			++MatchedRooms;
		}

		// A room the JSON does not describe keeps the equivalent rectangle, which is exactly
		// the distortion this file exists to remove. That is a data-authoring fault, not a
		// normal fallback, so it must be visible.
		for (const FBRiskRoomGeometry& Room : InOutRooms)
		{
			if (Room.FootprintPolygon.Num() == 0)
			{
				UE_LOG(LogBRiskDataImporter, Warning,
					TEXT("B-Risk room %d ('%s') has no footprint in %s; it will render as the equivalent rectangle, ")
					TEXT("which does not match the real floor plan."),
					Room.RoomId, *Room.Label, *JsonPath);
			}
		}

		UE_LOG(LogBRiskDataImporter, Log,
			TEXT("Applied B-Risk zone footprints from %s (%d of %d rooms matched)."),
			*JsonPath, MatchedRooms, InOutRooms.Num());

		ParseZonesDataOpenings(Root, JsonPath, InOutRooms, InOutVents);
	}

	/**
	 * Apply B-Risk's own vent open/close schedule, from the companion vents.xml.
	 *
	 * This is deliberately B-Risk's file rather than the Revit add-in's JSON, even though
	 * openings[] carries openTimeS/closeTimeS that measure identical on all 34 openings of the
	 * 12-room export. vents.xml ships with EVERY B-Risk run that has vents, so reading it here
	 * gives scheduling to .smv-only scenarios too, and leaves one code path instead of a JSON path
	 * plus a fallback. Where both exist and disagree, B-Risk wins and the disagreement is logged -
	 * that question is out with the add-in author.
	 *
	 * THE JOIN IS THE HARD PART, and it is why this only fills in a schedule where it can prove
	 * which record belongs to which vent. Measured across two scenarios: the .smv's VENTGEOM order
	 * is NOT the vents.xml id order (positional (fromroom,toroom) agrees 20/34 in the 12-room export
	 * and 0/3 in 3_RoomFire), the two files do not even carry the same (fromroom,toroom) multiset,
	 * both files' `face` and `offset` columns disagree with each other, and vents.xml has no width
	 * or head to match on. So:
	 *
	 *  - VentId set (openings[] was applied) -> exact join on vents.xml <id>. The add-in's ventId IS
	 *    that id, and both run 1..34 in file order.
	 *  - Otherwise -> (fromRoom, toRoom, sill), and ONLY when that key picks out exactly one vent and
	 *    exactly one record. In 3_RoomFire it is unique for all three; in a corridor with 27 openings
	 *    sharing a room pair it is unique for none, and those are left unscheduled rather than
	 *    guessed. An opening wrongly told to shut is worse than one that never shuts.
	 */
	void ParseVentsXml(const FString& XmlPath, TArray<FBRiskVentGeometry>& InOutVents)
	{
		if (InOutVents.Num() == 0 || !FPaths::FileExists(XmlPath))
		{
			// No vents.xml is normal: B-Risk omits it for a model with no vents (basemodel_testBox).
			return;
		}

		FXmlFile XmlFile(XmlPath);
		if (!XmlFile.IsValid())
		{
			UE_LOG(LogBRiskDataImporter, Warning, TEXT("Unable to parse B-Risk vents XML: %s"), *XmlPath);
			return;
		}

		const FXmlNode* RootNode = XmlFile.GetRootNode();
		if (!RootNode)
		{
			return;
		}

		struct FVentSchedule
		{
			int32 Id = INDEX_NONE;
			int32 FromRoomId = INDEX_NONE;
			int32 ToRoomId = INDEX_NONE;
			double SillHeight = 0.0;
			double OpenTimeSeconds = 0.0;
			double CloseTimeSeconds = 0.0;
			bool bClaimed = false;
		};

		TArray<FVentSchedule> Schedules;
		for (const FXmlNode* VentNode : RootNode->GetChildrenNodes())
		{
			if (!VentNode || !VentNode->GetTag().Equals(TEXT("Vent"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			FVentSchedule& Schedule = Schedules.AddDefaulted_GetRef();
			Schedule.Id = GetChildInt(VentNode, TEXT("id"));
			Schedule.FromRoomId = GetChildInt(VentNode, TEXT("fromroom"));
			Schedule.ToRoomId = GetChildInt(VentNode, TEXT("toroom"));
			Schedule.SillHeight = GetChildDouble(VentNode, TEXT("sillheight"));
			Schedule.OpenTimeSeconds = GetChildDouble(VentNode, TEXT("opentime"));
			Schedule.CloseTimeSeconds = GetChildDouble(VentNode, TEXT("closetime"));
		}

		if (Schedules.Num() == 0)
		{
			return;
		}

		constexpr double SillMatchToleranceM = 1.0e-3;
		int32 MatchedById = 0;
		int32 MatchedByRoomPair = 0;
		int32 Disagreements = 0;

		const auto Apply = [&Disagreements](FBRiskVentGeometry& Vent, const FVentSchedule& Schedule)
		{
			const bool bHadJsonTimes = Vent.OpenTimeSeconds >= 0.0 || Vent.CloseTimeSeconds >= 0.0;
			if (bHadJsonTimes
				&& (!FMath::IsNearlyEqual(Vent.OpenTimeSeconds, Schedule.OpenTimeSeconds, 1.0e-6)
					|| !FMath::IsNearlyEqual(Vent.CloseTimeSeconds, Schedule.CloseTimeSeconds, 1.0e-6)))
			{
				++Disagreements;
				UE_LOG(LogBRiskDataImporter, Warning,
					TEXT("B-Risk vent %d: Zones-data.json says open %g / close %g but vents.xml says %g / %g. ")
					TEXT("Using vents.xml - it is B-Risk's own input and the only source a .smv-only ")
					TEXT("scenario has."),
					Schedule.Id, Vent.OpenTimeSeconds, Vent.CloseTimeSeconds,
					Schedule.OpenTimeSeconds, Schedule.CloseTimeSeconds);
			}

			Vent.OpenTimeSeconds = Schedule.OpenTimeSeconds;
			Vent.CloseTimeSeconds = Schedule.CloseTimeSeconds;
			Vent.bHasSchedule = true;
		};

		// Pass 1 - exact, by the id the add-in already recorded.
		for (FBRiskVentGeometry& Vent : InOutVents)
		{
			if (Vent.VentId == INDEX_NONE)
			{
				continue;
			}

			FVentSchedule* Schedule = Schedules.FindByPredicate(
				[&Vent](const FVentSchedule& Candidate) { return Candidate.Id == Vent.VentId; });
			if (Schedule && !Schedule->bClaimed)
			{
				Schedule->bClaimed = true;
				Apply(Vent, *Schedule);
				++MatchedById;
			}
		}

		// Pass 2 - .smv-only vents, and only where the room pair and sill single one out on BOTH sides.
		for (FBRiskVentGeometry& Vent : InOutVents)
		{
			if (Vent.bHasSchedule)
			{
				continue;
			}

			const auto MatchesVent = [&Vent](const FVentSchedule& Candidate)
			{
				return !Candidate.bClaimed
					&& Candidate.FromRoomId == Vent.FromRoomId
					&& Candidate.ToRoomId == Vent.ToRoomId
					&& FMath::IsNearlyEqual(Candidate.SillHeight, Vent.SillHeight, SillMatchToleranceM);
			};

			int32 CandidateCount = 0;
			FVentSchedule* Match = nullptr;
			for (FVentSchedule& Candidate : Schedules)
			{
				if (MatchesVent(Candidate))
				{
					++CandidateCount;
					Match = &Candidate;
				}
			}
			if (CandidateCount != 1 || !Match)
			{
				continue;
			}

			// Ambiguity has two directions: one record fitting several vents is just as unusable as
			// several records fitting one vent.
			int32 CompetingVents = 0;
			for (const FBRiskVentGeometry& Other : InOutVents)
			{
				if (!Other.bHasSchedule
					&& Other.FromRoomId == Vent.FromRoomId
					&& Other.ToRoomId == Vent.ToRoomId
					&& FMath::IsNearlyEqual(Other.SillHeight, Vent.SillHeight, SillMatchToleranceM))
				{
					++CompetingVents;
				}
			}
			if (CompetingVents != 1)
			{
				continue;
			}

			Match->bClaimed = true;
			Apply(Vent, *Match);
			++MatchedByRoomPair;
		}

		int32 Unscheduled = 0;
		for (const FBRiskVentGeometry& Vent : InOutVents)
		{
			Unscheduled += Vent.bHasSchedule ? 0 : 1;
		}

		UE_LOG(LogBRiskDataImporter, Log,
			TEXT("Applied B-Risk vent schedules from %s: %d of %d vents (%d by ventId, %d by room pair), ")
			TEXT("%d left permanently open because no record could be matched unambiguously, %d ")
			TEXT("disagreement(s) with Zones-data.json."),
			*XmlPath, MatchedById + MatchedByRoomPair, InOutVents.Num(),
			MatchedById, MatchedByRoomPair, Unscheduled, Disagreements);
	}

	void ParseSprinklersXml(const FString& XmlPath, const TArray<FBRiskRoomGeometry>& Rooms, TArray<FBRiskSprinklerGeometry>& OutSprinklers)
	{
		OutSprinklers.Reset();

		if (!FPaths::FileExists(XmlPath))
		{
			return;
		}

		FXmlFile XmlFile(XmlPath);
		if (!XmlFile.IsValid())
		{
			UE_LOG(LogBRiskDataImporter, Warning, TEXT("Unable to parse B-Risk sprinklers XML: %s"), *XmlPath);
			return;
		}

		const FXmlNode* RootNode = XmlFile.GetRootNode();
		if (!RootNode)
		{
			return;
		}

		for (const FXmlNode* SprinklerNode : RootNode->GetChildrenNodes())
		{
			if (!SprinklerNode || !SprinklerNode->GetTag().Equals(TEXT("Sprinkler"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			FBRiskSprinklerGeometry& Sprinkler = OutSprinklers.AddDefaulted_GetRef();
			Sprinkler.SprinklerId = GetChildInt(SprinklerNode, TEXT("sprid"));
			Sprinkler.RoomId = GetChildInt(SprinklerNode, TEXT("room"));
			Sprinkler.Location.X = GetChildDouble(SprinklerNode, TEXT("sprx"));
			Sprinkler.Location.Y = GetChildDouble(SprinklerNode, TEXT("spry"));
			Sprinkler.ActivationTimeSeconds = GetChildDouble(SprinklerNode, TEXT("responsetime"), -1.0);
			Sprinkler.SprayRadius = GetSprinklerDistributionValue(SprinklerNode, TEXT("sprr"));
			Sprinkler.SprayDensity = GetSprinklerDistributionValue(SprinklerNode, TEXT("sprdensity"));
			Sprinkler.ActuationTemperatureC = GetSprinklerDistributionValue(SprinklerNode, TEXT("acttemp"));

			const double CeilingOffset = GetSprinklerDistributionValue(SprinklerNode, TEXT("sprz"));
			if (const FBRiskRoomGeometry* Room = Rooms.FindByPredicate([&Sprinkler](const FBRiskRoomGeometry& Candidate)
				{
					return Candidate.RoomId == Sprinkler.RoomId;
				}))
			{
				Sprinkler.Location.Z = FMath::Max(0.0, static_cast<double>(Room->Size.Z) - CeilingOffset);
			}
			else
			{
				Sprinkler.Location.Z = FMath::Max(0.0, CeilingOffset);
			}
		}
	}

	/** Read a node's named attribute as a double. Returns false if absent/unparseable. */
	bool TryGetNodeAttributeDouble(const FXmlNode* Node, const TCHAR* AttributeName, double& OutValue)
	{
		if (!Node)
		{
			return false;
		}

		const FString Attr = Node->GetAttribute(AttributeName);
		if (Attr.IsEmpty())
		{
			return false;
		}

		return TryParseDouble(TrimCell(Attr), OutValue);
	}

	/** Read a <Tag value="..."> child's "value" attribute; sets bHasFlag only when present. */
	void ReadValuedChild(const FXmlNode* TimeNode, const TCHAR* Tag, double& OutValue, bool& bHasFlag)
	{
		const FXmlNode* Child = FindFirstChildByTag(TimeNode, Tag);
		double Parsed = 0.0;
		if (Child && TryGetNodeAttributeDouble(Child, TEXT("value"), Parsed))
		{
			OutValue = Parsed;
			bHasFlag = true;
		}
	}

	/** Depth-first search for the first descendant node with the given tag. */
	const FXmlNode* FindNodeByTagRecursive(const FXmlNode* Parent, const FString& Tag)
	{
		if (!Parent)
		{
			return nullptr;
		}

		for (const FXmlNode* Child : Parent->GetChildrenNodes())
		{
			if (!Child)
			{
				continue;
			}

			if (Child->GetTag().Equals(Tag, ESearchCase::IgnoreCase))
			{
				return Child;
			}

			if (const FXmlNode* Found = FindNodeByTagRecursive(Child, Tag))
			{
				return Found;
			}
		}

		return nullptr;
	}

	/**
	 * Parse B-Risk calculated tenability output (output1.xml) into per-room tables.
	 * Structure: <output><run><room id="N"><time value="t"><FEDSum value=".."/>...
	 * Missing scenarios (no file) leave OutTables empty; missing tags within a
	 * sample leave the corresponding bHas* flag false rather than fabricating a value.
	 */
	void ParseTenabilityOutputXml(const FString& XmlPath, TArray<FBRiskTenabilityRoomTable>& OutTables)
	{
		OutTables.Reset();
		if (!FPaths::FileExists(XmlPath))
		{
			return;
		}

		FXmlFile XmlFile(XmlPath);
		if (!XmlFile.IsValid())
		{
			UE_LOG(LogBRiskDataImporter, Warning,
				TEXT("Unable to parse B-Risk output XML: %s"), *XmlPath);
			return;
		}

		const FXmlNode* RootNode = XmlFile.GetRootNode();
		if (!RootNode)
		{
			return;
		}

		const FXmlNode* RunNode = FindFirstChildByTag(RootNode, TEXT("run"));
		const FXmlNode* RoomsParent = RunNode ? RunNode : RootNode;

		for (const FXmlNode* RoomNode : RoomsParent->GetChildrenNodes())
		{
			if (!RoomNode || !RoomNode->GetTag().Equals(TEXT("room"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			FBRiskTenabilityRoomTable Table;
			const FString RoomIdAttr = RoomNode->GetAttribute(TEXT("id"));
			Table.RoomId = RoomIdAttr.IsEmpty() ? INDEX_NONE : FCString::Atoi(*RoomIdAttr);

			for (const FXmlNode* TimeNode : RoomNode->GetChildrenNodes())
			{
				if (!TimeNode || !TimeNode->GetTag().Equals(TEXT("time"), ESearchCase::IgnoreCase))
				{
					continue;
				}

				FBRiskTenabilitySample Sample;
				double TimeValue = 0.0;
				if (TryGetNodeAttributeDouble(TimeNode, TEXT("value"), TimeValue))
				{
					Sample.SampleTimeSeconds = TimeValue;
				}

				ReadValuedChild(TimeNode, TEXT("HeatRelease"), Sample.HeatReleaseKW, Sample.bHasHeatRelease);
				ReadValuedChild(TimeNode, TEXT("layerheight"), Sample.LayerHeightM, Sample.bHasLayerHeight);
				ReadValuedChild(TimeNode, TEXT("uppertemp"), Sample.UpperTemperatureC, Sample.bHasUpperTemperature);
				ReadValuedChild(TimeNode, TEXT("lowertemp"), Sample.LowerTemperatureC, Sample.bHasLowerTemperature);
				ReadValuedChild(TimeNode, TEXT("Visibility"), Sample.VisibilityM, Sample.bHasVisibility);
				ReadValuedChild(TimeNode, TEXT("FEDSum"), Sample.FEDSum, Sample.bHasFEDSum);
				ReadValuedChild(TimeNode, TEXT("FEDRadSum"), Sample.FEDRadSum, Sample.bHasFEDRadSum);

				Table.Samples.Add(Sample);
			}

			Table.Samples.Sort([](const FBRiskTenabilitySample& A, const FBRiskTenabilitySample& B)
			{
				return A.SampleTimeSeconds < B.SampleTimeSeconds;
			});

			if (Table.Samples.Num() > 0)
			{
				OutTables.Add(MoveTemp(Table));
			}
		}
	}

	/**
	 * Parse B-Risk analysis endpoints from input1.xml's <tenability> block. Each
	 * endpoint is a content node (e.g. <endpoint_FED>0.3</endpoint_FED>). Absent
	 * tags leave the corresponding bHas* flag false so the consumer can warn and
	 * use a documented default. endpoint_temp is captured raw (not Celsius).
	 */
	void ParseTenabilityEndpointsXml(const FString& XmlPath, FBRiskTenabilityEndpoints& OutEndpoints)
	{
		if (!FPaths::FileExists(XmlPath))
		{
			return;
		}

		FXmlFile XmlFile(XmlPath);
		if (!XmlFile.IsValid())
		{
			UE_LOG(LogBRiskDataImporter, Warning,
				TEXT("Unable to parse B-Risk input XML: %s"), *XmlPath);
			return;
		}

		const FXmlNode* RootNode = XmlFile.GetRootNode();
		const FXmlNode* TenabilityNode = FindNodeByTagRecursive(RootNode, TEXT("tenability"));
		if (!TenabilityNode)
		{
			return;
		}

		const auto ReadEndpoint =
			[TenabilityNode](const TCHAR* Tag, double& OutValue, bool& bHasFlag)
		{
			const FString Content = GetChildContent(TenabilityNode, Tag);
			double Parsed = 0.0;
			if (!Content.IsEmpty() && TryParseDouble(Content, Parsed))
			{
				OutValue = Parsed;
				bHasFlag = true;
			}
		};

		ReadEndpoint(TEXT("monitor_height"), OutEndpoints.MonitorHeightM, OutEndpoints.bHasMonitorHeight);
		ReadEndpoint(TEXT("endpoint_visibility"), OutEndpoints.EndpointVisibilityM, OutEndpoints.bHasEndpointVisibility);
		ReadEndpoint(TEXT("endpoint_FED"), OutEndpoints.EndpointFED, OutEndpoints.bHasEndpointFED);
		ReadEndpoint(TEXT("endpoint_radiation"), OutEndpoints.EndpointRadiation, OutEndpoints.bHasEndpointRadiation);
		ReadEndpoint(TEXT("endpoint_temp"), OutEndpoints.EndpointTempRaw, OutEndpoints.bHasEndpointTemp);
	}

	/**
	 * Parse input1.xml <soot_yield> (g soot per g fuel; 0.07 for pre-flashover VM2).
	 *
	 * Separate from ParseTenabilityEndpointsXml because soot_yield is NOT inside <tenability> - it
	 * is a combustion parameter and sits elsewhere in the document, so it is located by a recursive
	 * tag search from the root. If a scenario ever declares more than one fire item, this takes the
	 * FIRST occurrence; it is reported, not simulated, so a single representative value is enough.
	 */
	void ParseSootYieldXml(const FString& XmlPath, double& OutSootYieldGPerG, bool& bOutHasSootYield)
	{
		if (!FPaths::FileExists(XmlPath))
		{
			return;
		}

		FXmlFile XmlFile(XmlPath);
		if (!XmlFile.IsValid())
		{
			// ParseTenabilityEndpointsXml already warns for this same file; stay quiet.
			return;
		}

		const FXmlNode* SootNode = FindNodeByTagRecursive(XmlFile.GetRootNode(), TEXT("soot_yield"));
		if (!SootNode)
		{
			return;
		}

		double Parsed = 0.0;
		if (TryParseDouble(SootNode->GetContent(), Parsed))
		{
			OutSootYieldGPerG = Parsed;
			bOutHasSootYield = true;
		}
	}
}

bool FBRiskDataImporter::ImportScenarioFromSmv(const FString& SmvFilePath, FBRiskScenarioData& OutData, FString* OutError)
{
	if (SmvFilePath.IsEmpty() || !FPaths::FileExists(SmvFilePath))
	{
		SetBRiskImportError(OutError, FString::Printf(TEXT("SMV file path does not exist: %s"), *SmvFilePath));
		return false;
	}

	const FString Extension = FPaths::GetExtension(SmvFilePath).ToLower();
	if (Extension != TEXT("smv"))
	{
		SetBRiskImportError(OutError, FString::Printf(TEXT("B-Risk importer expects a .smv file: %s"), *SmvFilePath));
		return false;
	}

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *SmvFilePath))
	{
		SetBRiskImportError(OutError, FString::Printf(TEXT("Unable to read B-Risk SMV file: %s"), *SmvFilePath));
		return false;
	}

	OutData = FBRiskScenarioData();
	OutData.SourceSmvPath = SmvFilePath;

	const FString SmvDirectory = FPaths::GetPath(SmvFilePath);
	TArray<FString> ZoneCsvRelativePaths;
	int32 LastRoomIndex = INDEX_NONE;

	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		const FString Line = TrimCell(Lines[LineIndex]);
		if (Line.IsEmpty())
		{
			continue;
		}

		if (Line.Equals(TEXT("ZONE"), ESearchCase::IgnoreCase))
		{
			const int32 NextIndex = FindNextDataLine(Lines, LineIndex + 1);
			if (NextIndex != INDEX_NONE)
			{
				const FString ZoneFile = TrimCell(Lines[NextIndex]);
				if (!ZoneFile.IsEmpty())
				{
					ZoneCsvRelativePaths.AddUnique(ZoneFile);
					OutData.ReferencedFiles.AddUnique(
						FPaths::ConvertRelativePathToFull(FPaths::Combine(SmvDirectory, ZoneFile)));
				}
				LineIndex = NextIndex;
			}
			continue;
		}

		if (Line.StartsWith(TEXT("ROOM"), ESearchCase::IgnoreCase))
		{
			FBRiskRoomGeometry ParsedRoom;
			if (TryParseRoom(Lines, LineIndex, ParsedRoom))
			{
				LastRoomIndex = OutData.Rooms.Add(ParsedRoom);
			}
			continue;
		}

		if (Line.Equals(TEXT("LABEL"), ESearchCase::IgnoreCase))
		{
			if (LastRoomIndex != INDEX_NONE && OutData.Rooms.IsValidIndex(LastRoomIndex))
			{
				const int32 OffsetLineIndex = FindNextDataLine(Lines, LineIndex + 1);
				const int32 LabelLineIndex = OffsetLineIndex != INDEX_NONE
					? FindNextDataLine(Lines, OffsetLineIndex + 1)
					: INDEX_NONE;

				if (LabelLineIndex != INDEX_NONE)
				{
					OutData.Rooms[LastRoomIndex].Label = TrimCell(Lines[LabelLineIndex]);
					LineIndex = LabelLineIndex;
				}
			}
			continue;
		}

		if (Line.Equals(TEXT("FIRE"), ESearchCase::IgnoreCase))
		{
			FBRiskFireGeometry ParsedFire;
			if (TryParseFire(Lines, LineIndex, ParsedFire))
			{
				OutData.Fires.Add(ParsedFire);
			}
			continue;
		}

		if (Line.Equals(TEXT("VENTGEOM"), ESearchCase::IgnoreCase))
		{
			FBRiskVentGeometry ParsedVent;
			if (TryParseVentGeom(Lines, LineIndex, ParsedVent))
			{
				OutData.Vents.Add(ParsedVent);
			}
		}
	}

	if (ZoneCsvRelativePaths.Num() == 0)
	{
		SetBRiskImportError(OutError, FString::Printf(
			TEXT("No ZONE references found in B-Risk SMV file: %s"), *SmvFilePath));
		return false;
	}

	for (const FString& RelativeZonePath : ZoneCsvRelativePaths)
	{
		const FString AbsoluteZonePath =
			FPaths::ConvertRelativePathToFull(FPaths::Combine(SmvDirectory, RelativeZonePath));

		// Absent and unreadable are different faults and must not share an outcome. The .smv is
		// written when the model is authored, so it names its results file whether or not B-Risk has
		// been run - an absent CSV is the normal state of a model waiting to be simulated, and
		// refusing to load it would throw away rooms, vents and fires that parsed perfectly well.
		// A CSV that IS present and fails to parse is a corrupted run, and still stops the import,
		// so it can never be mistaken for one that was simply never produced.
		if (!FPaths::FileExists(AbsoluteZonePath))
		{
			OutData.MissingResultFiles.AddUnique(AbsoluteZonePath);
			UE_LOG(LogBRiskDataImporter, Warning,
				TEXT("B-Risk zone CSV not found: %s. Importing geometry only - this model has no results yet."),
				*AbsoluteZonePath);
			continue;
		}

		FBRiskZoneTable ZoneTable;
		if (!ParseZoneCsv(AbsoluteZonePath, ZoneTable, OutError))
		{
			return false;
		}
		OutData.ZoneTables.Add(MoveTemp(ZoneTable));
	}

	// True room footprints from the OMA Revit add-in, when the model was exported with it.
	// Must run before anything that consumes room geometry: it is the only source of the
	// real plan shape, since the .smv rectangle is area/perimeter-equivalent only.
	const FString ZonesDataJsonPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(SmvDirectory, TEXT("Zones-data.json")));
	ParseZonesDataJson(ZonesDataJsonPath, OutData.Rooms, OutData.Vents);
	if (FPaths::FileExists(ZonesDataJsonPath))
	{
		OutData.ReferencedFiles.AddUnique(ZonesDataJsonPath);
	}

	// AFTER the JSON, because the JSON replaces the whole vent list and would drop anything applied
	// before it - and because the ventId it brings is what makes the exact join possible.
	const FString VentsXmlPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(SmvDirectory, TEXT("vents.xml")));
	ParseVentsXml(VentsXmlPath, OutData.Vents);
	if (FPaths::FileExists(VentsXmlPath))
	{
		OutData.ReferencedFiles.AddUnique(VentsXmlPath);
	}

	// Decide the scenario's coordinate frame ONCE, here, and only here.
	//
	// Keyed on whether the JSON actually delivered a footprint, NOT on whether the file exists: a
	// malformed or non-matching JSON leaves every room on the .smv rectangle, and those numbers are
	// only trustworthy in the legacy Smokeview frame. Presence of a polygon is the same condition
	// that makes the Revit mapping measurable, so the two cannot disagree.
	const bool bAnyFootprintApplied = OutData.Rooms.ContainsByPredicate(
		[](const FBRiskRoomGeometry& Room) { return Room.FootprintPolygon.Num() >= 3; });
	OutData.RoomFrame = bAnyFootprintApplied
		? BRiskCoord::ERoomFrame::Revit
		: BRiskCoord::ERoomFrame::SmokeviewSwap;

	UE_LOG(LogBRiskDataImporter, Log,
		TEXT("B-Risk scenario coordinate frame: %s (%s)."),
		bAnyFootprintApplied ? TEXT("Revit (x,-y,z)") : TEXT("legacy Smokeview X<->Y swap"),
		bAnyFootprintApplied
			? TEXT("Zones-data.json footprints applied")
			: TEXT("no Zones-data.json footprints; preserving historical orientation"));

	const FString SprinklersXmlPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(SmvDirectory, TEXT("sprinklers.xml")));
	ParseSprinklersXml(SprinklersXmlPath, OutData.Rooms, OutData.Sprinklers);
	if (OutData.Sprinklers.Num() > 0)
	{
		OutData.ReferencedFiles.AddUnique(SprinklersXmlPath);
	}

	// B-Risk calculated tenability output (FEDSum/FEDRadSum/Visibility/...) lives in
	// output1.xml, a sibling of the .smv that is NOT referenced by the manifest.
	const FString OutputXmlPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(SmvDirectory, TEXT("output1.xml")));
	ParseTenabilityOutputXml(OutputXmlPath, OutData.TenabilityTables);
	if (OutData.TenabilityTables.Num() > 0)
	{
		OutData.ReferencedFiles.AddUnique(OutputXmlPath);
	}
	else if (!FPaths::FileExists(OutputXmlPath))
	{
		// Already non-fatal, but it belongs in the same list: it is a results file, and a user told
		// only about the missing CSV would fix that and still have no tenability.
		OutData.MissingResultFiles.AddUnique(OutputXmlPath);
	}

	// Analysis endpoints (monitor height, endpoint_FED, ...) live in input1.xml.
	const FString InputXmlPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(SmvDirectory, TEXT("input1.xml")));
	ParseTenabilityEndpointsXml(InputXmlPath, OutData.TenabilityEndpoints);
	ParseSootYieldXml(InputXmlPath, OutData.SootYieldGPerG, OutData.bHasSootYield);
	if (FPaths::FileExists(InputXmlPath))
	{
		OutData.ReferencedFiles.AddUnique(InputXmlPath);
	}

	// Presence of time samples, not merely of a table: a zone CSV that parsed to zero rows answers
	// no question about what happened over time, and callers gate real work on this.
	OutData.bHasResultsData = OutData.ZoneTables.ContainsByPredicate(
		[](const FBRiskZoneTable& Table) { return Table.TimeSeconds.Num() > 0; });

	if (!OutData.bHasResultsData)
	{
		UE_LOG(LogBRiskDataImporter, Warning,
			TEXT("B-Risk scenario imported WITHOUT results (%d file(s) missing): %s"),
			OutData.MissingResultFiles.Num(),
			*FString::Join(OutData.MissingResultFiles, TEXT(", ")));
	}

	int32 RoomsWithFootprint = 0;
	for (const FBRiskRoomGeometry& Room : OutData.Rooms)
	{
		RoomsWithFootprint += (Room.FootprintPolygon.Num() > 0) ? 1 : 0;
	}

	UE_LOG(LogBRiskDataImporter, Log,
		TEXT("Imported B-Risk scenario: %s  (rooms=%d  footprints=%d  fires=%d  sprinklers=%d  vents=%d  zoneTables=%d  tenabilityRooms=%d)"),
		*SmvFilePath,
		OutData.Rooms.Num(),
		RoomsWithFootprint,
		OutData.Fires.Num(),
		OutData.Sprinklers.Num(),
		OutData.Vents.Num(),
		OutData.ZoneTables.Num(),
		OutData.TenabilityTables.Num());

	return true;
}
