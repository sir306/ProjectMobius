// Fill out your copyright notice in the Description page of Project Settings.


#include "InWorldUI/AgentInfoDisplay.h"

#include "Components/PedestrianAgentMeshWidget.h"


UAgentInfoDisplay::UAgentInfoDisplay(): WidgetMeshViewerID(0)
{
	// TODO: Remove following Debug Code
	FAgentMeshViewer DebugData1 = FAgentMeshViewer();
	DebugData1.AgentID = 1;
	DebugData1.AgentWorldPosition = FVector(100, 200, 300);
	DebugData1.AgentSpeed = 5.0f;
	DebugData1.AgentHeight = 180.0f; // Default height in cm
	
	FAgentMeshViewer DebugData2 = FAgentMeshViewer();
	DebugData2.AgentID = 2;
	DebugData2.AgentWorldPosition = FVector(0, 0, 0);
	DebugData2.AgentSpeed = 3.0f;
	DebugData2.AgentHeight = 175.0f; // Default height in cm	
	
	PedestrianAgentData.Add(DebugData1);
	PedestrianAgentData.Add(DebugData2);
}

void UAgentInfoDisplay::UpdateAgentInfoMeshData(const TArray<FAgentMeshViewer>& AgentData)
{
	PedestrianAgentData = AgentData;

	// May need to invalidate the widget to update the display
	if (DisplayWidget.IsValid())
	{
		//DisplayWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UAgentInfoDisplay::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (AgentInfoMeshAsset)
	{
		WidgetMeshViewerID = DisplayWidget->AddMesh(*AgentInfoMeshAsset);
		DisplayWidget->EnableInstancing(WidgetMeshViewerID, BaseSize);
	}
}

void UAgentInfoDisplay::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	DisplayWidget.Reset();
}

TSharedRef<SWidget> UAgentInfoDisplay::RebuildWidget()
{
	DisplayWidget = SNew(SPedestrianAgentMeshWidget, *this)
    .Text(FText::FromString(TEXT("Agent Info Display\nID:12345\nSpeed: 10.0m/s")));
	return DisplayWidget.ToSharedRef();
}
