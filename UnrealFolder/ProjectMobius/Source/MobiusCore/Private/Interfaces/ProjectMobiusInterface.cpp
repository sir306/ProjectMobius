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

#include "Interfaces/ProjectMobiusInterface.h"
#include "Engine/GameInstance.h"
#include "GameInstances/ProjectMobiusGameInstance.h"

// Add default functionality here for any IProjectMobiusInterface functions that are not pure virtual.

UProjectMobiusGameInstance* IProjectMobiusInterface::GetMobiusGameInstance(UWorld* World)
{
	if(!World)
	{
		return nullptr;
	}
	// Get the game instance
	UGameInstance* GameInstance = World->GetGameInstance();

	if (GameInstance)
	{
		// Cast the game instance to our custom game instance
		UProjectMobiusGameInstance* MobiusGameInst = Cast<UProjectMobiusGameInstance>(GameInstance);

		// check if cast was successful
		if (MobiusGameInst)
		{
			return MobiusGameInst;
		}
		else
		{
			// return nullptr if the cast was not successful
			return nullptr;
		}
	}
	else
	{
		// return nullptr if the game instance is not valid
		return nullptr;
	}
}

void IProjectMobiusInterface::GetMobiusGameInstancePedestrianData(UWorld* World, FString& OutCompleteDataPath)
{
	// Get the game instance
	UProjectMobiusGameInstance* MobiusGameInst = GetMobiusGameInstance(World);

	// Check if the game instance is valid
	if (!MobiusGameInst)
	{
		UE_LOG(LogTemp, Warning, TEXT("MobiusGameInst is nullptr"));
		return;
	}

	// Get the data file defaults from the custom game instance
	OutCompleteDataPath = MobiusGameInst->GetPedestrianDataFilePath();
}

void IProjectMobiusInterface::GetMobiusGameInstancePedestrianData(UWorld* World, FString& OutCompleteDataPath, FString& OutDataName)
{
	// Get the game instance
	UProjectMobiusGameInstance* MobiusGameInst = GetMobiusGameInstance(World);

	// Check if the game instance is valid
	if (!MobiusGameInst)
	{
		UE_LOG(LogTemp, Warning, TEXT("MobiusGameInst is nullptr"));
		return;
	}

	// Get the pedestrian data file 
	OutCompleteDataPath = MobiusGameInst->GetPedestrianDataFilePath();
	// get the pedestrian data file name
	OutDataName = MobiusGameInst->GetPedestrianDataFileName();
}

void IProjectMobiusInterface::UpdateMobiusGameInstancePedestrianData(UWorld* World, FString CompleteDataPath)
{
	// Get the game instance
	UProjectMobiusGameInstance* MobiusGameInst = GetMobiusGameInstance(World);

	// Check if the game instance is valid
	if (!MobiusGameInst)
	{
		UE_LOG(LogTemp, Warning, TEXT("MobiusGameInst is nullptr"));
		return;
	}

	// Set the pedestrian data file path
	MobiusGameInst->SetPedestrianDataFilePath(CompleteDataPath);
	
	// Set the pedestrian data file name
	MobiusGameInst->SetPedestrianDataFileName(FPaths::GetCleanFilename(CompleteDataPath));
}

void IProjectMobiusInterface::GetMobiusGameInstanceMeshDataFile(UWorld* World, FString& OutCompleteDataPath,
	FString& OutDataName)
{
	// Get the game instance
	UProjectMobiusGameInstance* MobiusGameInst = GetMobiusGameInstance(World);

	// Check if the game instance is valid
	if (!MobiusGameInst)
	{
		UE_LOG(LogTemp, Warning, TEXT("MobiusGameInst is nullptr"));
		return;
	}

	// Get the mesh data file 
	OutCompleteDataPath = MobiusGameInst->GetSimulationMeshFilePath();
	// get the pedestrian data file name
	OutDataName = MobiusGameInst->GetSimulationMeshFileName();
}

void IProjectMobiusInterface::GetMobiusGameInstanceMeshDataFile(UWorld* World, FString& OutCompleteDataPath)
{
	// Get the game instance
	UProjectMobiusGameInstance* MobiusGameInst = GetMobiusGameInstance(World);

	// Check if the game instance is valid
	if (!MobiusGameInst)
	{
		UE_LOG(LogTemp, Warning, TEXT("MobiusGameInst is nullptr"));
		return;
	}

	// Get the mesh data file 
	OutCompleteDataPath = MobiusGameInst->GetSimulationMeshFilePath();
}

void IProjectMobiusInterface::UpdateMobiusGameInstanceMeshDataFile(UWorld* World, FString CompleteDataPath)
{
	// Get the game instance
	UProjectMobiusGameInstance* MobiusGameInst = GetMobiusGameInstance(World);

	// Check if the game instance is valid
	if (!MobiusGameInst)
	{
		UE_LOG(LogTemp, Warning, TEXT("MobiusGameInst is nullptr"));
		return;
	}

	// Set the mesh data file path
	MobiusGameInst->SetSimulationMeshFilePath(CompleteDataPath);
	
	// Set the mesh data file name
	MobiusGameInst->SetSimulationMeshFileName(FPaths::GetCleanFilename(CompleteDataPath));
}

float IProjectMobiusInterface::GetMobiusGameInstanceSimulationTimeDilatationFactor(UWorld* World)
{
	// Get the game instance
	UProjectMobiusGameInstance* MobiusGameInst = GetMobiusGameInstance(World);

	// Check if the game instance is valid
	if (!MobiusGameInst)
	{
		UE_LOG(LogTemp, Warning, TEXT("MobiusGameInst is nullptr"));
		return 0.0f; // we return 0.0f if the game instance is not valid to prevent any movement
	}

	return MobiusGameInst->GetTimeDilationScaleFactor();
}

void IProjectMobiusInterface::UpdateMobiusGameInstanceSimulationTimeDilatationFactor(UWorld* World, float NewSimulationTimeDilatationFactor)
{
	// Get the game instance
	UProjectMobiusGameInstance* MobiusGameInst = GetMobiusGameInstance(World);

	// Check if the game instance is valid
	if (!MobiusGameInst)
	{
		UE_LOG(LogTemp, Warning, TEXT("MobiusGameInst is nullptr"));
		return;
	}

	// Check NewSimulation Time Dilatation Factor is greater than 0 as we don't want to have a negative time dilatation factor
	if(NewSimulationTimeDilatationFactor > 0.0f)
	{
		// Set the new time dilatation factor
		MobiusGameInst->SetTimeDilationScaleFactor(NewSimulationTimeDilatationFactor);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("NewSimulationTimeDilatationFactor is less than 0.0f"));
	}
}
