// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "BRiskDataImporter.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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

		const TArray<FString> DimTokens = SplitWhitespace(TrimCell(Lines[DimsIndex]));
		const TArray<FString> OriginTokens = SplitWhitespace(TrimCell(Lines[OriginIndex]));
		if (DimTokens.Num() < 3 || OriginTokens.Num() < 3)
		{
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

		const TArray<FString> Tokens = SplitWhitespace(TrimCell(Lines[DataIndex]));
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

		const TArray<FString> Tokens = SplitWhitespace(TrimCell(Lines[DataIndex]));
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
		if (TryParseDouble(Tokens[6], Value)) OutVent.Height = Value;

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

		FBRiskZoneTable ZoneTable;
		if (!ParseZoneCsv(AbsoluteZonePath, ZoneTable, OutError))
		{
			return false;
		}
		OutData.ZoneTables.Add(MoveTemp(ZoneTable));
	}

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

	// Analysis endpoints (monitor height, endpoint_FED, ...) live in input1.xml.
	const FString InputXmlPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(SmvDirectory, TEXT("input1.xml")));
	ParseTenabilityEndpointsXml(InputXmlPath, OutData.TenabilityEndpoints);
	if (FPaths::FileExists(InputXmlPath))
	{
		OutData.ReferencedFiles.AddUnique(InputXmlPath);
	}

	UE_LOG(LogBRiskDataImporter, Log,
		TEXT("Imported B-Risk scenario: %s  (rooms=%d  fires=%d  sprinklers=%d  vents=%d  zoneTables=%d  tenabilityRooms=%d)"),
		*SmvFilePath,
		OutData.Rooms.Num(),
		OutData.Fires.Num(),
		OutData.Sprinklers.Num(),
		OutData.Vents.Num(),
		OutData.ZoneTables.Num(),
		OutData.TenabilityTables.Num());

	return true;
}
