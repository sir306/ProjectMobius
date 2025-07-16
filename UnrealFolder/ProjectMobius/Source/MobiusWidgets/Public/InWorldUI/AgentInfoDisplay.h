// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EnumsAndStructs/AgentMeshViewer.h"
#include "AgentInfoDisplay.generated.h"

class USelectedAgentDisplay;
class USlateVectorArtData;
class SPedestrianAgentMeshWidget;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UAgentInfoDisplay : public UWidget
{
	GENERATED_BODY()

public:
	UAgentInfoDisplay();
	
	/** Updates the PedestrianAgentData array with the provided AgentData and ensures the display widget is prepared
	 *  for potential updates. This method may involve marking the widget for repainting to reflect changes in the data.
	 *  If the associated display widget is valid, the function checks its state, enabling deferred updates for optimized rendering.
	 *
	 * @param AgentData An array of FAgentMeshViewer objects containing data to update the current PedestrianAgentData.
	 */
	void UpdateAgentInfoMeshData();
	

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Info")
	float BaseSize = 100.0f; /*must match mesh size in cm - text rendered behaves as expected but mesh doesn't*/

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Info")
	float ReferenceDistance = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Info")
	TObjectPtr<USlateVectorArtData> AgentInfoMeshAsset;

	int32 WidgetMeshViewerID;

	/** To avoid constant rebuilds, we use this array to hold current information that will be used in OnPaint call */
	TArray<FAgentMeshViewer> PedestrianAgentData = TArray<FAgentMeshViewer>();

protected:
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

	TSharedPtr<SPedestrianAgentMeshWidget> DisplayWidget;
};
