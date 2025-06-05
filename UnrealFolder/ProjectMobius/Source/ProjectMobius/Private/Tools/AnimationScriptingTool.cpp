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

#include "Tools/AnimationScriptingTool.h"

void UAnimationScriptingTool::CreateSaveFile(FString InFileName, FString InFilePath)
{
	// complete filepath and name converted to standard string
	FString completeFilePathFString = InFilePath + "/" + InFileName;
	FString completeFilePath = FPaths::ConvertRelativePathToFull(completeFilePathFString);

	// check if file exists
	if (CheckIfFileExists(completeFilePath))
	{
		// if file exists, don't create a new file
		return;
	}

	IPlatformFile& FileManager = IPlatformFile::GetPlatformPhysical();

	// create a new file following the path
	bool successfulfile = FileManager.CreateDirectoryTree(*InFilePath);

	// log if file was created successfully
	if (successfulfile)
	{
		UE_LOG(LogTemp, Warning, TEXT("File created successfully"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("File creation failed"));
	}

	FDateTime now = FDateTime::Now();

	FString HeroTitle = now.ToFormattedString(TEXT("%Y-%m-%d,%H:%M:%S,"));

	FString Strip = TEXT("_");
	FString RightHand;
	FString ExperimentTitle;

	InFileName.Split(Strip, &ExperimentTitle, &RightHand);
	
	

	HeroTitle.Append(ExperimentTitle);
	HeroTitle.Append(TEXT("\n"));
	//FString HeroTitle = now.ToFormattedString(TEXT("%d-%m-%Y, %H:%M:%S\n"));

	// create a new file
	bool successful = FFileHelper::SaveStringToFile(HeroTitle, *completeFilePath, FFileHelper::EEncodingOptions::ForceUTF8, &IFileManager::Get());

	// log if file was created successfully
	if (successful)
	{
		UE_LOG(LogTemp, Warning, TEXT("File created successfully"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("File creation failed"));
	}
}

void UAnimationScriptingTool::AppendFileWithArray(FString fileName, FString filePath, TArray<FString> data)
{
	// complete filepath and name converted to standard string
	FString completeFilePathFString = filePath + "/" + fileName;
	FString completeFilePath = FPaths::ConvertRelativePathToFull(completeFilePathFString);

	// check if file exists
	if (!CheckIfFileExists(completeFilePath))
	{
		// if file does not exist, create a new file
		CreateSaveFile(fileName, filePath);
	}

	// write data to file
	bool successful = FFileHelper::SaveStringArrayToFile(data, *completeFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);

	// log if file was created successfully
	if (successful)
	{
		UE_LOG(LogTemp, Warning, TEXT("File updated successfully"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("File update failed"));
	}
}

void UAnimationScriptingTool::AppendLineToFile(FString fileName, FString filePath, FString data)
{
	// complete filepath and name converted to standard string
	FString completeFilePathFString = filePath + "/" + fileName;
	FString completeFilePath = FPaths::ConvertRelativePathToFull(completeFilePathFString);

	// check if file exists
	if (!CheckIfFileExists(completeFilePath))
	{
		// if file does not exist, create a new file
		CreateSaveFile(fileName, filePath);
	}
	
	// append new line to the file
	data += "\n";

	// write data to file
	bool successful = FFileHelper::SaveStringToFile(data, *completeFilePath, FFileHelper::EEncodingOptions::ForceUTF8, &IFileManager::Get(), FILEWRITE_Append);

	// log if file was created successfully
	if (successful)
	{
		UE_LOG(LogTemp, Warning, TEXT("File updated successfully"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("File update failed"));
	}
}

bool UAnimationScriptingTool::CheckIfFileExists(FString filePath)
{
	IPlatformFile& FileManager = IPlatformFile::GetPlatformPhysical();

	// check if file exists
	if (FileManager.FileExists(*filePath))
	{
		// if file exists, return true
		return true;
	}

	return false;
}
