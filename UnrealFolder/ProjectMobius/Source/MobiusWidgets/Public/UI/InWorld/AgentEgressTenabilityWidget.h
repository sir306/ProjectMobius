// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "EnumsAndStructs/AgentMeshViewer.h"
#include "AgentEgressTenabilityWidget.generated.h"

class SAgentEgressTenability;
class USlateVectorArtData;

/** Lightweight UWidget wrapper for the instanced agent egress tenability renderer. */
UCLASS()
class MOBIUSWIDGETS_API UAgentEgressTenabilityWidget : public UWidget
{
	GENERATED_BODY()

public:
	UAgentEgressTenabilityWidget(const FObjectInitializer& ObjectInitializer);

	/**
	 * Overrides the Mass-produced snapshot with caller-supplied data.
	 * Call ClearAgentEgressTenabilityDataOverride to resume the default Mass source.
	 */
	void UpdateAgentEgressTenabilityData(const TArray<FAgentEgressTenabilityViewer>& AgentEgressTenabilityData);
	void ClearAgentEgressHealthDataOverride();

	/** Returns the active data source without copying it. */
	TConstArrayView<FAgentEgressTenabilityViewer> GetAgentEgressTenabilityData() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability")
	TObjectPtr<USlateVectorArtData> AgentEgressTenabilityMeshAsset;

	/**
	 * World-space vertical offset, in centimetres above the agent origin, giving the head point
	 * the marker is anchored to. Projected through the view like any world point, so the marker
	 * sits perspective-correctly above each agent's head with no distance-driven screen drift.
	 * Tune live in the widget Details; no rebuild needed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability", meta = (ClampMin = "0.0"))
	float WorldHeightOffset = 200.0f;

	/** Distance at which the vector-art mesh is rendered at scale 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability", meta = (ClampMin = "1.0"))
	float ReferenceDistance = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability", meta = (ClampMin = "0.001"))
	float MinimumScale = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability", meta = (ClampMin = "0.001"))
	float MaximumScale = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability", meta = (ClampMin = "1"))
	int32 InitialInstanceCapacity = 4096;

	/**
	 * Draw a per-agent debug text label above each bar showing the criterion and
	 * its values (DisplayRisk, visibility, accumulated FED, temperature). Debug aid
	 * only — per-agent text is not for large crowds; disable for performance runs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability|Debug")
	bool bShowDebugText = true;

protected:
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SAgentEgressTenability> SlateWidget;
	TArray<FAgentEgressTenabilityViewer> ManualAgentEgressHealthData;
	bool bUseManualAgentEgressHealthData = false;
};
