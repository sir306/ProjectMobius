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
#include "UI/LoadSave/LoadDataParentWidget.h"
#include "LoadMeshWidget.generated.h"

/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API ULoadMeshWidget : public ULoadDataParentWidget
{
	GENERATED_BODY()

#pragma region METHODS
public:
#pragma region PUBLIC_METHODS
	// Constructor 
	virtual void NativeConstruct() override;
	
	/**
	 * Method to call when the SelectFileButton is clicked
	 * It is overridden from the parent class to get the mesh data file
	 */ 
	virtual void OnSelectFileButtonClicked() override;

	/**
	 * Get Mobius Game Instance data --
	 * It is overridden from the parent class to get the mesh data
	 */ 
	virtual void GetMobiusGameInstanceData() override;

	/**
	 * Method to update the Mobius Game Instance data --
	 * It is overridden from the parent class to update the mesh data
	 */ 
	virtual void UpdateMobiusGameInstanceData() override;
	
	UFUNCTION()
	virtual void DialogClosed(const FString& AgentFilePath, const FString& MeshFilePath, bool bAgentSuccess, bool bMeshSuccess) override;

	/** Handler for file dialog errors. Displays error popup to user. */
	UFUNCTION()
	void OnDialogError(const FString& ErrorTitle, const FString& ErrorMessage);

protected:
	/**
	 * If the chosen mesh .h5 also carries agent trajectories, ask whether to load them too.
	 * Mirror of ULoadAgentDataWidget::OfferEmbeddedGeometry. Silent no-op when the file holds no
	 * agent data, when it is already the loaded pedestrian file, or when the user declines.
	 *
	 * ⚠️ Blocks on a modal while open - only ever call this from the file-pick callback.
	 */
	void OfferEmbeddedAgentData(const FString& MeshFilePath);

	/** Mirrors OnMeshFileChanged into the displayed filename. */
	virtual void BindGameInstanceFileDelegate() override;
	virtual void UnbindGameInstanceFileDelegate() override;

public:
#pragma endregion PUBLIC_METHODS

#pragma endregion METHODS

#pragma region PROPERTIES_AND_CLASS_COMPONENTS
public:

protected:


private:


#pragma endregion PROPERTIES_AND_CLASS_COMPONENTS

#pragma region GETTERS_SETTERS
public:


#pragma endregion GETTERS_SETTERS
};
