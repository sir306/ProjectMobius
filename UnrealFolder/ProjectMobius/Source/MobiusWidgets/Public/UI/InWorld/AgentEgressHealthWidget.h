// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "EnumsAndStructs/AgentMeshViewer.h"
#include "AgentEgressHealthWidget.generated.h"

class SAgentEgressHealth;
class USlateVectorArtData;

/** Lightweight UWidget wrapper for the instanced agent egress health renderer. */
UCLASS()
class MOBIUSWIDGETS_API UAgentEgressHealthWidget : public UWidget
{
	GENERATED_BODY()

public:
	UAgentEgressHealthWidget(const FObjectInitializer& ObjectInitializer);

	/**
	 * Overrides the Mass-produced snapshot with caller-supplied data.
	 * Call ClearAgentEgressHealthDataOverride to resume the default Mass source.
	 */
	void UpdateAgentEgressHealthData(const TArray<FAgentEgressHealthViewer>& AgentEgressHealthData);
	void ClearAgentEgressHealthDataOverride();

	/** Returns the active data source without copying it. */
	TConstArrayView<FAgentEgressHealthViewer> GetAgentEgressHealthData() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Health")
	TObjectPtr<USlateVectorArtData> AgentEgressHealthMeshAsset;

	/** World-space vertical offset from each agent origin, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Health", meta = (ClampMin = "0.0"))
	float WorldHeightOffset = 200.0f;

	/** Distance at which the vector-art mesh is rendered at scale 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Health", meta = (ClampMin = "1.0"))
	float ReferenceDistance = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Health", meta = (ClampMin = "0.001"))
	float MinimumScale = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Health", meta = (ClampMin = "0.001"))
	float MaximumScale = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Health", meta = (ClampMin = "1"))
	int32 InitialInstanceCapacity = 4096;

protected:
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SAgentEgressHealth> SlateWidget;
	TArray<FAgentEgressHealthViewer> ManualAgentEgressHealthData;
	bool bUseManualAgentEgressHealthData = false;
};
