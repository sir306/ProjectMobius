// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.  
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,  
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL  
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR  
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING  
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS  
 * IN THE SOFTWARE.
 */

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
