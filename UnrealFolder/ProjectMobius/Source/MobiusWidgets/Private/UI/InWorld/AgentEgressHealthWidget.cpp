// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/InWorld/AgentEgressHealthWidget.h"

#include "Slate/Components/SAgentEgressHealth.h"
#include "Slate/SlateVectorArtData.h"
#include "Subsystems/StatisticSubsystem.h"
#include "UObject/ConstructorHelpers.h"

UAgentEgressHealthWidget::UAgentEgressHealthWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<USlateVectorArtData> DefaultMeshAsset(
		TEXT("/Game/01_Dev/Widgets/LevelComponents/EgressMetrics/Agent-ASET-Health/"
			"SVAD_AgentEgressHealth.SVAD_AgentEgressHealth"));

	if (DefaultMeshAsset.Succeeded())
	{
		AgentEgressHealthMeshAsset = DefaultMeshAsset.Object;
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);
	ForceVolatile(true);
}

void UAgentEgressHealthWidget::UpdateAgentEgressHealthData(
	const TArray<FAgentEgressHealthViewer>& AgentEgressHealthData)
{
	ManualAgentEgressHealthData = AgentEgressHealthData;
	bUseManualAgentEgressHealthData = true;
}

void UAgentEgressHealthWidget::ClearAgentEgressHealthDataOverride()
{
	ManualAgentEgressHealthData.Reset();
	bUseManualAgentEgressHealthData = false;
}

TConstArrayView<FAgentEgressHealthViewer> UAgentEgressHealthWidget::GetAgentEgressHealthData() const
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
		: TConstArrayView<FAgentEgressHealthViewer>();
}

void UAgentEgressHealthWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	MinimumScale = FMath::Max(MinimumScale, 0.001f);
	MaximumScale = FMath::Max(MaximumScale, MinimumScale);
	ReferenceDistance = FMath::Max(ReferenceDistance, 1.0f);
	InitialInstanceCapacity = FMath::Max(InitialInstanceCapacity, 1);

	if (SlateWidget.IsValid())
	{
		SlateWidget->SetMeshAsset(AgentEgressHealthMeshAsset, InitialInstanceCapacity);
	}
}

void UAgentEgressHealthWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	SlateWidget.Reset();
	Super::ReleaseSlateResources(bReleaseChildren);
}

TSharedRef<SWidget> UAgentEgressHealthWidget::RebuildWidget()
{
	SlateWidget = SNew(SAgentEgressHealth, *this);
	SlateWidget->SetMeshAsset(AgentEgressHealthMeshAsset, InitialInstanceCapacity);
	return SlateWidget.ToSharedRef();
}
