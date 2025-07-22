// Fill out your copyright notice in the Description page of Project Settings.


#include "InWorldUI/AgentInfoDisplay.h"

#include "Components/CustomSlateComponents/SAgentFollowIndicator.h"
#include "Components/CustomSlateComponents/SPedestrianAgentHoverMeshWidget.h"
#include "Subsystems/StatisticSubsystem.h"


UAgentInfoDisplay::UAgentInfoDisplay():
	HoverWidgetMeshViewerID(0),
	SelectedFollowWidgetMeshViewerID(0)
{
	if (auto World = GetWorld())
	{
		if (auto StatSub = World->GetSubsystem<UStatisticSubsystem>())
		{
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
