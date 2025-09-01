// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/FlowSectionCounter.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"

void UFlowSectionCounter::NativePreConstruct()
{
	Super::NativePreConstruct();
	
	// Create the widget tree here if needed
	
}

void UFlowSectionCounter::NativeConstruct()
{
	Super::NativeConstruct();
	
}

void UFlowSectionCounter::NativeDestruct()
{
	Super::NativeDestruct();
}

TSharedRef<SWidget> UFlowSectionCounter::RebuildWidget()
{
	return Super::RebuildWidget();
}

FVector2D UFlowSectionCounter::GetParentSlotSize() const
{
	// Make sure the widget is constructed and ticked at least once,
	// otherwise the cached geometry may not be valid yet
	if (!IsValid(this))
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowSectionCounter widget is invalid!"));
		return FVector2D::ZeroVector;
	}

	// This gives you the actual render-space size of the widget
	const FGeometry& CachedGeometry = GetCachedGeometry();
	const FVector2D LocalSize = CachedGeometry.GetLocalSize();

	UE_LOG(LogTemp, Display, TEXT("FlowSectionCounter size: %s"), *LocalSize.ToString());

	if (LocalSize.IsZero())
	{
		// log warning
		UE_LOG(LogTemp, Warning, TEXT("FlowSectionCounter LocalSize is zero! - geometry is likely not valid yet, i.e. called in a pre constructor or before synchronized."));

		return FVector2D(130, 30); // return a default size -> for now
	}

	return LocalSize;
}

void UFlowSectionCounter::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	GetParentSlotSize();
}
