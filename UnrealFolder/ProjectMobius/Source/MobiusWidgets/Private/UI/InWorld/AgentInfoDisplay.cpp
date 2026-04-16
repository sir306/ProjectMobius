// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/InWorld/AgentInfoDisplay.h"

#include "Slate/Components/SAgentFollowIndicator.h"
#include "Slate/Components/SPedestrianAgentHoverMeshWidget.h"
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
			World->GetSubsystem<UStatisticSubsystem>()->OnSelectedAgentInfoChanged.AddUObject(this, &UAgentInfoDisplay::UpdateAgentInfoMeshData);
		}
	}
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
	Super::ReleaseSlateResources(bReleaseChildren);

	HoverWidget.Reset();
	FollowIndicatorWidget.Reset();
}

void UAgentInfoDisplay::BeginDestroy()
{
	if (UWorld* World = GetWorld())
	{
		if (UStatisticSubsystem* StatSub = World->GetSubsystem<UStatisticSubsystem>())
		{
			StatSub->OnSelectedAgentInfoChanged.RemoveAll(this);
		}
	}

	Super::BeginDestroy();
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

	return Overlay;
	//return FollowIndicatorWidget.ToSharedRef();
}
