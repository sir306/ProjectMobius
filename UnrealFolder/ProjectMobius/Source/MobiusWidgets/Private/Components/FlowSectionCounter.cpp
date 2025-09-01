// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/FlowSectionCounter.h"

#include "Blueprint/WidgetTree.h"
#include "Components/GridPanel.h"

void UFlowSectionCounter::NativePreConstruct()
{
	Super::NativePreConstruct();
	
	// Create the widget tree here if needed

	if (RootWidgetGridPanel == nullptr)
	{
		RootWidgetGridPanel = WidgetTree->ConstructWidget<UGridPanel>(UGridPanel::StaticClass(), TEXT("RootWidgetGridPanel"));
	}
	if (auto NewRoot = Cast<UGridPanel>(GetRootWidget()))
	{
		RootWidgetGridPanel = NewRoot;
	}
}

void UFlowSectionCounter::NativeConstruct()
{
	Super::NativeConstruct();
}

void UFlowSectionCounter::NativeDestruct()
{
	Super::NativeDestruct();
}

void UFlowSectionCounter::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}
