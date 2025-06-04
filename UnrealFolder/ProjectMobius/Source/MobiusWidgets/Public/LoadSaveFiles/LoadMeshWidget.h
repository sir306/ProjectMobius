// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "LoadSaveFiles/LoadDataParentWidget.h"
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

	// Tick Method for in C++ for the widget
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/**
	 * Method to call when the SelectFileButton is clicked
	 * It is overriden from the parent class to get the mesh data file
	 */ 
	virtual void OnSelectFileButtonClicked() override;

	/**
	 * Get Mobius Game Instance data --
	 * It is overriden from the parent class to get the mesh data
	 */ 
	virtual void GetMobiusGameInstanceData() override;

	/**
	 * Method to update the Mobius Game Instance data --
	 * It is overriden from the parent class to update the mesh data
	 */ 
	virtual void UpdateMobiusGameInstanceData() override;
	
	UFUNCTION()
	virtual void DialogClosed(const FString& AgentFilePath, const FString& MeshFilePath, bool bAgentSuccess, bool bMeshSuccess) override;
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
