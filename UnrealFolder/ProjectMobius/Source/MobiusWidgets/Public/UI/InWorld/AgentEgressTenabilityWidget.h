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
	 * Vector-art quad for the in-world fail markers, drawn as a SECOND hardware-instanced mesh by the
	 * same renderer, so the whole crowd's markers cost one extra draw call. Registered after the bar
	 * mesh deliberately: Slate has no depth buffer, so the order meshes are added in is what fixes
	 * their relative order, and markers must paint above bars.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability")
	TObjectPtr<USlateVectorArtData> FailMarkerMeshAsset;

	/**
	 * World-space vertical offset, in centimetres above the agent origin, giving the head point
	 * the marker is anchored to. Projected through the view like any world point, so the marker
	 * sits perspective-correctly above each agent's head with no distance-driven screen drift.
	 * Tune live in the widget Details; no rebuild needed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability", meta = (ClampMin = "0.0"))
	float WorldHeightOffset = 200.0f;

	/**
	 * World-space vertical offset, in centimetres, of a fail marker above the point where that agent's
	 * tenability failed. Above WorldHeightOffset so the marker clears the live bar. Note the anchor
	 * differs from the bar's: the bar tracks the agent, the marker stays at the failure point, which is
	 * what makes it a forensic record rather than a second health readout.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability", meta = (ClampMin = "0.0"))
	float FailMarkerHeightOffset = 260.0f;

	/**
	 * Master gate for the in-world fail markers. Off submits an empty instance buffer rather than
	 * branching per agent, so the mesh stays resident and toggling back on needs no reallocation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability")
	bool bShowFailMarkers = true;

	/**
	 * Smallest instance scale a fail marker may render at, separate from the bar's MinimumScale.
	 * The two cannot share a floor: at weight 7 on a 128 px atlas slot the stroke is about 3.5 px, so
	 * a marker below roughly 28 px on screen goes sub-pixel, and on the 32-unit quad the bar's default
	 * of 0.05 would put a distant marker at 1.6 px. 1.25 holds a 40 px floor. Raising the bar's own
	 * MinimumScale to match would have made distant bars much larger, changing shipped behaviour.
	 *
	 * This is the supported fix for markers fading at distance. Do NOT thicken the source art
	 * instead — the stroke weights are measured and owner-approved; see
	 * SourceArt/TenabilityFailMarkers/README.md.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability", meta = (ClampMin = "0.001"))
	float FailMarkerMinimumScale = 1.25f;

	/**
	 * Per-criterion marker gates. These filter on the icon actually drawn — that is, on
	 * FirstFailureCriterion via its atlas slot — and deliberately NOT on FailureMask. FailureMask
	 * holds every criterion that failed simultaneously, so a mask filter could hide a marker that is
	 * visibly showing a different hazard's icon. Unchecking Thermal hides thermal-looking markers and
	 * nothing else.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability|Fail Marker Types")
	bool bShowThermalFailMarkers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability|Fail Marker Types")
	bool bShowGasFailMarkers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability|Fail Marker Types")
	bool bShowVisibilityFailMarkers = true;

	/**
	 * The unattributed-failure diagnostic (Temperature, LayerHeight, or a failure flag with criterion
	 * None, which is a timeline-builder bug). Defaults ON: hiding a bug indicator by default defeats
	 * the purpose of having one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability|Fail Marker Types")
	bool bShowUnattributedFailMarkers = true;

	/**
	 * Is the marker for this atlas slot currently visible? Slots are 0 thermal, 1 gas, 2 visibility,
	 * 3 unattributed diagnostic, matching T_TenabilityFailMarkerAtlas and the renderer's criterion
	 * mapping. Keeps the slot-to-toggle table in one place.
	 */
	bool IsFailMarkerSlotVisible(int32 AtlasSlot) const;

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
