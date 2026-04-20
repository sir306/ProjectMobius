// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/InWorld/AgentInfoDisplay.h"

#include "Slate/Components/SAgentFollowIndicator.h"
#include "Slate/Components/SPedestrianAgentHoverMeshWidget.h"
#include "Subsystems/StatisticSubsystem.h"


UAgentInfoDisplay::UAgentInfoDisplay():
	HoverWidgetMeshViewerID(0),
	SelectedFollowWidgetMeshViewerID(0)
{
}

void UAgentInfoDisplay::UpdateAgentInfoMeshData()
{
	if (auto World = GetWorld())
	{
		SelectedAgentData = World->GetSubsystem<UStatisticSubsystem>()->GetSelectedAgentInfoMeshData();
		HoveredAgentData = World->GetSubsystem<UStatisticSubsystem>()->GetHoveredAgentInfoMeshData();
	}
	// May need to invalidate the widget to update the display
	if (HoverWidget.IsValid())
	{
		//DisplayWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UAgentInfoDisplay::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (AgentHoverMeshAsset)
	{
		HoverWidgetMeshViewerID = HoverWidget->AddMesh(*AgentHoverMeshAsset);
		HoverWidget->EnableInstancing(HoverWidgetMeshViewerID, 1);
	}
	if (AgentFollowIndicatorMeshAsset)
	{
		SelectedFollowWidgetMeshViewerID = FollowIndicatorWidget->AddMesh(*AgentFollowIndicatorMeshAsset);
		FollowIndicatorWidget->EnableInstancing(SelectedFollowWidgetMeshViewerID, 1);
	}
}

void UAgentInfoDisplay::ReleaseSlateResources(bool bReleaseChildren)
{
	if (UWorld* World = GetWorld())
	{
		if (UStatisticSubsystem* StatSub = World->GetSubsystem<UStatisticSubsystem>())
		{
			StatSub->OnSelectedAgentInfoChanged.RemoveAll(this);
		}
	}

	HoverWidget.Reset();
	FollowIndicatorWidget.Reset();

	Super::ReleaseSlateResources(bReleaseChildren);
}


TSharedRef<SWidget> UAgentInfoDisplay::RebuildWidget()
{
	// Create the children
	HoverWidget = SNew(SAgentFollowIndicator, *this)
		.FollowIndicator(false);

	FollowIndicatorWidget = SNew(SAgentFollowIndicator, *this);



	// Create the overlay
	TSharedRef<SOverlay> Overlay = SNew(SOverlay)

	+ SOverlay::Slot()
	[
		HoverWidget.ToSharedRef()
	]

	+ SOverlay::Slot()
	[
		FollowIndicatorWidget.ToSharedRef()
	];

	if (UWorld* World = GetWorld())
	{
		if (UStatisticSubsystem* StatSub = World->GetSubsystem<UStatisticSubsystem>())
		{
			StatSub->OnSelectedAgentInfoChanged.RemoveAll(this);
			StatSub->OnSelectedAgentInfoChanged.AddUObject(this, &UAgentInfoDisplay::UpdateAgentInfoMeshData);
		}
	}

	return Overlay;
	//return FollowIndicatorWidget.ToSharedRef();
}
