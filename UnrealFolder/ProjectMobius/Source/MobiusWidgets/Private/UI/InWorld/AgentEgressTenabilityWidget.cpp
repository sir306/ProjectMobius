// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/InWorld/AgentEgressTenabilityWidget.h"

#include "Slate/Components/SAgentEgressHealth.h"
#include "Slate/SlateVectorArtData.h"
#include "Subsystems/StatisticSubsystem.h"
#include "UObject/ConstructorHelpers.h"

UAgentEgressTenabilityWidget::UAgentEgressTenabilityWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<USlateVectorArtData> DefaultMeshAsset(
		TEXT("/Game/01_Dev/Widgets/LevelComponents/EgressMetrics/Agent-Tenability/"
			"SVAD_AgentEgressTenability.SVAD_AgentEgressTenability"));

	if (DefaultMeshAsset.Succeeded())
	{
		AgentEgressTenabilityMeshAsset = DefaultMeshAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<USlateVectorArtData> DefaultFailMarkerAsset(
		TEXT("/Game/01_Dev/Widgets/WidgetMaterials/WorldWidgetMats/TenabilityFailMarkers/"
			"SVAD_TenabilityFailMarker.SVAD_TenabilityFailMarker"));

	if (DefaultFailMarkerAsset.Succeeded())
	{
		FailMarkerMeshAsset = DefaultFailMarkerAsset.Object;
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);
	ForceVolatile(true);
}

void UAgentEgressTenabilityWidget::UpdateAgentEgressTenabilityData(
	const TArray<FAgentEgressTenabilityViewer>& AgentEgressTenabilityData)
{
	ManualAgentEgressHealthData = AgentEgressTenabilityData;
	bUseManualAgentEgressHealthData = true;
}

void UAgentEgressTenabilityWidget::ClearAgentEgressHealthDataOverride()
{
	ManualAgentEgressHealthData.Reset();
	bUseManualAgentEgressHealthData = false;
}

TConstArrayView<FAgentEgressTenabilityViewer> UAgentEgressTenabilityWidget::GetAgentEgressTenabilityData() const
{
	if (bUseManualAgentEgressHealthData)
	{
		return ManualAgentEgressHealthData;
	}

	const UWorld* World = GetWorld();
	const UStatisticSubsystem* StatisticSubsystem =
		World ? World->GetSubsystem<UStatisticSubsystem>() : nullptr;
	return StatisticSubsystem
		? StatisticSubsystem->GetAgentEgressHealthData()
		: TConstArrayView<FAgentEgressTenabilityViewer>();
}

void UAgentEgressTenabilityWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	MinimumScale = FMath::Max(MinimumScale, 0.001f);
	MaximumScale = FMath::Max(MaximumScale, MinimumScale);
	ReferenceDistance = FMath::Max(ReferenceDistance, 1.0f);
	InitialInstanceCapacity = FMath::Max(InitialInstanceCapacity, 1);

	FailMarkerHeightOffset = FMath::Max(FailMarkerHeightOffset, 0.0f);

	if (SlateWidget.IsValid())
	{
		SlateWidget->SetMeshAsset(AgentEgressTenabilityMeshAsset, InitialInstanceCapacity);
		// After the bar, always: registration order is the only thing fixing which mesh paints on top.
		SlateWidget->SetFailMarkerMeshAsset(FailMarkerMeshAsset, InitialInstanceCapacity);
	}
}

void UAgentEgressTenabilityWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	SlateWidget.Reset();
	Super::ReleaseSlateResources(bReleaseChildren);
}

TSharedRef<SWidget> UAgentEgressTenabilityWidget::RebuildWidget()
{
	SlateWidget = SNew(SAgentEgressTenability, *this);
	SlateWidget->SetMeshAsset(AgentEgressTenabilityMeshAsset, InitialInstanceCapacity);
	SlateWidget->SetFailMarkerMeshAsset(FailMarkerMeshAsset, InitialInstanceCapacity);
	return SlateWidget.ToSharedRef();
}
