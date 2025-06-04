// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AnimationScriptingTool.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UAnimationScriptingTool : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/*
	* Method to create save file to specified location
	*
	* @param InFileName - name of the file to be created
	* @param InFilePath - path to the file to be created
	*/
	UFUNCTION(BlueprintCallable, Category = "SaveExperimentToFile")
	static void CreateSaveFile(FString InFileName, FString InFilePath);

	/*
	* Method to update file with new data array and create new file if it does not exist
	* 
	* @param fileName - name of the file to be created
	* @param filePath - path to the file to be created
	* @param data - data to be written to the file as an array of strings
	*/
	UFUNCTION(BlueprintCallable, Category = "SaveExperimentToFile")
	static void AppendFileWithArray(FString fileName, FString filePath, TArray<FString> data);
    

	/*
	* Method to update file with new data string and create new file if it does not exist
	* 
	* @param fileName - name of the file to be created
	* @param filePath - path to the file to be created
	* @param data - data to be written to the file as an array of strings
	*/
	UFUNCTION(BlueprintCallable, Category = "SaveExperimentToFile")
	static void AppendLineToFile(FString fileName, FString filePath, FString data);
    

protected:

private:
	/*
	* Method to check if file already exists - internal method
	*/
	static bool CheckIfFileExists(FString filePath);
};
