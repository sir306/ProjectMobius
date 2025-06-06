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
#include "UObject/Interface.h"
#include "ProjectMobiusInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UProjectMobiusInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class MOBIUSCORE_API IProjectMobiusInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	/**
	* Get Mobius Game Instance
	* 
	* @param World The world context
	*
	* @return UProjectMobiusGameInstance* The Mobius Game Instance
	*
	*/
	class UProjectMobiusGameInstance* GetMobiusGameInstance(UWorld* World);


	/**
	* Get Mobius Game Instance Pedestrian data CompleteFilePath
	* 
	* @param OutCompleteDataPath The variable to assign the complete path to the data file
	*/
	void GetMobiusGameInstancePedestrianData(UWorld* World, FString& OutCompleteDataPath);

	/**
	* Get Mobius Game Instance Pedestrian data CompleteFilePath and DataName
	* 
	* @param OutCompleteDataPath The variable to assign the complete path to the data file
	* @param OutDataName The variable to assign the data name
	* 
	*/
	void GetMobiusGameInstancePedestrianData(UWorld* World, FString& OutCompleteDataPath, FString& OutDataName);

	/**
	* Update Mobius Game Instance Pedestrian Data 
	*
	* @param World The world context
	* @param CompleteDataPath The complete path to the data file
	* 
	*/
	void UpdateMobiusGameInstancePedestrianData(UWorld* World, FString CompleteDataPath);

	/**
	 * Get Mobius Game Instance Mesh File Data Path and Data Name
	 *
	 * @param World The world context
	 * @param OutCompleteDataPath The variable to assign the complete path to the file for the mesh
	 * @param OutDataName The variable to assign the file name
	 */
	void GetMobiusGameInstanceMeshDataFile(UWorld* World, FString& OutCompleteDataPath, FString& OutDataName);

	/**
	 * Get Mobius Game Instance Mesh File Data Path and Data Name
	 *
	 * @param World The world context
	 * @param OutCompleteDataPath The variable to assign the complete path to the file for the mesh
	 */
	void GetMobiusGameInstanceMeshDataFile(UWorld* World, FString& OutCompleteDataPath);
	
	/**
	 * Update Mobius Game Instance Mesh File Data Path
	 *
	 * @param World The world context
	 * @param CompleteDataPath The complete path to the data file for the mesh
	 */
	void UpdateMobiusGameInstanceMeshDataFile(UWorld* World, FString CompleteDataPath);

	/**
	* Get Mobius Game Instance Simulation Time Dilatation Factor
	*
	* @param World The world context
	*
	* @return float The simulation time dilatation factor
	*/
	float GetMobiusGameInstanceSimulationTimeDilatationFactor(UWorld* World);

	/**
	* Update Mobius Game Instance Simulation Time Dilatation Factor
	* 
	* @param NewSimulationTimeDilatationFactor The new simulation time dilatation factor
	*/
	virtual void UpdateMobiusGameInstanceSimulationTimeDilatationFactor(UWorld* World, float NewSimulationTimeDilatationFactor);
};
