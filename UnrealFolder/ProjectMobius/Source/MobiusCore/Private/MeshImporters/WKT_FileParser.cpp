// Fill out your copyright notice in the Description page of Project Settings.


#include "MeshImporters/WKT_FileParser.h"

UWKT_FileParser::UWKT_FileParser()
{
}

UWKT_FileParser::~UWKT_FileParser()
{
}

bool UWKT_FileParser::LoadWKTFile(const FString& FilePath, FString& OutWKTData, FString& OutErrorMessage)
{
	// Check if the file exists
	if (!FPaths::FileExists(FilePath))
	{
		OutErrorMessage = FString::Printf(TEXT("File not found: %s"), *FilePath);
		return false;
	}

	// Load the file content
	if (FFileHelper::LoadFileToString(OutWKTData, *FilePath))
	{
		// Successfully loaded the file
		return true;
	}

	// failed to load the file and parse as string
	OutErrorMessage = FString::Printf(TEXT("Failed to load WKT file: %s"), *FilePath);

	// failed to load the file
	return false;
}

bool UWKT_FileParser::LoadJsonWktFile(const FString& FilePath, TArray<FString>& OutWktList, FString& OutErrorMessage)
{
	// Check if file exists
	if (!FPaths::FileExists(FilePath))
	{
		OutErrorMessage = FString::Printf(TEXT("File not found: %s"), *FilePath);
		return false;
	}

	// Read file to string
	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		OutErrorMessage = FString::Printf(TEXT("Failed to read file: %s"), *FilePath);
		return false;
	}

	// Parse JSON
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
	TArray<TSharedPtr<FJsonValue>> JsonArray;

	if (!FJsonSerializer::Deserialize(Reader, JsonArray) || JsonArray.Num() == 0)
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid or empty JSON array in file: %s"), *FilePath);
		return false;
	}

	// Extract all 'geometry' fields
	for (const TSharedPtr<FJsonValue>& Entry : JsonArray)
	{
		if (!Entry.IsValid() || !Entry->AsObject().IsValid())
			continue;

		TSharedPtr<FJsonObject> Obj = Entry->AsObject();
		FString Geometry;
		if (Obj->TryGetStringField(TEXT("geometry"), Geometry))
		{
			OutWktList.Add(Geometry);
		}
	}

	if (OutWktList.Num() == 0)
	{
		OutErrorMessage = TEXT("No 'geometry' fields found in JSON array.");
		return false;
	}

	return true;
}

TArray<FVector2D> UWKT_FileParser::ParseWKTData(const FString& InWKTDataString, FString& OutErrorMessage)
{
	FString CleanWKT = InWKTDataString;
	CleanWKT.TrimStartAndEndInline();
	CleanWKT = CleanWKT.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(""));

	FString Prefix;
	FString CoordBlock;

	// Extract prefix and inner coordinates
	int32 OpenParenIndex;
	if (CleanWKT.FindChar('(', OpenParenIndex))
	{
		Prefix = CleanWKT.Left(OpenParenIndex).ToUpper().TrimStartAndEnd();
		CoordBlock = CleanWKT.Mid(OpenParenIndex);
		CoordBlock = CoordBlock.Replace(TEXT("("), TEXT("")).Replace(TEXT(")"), TEXT(""));
	}

	TArray<FVector2D> ParsedPoints;

	if (Prefix == TEXT("POINT"))
	{
		TArray<FString> XY;
		CoordBlock.ParseIntoArray(XY, TEXT(" "), true);
		if (XY.Num() == 2)
		{
			ParsedPoints.Add(FVector2D(FCString::Atof(*XY[0]), FCString::Atof(*XY[1])));
		}
	}
	else if (Prefix == TEXT("LINESTRING") || Prefix == TEXT("POLYGON"))
	{
		if (Prefix == TEXT("POLYGON"))
		{
			// POLYGON can have nested parentheses
			int32 InnerStart = CleanWKT.Find(TEXT("(("));
			int32 InnerEnd = CleanWKT.Find(TEXT("))"));
			if (InnerStart != INDEX_NONE && InnerEnd != INDEX_NONE)
			{
				CoordBlock = CleanWKT.Mid(InnerStart + 2, InnerEnd - InnerStart - 2);
			}
		}

		TArray<FString> Pairs;
		CoordBlock.ParseIntoArray(Pairs, TEXT(","), true);
		for (const FString& Pair : Pairs)
		{
			TArray<FString> XY;
			Pair.TrimStartAndEnd().ParseIntoArray(XY, TEXT(" "), true);
			if (XY.Num() == 2)
			{
				ParsedPoints.Add(FVector2D(FCString::Atof(*XY[0]), FCString::Atof(*XY[1])));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Unsupported WKT type: %s"), *Prefix);
		OutErrorMessage = FString::Printf(TEXT("Unsupported WKT type: %s"), *Prefix);
	}

	return ParsedPoints;
}

bool UWKT_FileParser::ParseGeometryCollectionWkt(const FString& WKTString, TArray<TArray<FVector2D>>& OutGeometries,
	FString& OutErrorMessage)
{
	FString CleanWKT = WKTString;
	CleanWKT.TrimStartAndEndInline();
	CleanWKT = CleanWKT.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(""));

	if (!CleanWKT.StartsWith(TEXT("GEOMETRYCOLLECTION"), ESearchCase::IgnoreCase))
	{
		OutErrorMessage = TEXT("WKT does not begin with GEOMETRYCOLLECTION");
		return false;
	}

	// Extract the content inside the GEOMETRYCOLLECTION (...)
	int32 OpenParen = CleanWKT.Find(TEXT("("));
	int32 CloseParen = INDEX_NONE;
	if (OpenParen == INDEX_NONE || !CleanWKT.FindLastChar(')', CloseParen) || CloseParen <= OpenParen)
	{
		OutErrorMessage = TEXT("Malformed GEOMETRYCOLLECTION WKT.");
		return false;
	}

	FString Inner = CleanWKT.Mid(OpenParen + 1, CloseParen - OpenParen - 1).TrimStartAndEnd();

	// Look for each POLYGON block
	int32 Pos = 0;
	while (true)
	{
		int32 PolygonStart = Inner.Find(TEXT("POLYGON"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Pos);
		if (PolygonStart == INDEX_NONE) break;

		int32 FirstParen = Inner.Find(TEXT("(("), ESearchCase::IgnoreCase, ESearchDir::FromStart, PolygonStart);
		int32 EndParen = Inner.Find(TEXT("))"), ESearchCase::IgnoreCase, ESearchDir::FromStart, FirstParen + 2);
		if (FirstParen == INDEX_NONE || EndParen == INDEX_NONE) break;

		FString PolygonBlock = Inner.Mid(FirstParen + 2, EndParen - FirstParen - 2);

		FString DummyError;
		TArray<FVector2D> PolygonPoints = ParseWKTData(TEXT("LINESTRING(") + PolygonBlock + TEXT(")"), DummyError);
		if (!PolygonPoints.IsEmpty())
		{
			OutGeometries.Add(PolygonPoints);
		}

		Pos = EndParen + 2;
	}

	if (OutGeometries.Num() == 0)
	{
		OutErrorMessage = TEXT("No valid POLYGON found in GEOMETRYCOLLECTION.");
		return false;
	}

	return true;
}

