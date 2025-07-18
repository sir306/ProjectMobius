// Fill out your copyright notice in the Description page of Project Settings.


#include "InWorldUI/AgentInfoDisplay.h"

#include "Components/CustomSlateComponents/SAgentFollowIndicator.h"
#include "Components/CustomSlateComponents/SPedestrianAgentHoverMeshWidget.h"
#include "Subsystems/StatisticSubsystem.h"


UAgentInfoDisplay::UAgentInfoDisplay():
	HoverWidgetMeshViewerID(0),
	SelectedFollowWidgetMeshViewerID(0)
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
	
	PedestrianHoverAgentData.Add(DebugData1);
	PedestrianHoverAgentData.Add(DebugData2);

	SelectedAgentData = DebugData1; // Set a default hovered agent for testing

	if (auto World = GetWorld())
	{
		if (auto StatSub = World->GetSubsystem<UStatisticSubsystem>())
		{
			StatSub->UpdateAgentInfoMeshData(PedestrianHoverAgentData);
			StatSub->UpdateSelectedAgentData(SelectedAgentData);
			

			// Bind delegates to trigger an update when the agent data changes
			World->GetSubsystem<UStatisticSubsystem>()->OnAgentInfoChanged.AddUObject(this, &UAgentInfoDisplay::UpdateAgentInfoMeshData);
			World->GetSubsystem<UStatisticSubsystem>()->OnSelectedAgentInfoChanged.AddUObject(this, &UAgentInfoDisplay::UpdateAgentInfoMeshData);
		}
	}
}

void UAgentInfoDisplay::UpdateAgentInfoMeshData()
{
	if (auto World = GetWorld())
	{
		PedestrianHoverAgentData = World->GetSubsystem<UStatisticSubsystem>()->GetAgentInfoMeshData();
		SelectedAgentData = World->GetSubsystem<UStatisticSubsystem>()->GetSelectedAgentInfoMeshData();
	}
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
		HoverWidgetMeshViewerID = DisplayWidget->AddMesh(*AgentInfoMeshAsset);
		DisplayWidget->EnableInstancing(HoverWidgetMeshViewerID, BaseSize);
	}
	if (AgentFollowIndicatorMeshAsset)
	{
		SelectedFollowWidgetMeshViewerID = FollowIndicatorWidget->AddMesh(*AgentFollowIndicatorMeshAsset);
		FollowIndicatorWidget->EnableInstancing(SelectedFollowWidgetMeshViewerID, 1);
	}
}

void UAgentInfoDisplay::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	DisplayWidget.Reset();
	FollowIndicatorWidget.Reset();
}

TSharedRef<SWidget> UAgentInfoDisplay::RebuildWidget()
{
	// Create the children
	DisplayWidget = SNew(SPedestrianAgentHoverMeshWidget, *this)
		.Text(FText::FromString(TEXT("Agent Info Display\nID:12345\nSpeed: 10.0m/s")));

	FollowIndicatorWidget = SNew(SAgentFollowIndicator, *this);

	

	// Create the overlay
	TSharedRef<SOverlay> Overlay = SNew(SOverlay)

	+ SOverlay::Slot()
	[
		DisplayWidget.ToSharedRef()
	]

	+ SOverlay::Slot()
	[
		FollowIndicatorWidget.ToSharedRef()
	];

	return Overlay;
	//return FollowIndicatorWidget.ToSharedRef();
}
