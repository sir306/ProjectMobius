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
#include "LoadSaveFiles/LoadDataParentWidget.h"
#include "LoadAgentDataWidget.generated.h"

/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API ULoadAgentDataWidget : public ULoadDataParentWidget
{
	GENERATED_BODY()
	
public:
	// Constructor 
	virtual void NativeConstruct() override;

	// Tick Method for in C++ for the widget
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/**
	 * Method to call when the SelectFileButton is clicked
	 * It is overriden from the parent class to get the pedestrian data
	 */ 
	virtual void OnSelectFileButtonClicked() override;

	/**
	 * Get Mobius Game Instance data --
	 * It is overriden from the parent class to get the pedestrian data
	 */ 
	virtual void GetMobiusGameInstanceData() override;

	/**
	 * Method to update the Mobius Game Instance data --
	 * It is overriden from the parent class to update the pedestrian data
	 */ 
	virtual void UpdateMobiusGameInstanceData() override;

	UFUNCTION()
	virtual void DialogClosed(const FString& AgentFilePath, const FString& MeshFilePath, bool bAgentSuccess, bool bMeshSuccess) override;
};
