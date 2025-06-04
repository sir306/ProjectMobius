// Fill out your copyright notice in the Description page of Project Settings.

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
