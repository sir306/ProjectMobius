// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WKT_FileParser.generated.h"

/**
 * This class is responsible for parsing WKT (Well-Known Text) files.
 * For more information on WKT, see:
 * https://en.wikipedia.org/wiki/Well-known_text_representation_of_geometry
 *
 * For now this class will implement a basic JSON parser for WKT files.
 * And implement a simple logic to create the geoemetry from the parsed data, for the mesh generator to use.
 * In future, we will extend this class to support an external library for WKT parsing, likely GeosJSON.
 * (Incorporating GeosJSON will allow us to handle more complex geometries and operations on them. However it is causing
 * issues with how GeosJSON implements its files.)
 */
UCLASS()
class MOBIUSCORE_API UWKT_FileParser : public UObject
{
	GENERATED_BODY()
	
public:
	// Constructor
	UWKT_FileParser();
	// Destructor
	virtual ~UWKT_FileParser();
	
	/**
	 * Load a .wkt file containing WKT data.
	 *
	 * @param[FString] FilePath The path to the WKT file.
	 * @param[FString] OutWKTData The output string to store the parsed WKT data.
	 * @param[FString] OutErrorMessage The output string to store any error messages.
	 * @return[bool] True if the file was loaded successfully, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "WKT_File_Parser")
	bool LoadWKTFile(const FString& FilePath, FString& OutWKTData, FString& OutErrorMessage);

	/**
	 * Load a JSON file containing WKT data.
	 *
	 * @param[FString] FilePath The path to the WKT file.
	 * @param[TArray<FString>] OutWktList The output string to store the parsed WKT data.
	 * @param[FString] OutErrorMessage The output string to store any error messages.
	 * @return[bool] True if the file was loaded successfully, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "WKT_File_Parser")
	bool LoadJsonWktFile(const FString& FilePath, TArray<FString>& OutWktList, FString& OutErrorMessage);

	/**
	 * Parse the WKT data from a string.
	 * @param[FString] InWKTDataString The output string to store the parsed WKT data.
	 * @param[FString] OutErrorMessage The output string to store any error messages.
	 * @return[TArray<FVector2D>] Array of 2D vectors representing the parsed WKT data - will be empty if data doesn't pass correctly or is empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "WKT_File_Parser")
	TArray<FVector2D> ParseWKTData(const FString& InWKTDataString, FString& OutErrorMessage);
	
	bool ParseGeometryCollectionWkt(const FString& WKTString, TArray<TArray<FVector2D>>& OutGeometries, FString& OutErrorMessage);

	/**
	 * To use our assimp importer, we need to convert the parsed WKT data into a format that can be used by the importer.
	 * This will convert the 2D vectors into 3D vectors, and create an Obj mesh
	 *
	 * @param[TArray<FVector2D>] ParsedPoints The array of 2D vectors representing the parsed WKT data.
	 * @return[FString] The string representation of the WKT data in a format that can be used by the importer.
	 */
	UFUNCTION(BlueprintCallable, Category = "WKT_File_Parser")
	FString ConvertPolygonToObjFormat(const TArray<FVector2D>& ParsedPoints) const;
	
};
