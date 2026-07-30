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
	 * tenability failed.
	 *
	 * Owner-set to 50, well below the bar's WorldHeightOffset of 200. The marker REPLACES that agent's
	 * bar rather than stacking over it (see bHideBarWhenFailMarkerShown), and the marker quad is much
	 * taller than the bar, so matching the bar's offset floated it too high above the failure point.
	 * 50 sits it just clear of the floor at the spot conditions went untenable, which is what a reviewer
	 * is reading it against. Was 260 while the marker and bar were expected to coexist.
	 *
	 * The ANCHOR still differs from the bar's, and that is the whole point: the bar tracks the agent, the
	 * marker stays at the failure point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability", meta = (ClampMin = "0.0"))
	float FailMarkerHeightOffset = 50.0f;

	/**
	 * Drop an agent's tenability bar once its fail marker is actually drawn, so the marker replaces the
	 * bar instead of both showing at the same point.
	 *
	 * Conditional on the marker having been EMITTED for that same agent this frame, never merely on the
	 * agent having failed. That is what keeps this safe while the marker render path is unproven: if a
	 * marker cannot be drawn for any reason - master toggle off, per-type toggle off, no captured pose,
	 * mesh unregistered, marker off screen - the bar stays, so a failed agent can never end up with
	 * nothing shown at all. The suppression switches itself on the moment markers genuinely draw.
	 *
	 * Suppresses only the bar's INSTANCE. The agent keeps its debug label, which is where the failure
	 * diagnostics are read from.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Egress Tenability")
	bool bHideBarWhenFailMarkerShown = true;

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

	// The debug overlay is no longer a property. It was bShowDebugText, defaulting ON, which meant a text
	// label over every agent plus a corner summary plate during normal use, and a rebuild to change it.
	// It now lives entirely on the console CVar Mobius.Tenability.DebugText (default 0 = off):
	//   1 = per-agent labels + the FailMarkers summary plate
	//   2 = also boxes at each emitted fail marker's resolved position
	// Declared in SAgentEgressHealth.cpp next to the code that reads it. Per-agent text is not for large
	// crowds, so leave it off for performance runs.

protected:
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SAgentEgressTenability> SlateWidget;
	TArray<FAgentEgressTenabilityViewer> ManualAgentEgressHealthData;
	bool bUseManualAgentEgressHealthData = false;
};
