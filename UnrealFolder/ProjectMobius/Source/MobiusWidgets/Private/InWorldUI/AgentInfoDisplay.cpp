// Fill out your copyright notice in the Description page of Project Settings.


#include "InWorldUI/AgentInfoDisplay.h"

#include "Components/PedestrianAgentMeshWidget.h"

UAgentInfoDisplay::UAgentInfoDisplay(): AgentID(0)
{
}

void UAgentInfoDisplay::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (AgentInfoMeshAsset)
	{
		AgentID = DisplayWidget->AddMesh(*AgentInfoMeshAsset);
		DisplayWidget->EnableInstancing(AgentID, BaseSize);
	}
}

void UAgentInfoDisplay::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	DisplayWidget.Reset();
}

TSharedRef<SWidget> UAgentInfoDisplay::RebuildWidget()
{
	DisplayWidget = SNew(SPedestrianAgentMeshWidget, *this).Text("_Text");
	return DisplayWidget.ToSharedRef();
}
