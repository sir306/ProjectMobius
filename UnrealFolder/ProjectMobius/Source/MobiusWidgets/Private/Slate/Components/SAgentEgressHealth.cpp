// Fill out your copyright notice in the Description page of Project Settings.

#include "Slate/Components/SAgentEgressHealth.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Slate/SlateVectorArtData.h"
#include "Slate/SlateVectorArtInstanceData.h"
#include "Styling/CoreStyle.h"
#include "UI/InWorld/AgentEgressTenabilityWidget.h"

namespace
{
	const TCHAR* TenabilityCriterionLabel(const uint8 Criterion)
	{
		switch (Criterion)
		{
		case 1: return TEXT("VIS");
		case 2: return TEXT("TOX");
		case 3: return TEXT("THR");
		case 4: return TEXT("TMP");
		case 5: return TEXT("LYR");
		default: return TEXT("---");
		}
	}

	/**
	 * Criterion -> slot in T_TenabilityFailMarkerAtlas, which is 2x2 row-major from the top left.
	 * Returned as the float the instance scalar carries.
	 *
	 * Slot 3 is a diagnostic, not a hazard type. Temperature and LayerHeight both default off in
	 * FTenabilityAnalysisSettings, and a failure flag alongside criterion None is a timeline-builder
	 * bug; drawing a marker for those makes the case visible instead of leaving "no marker"
	 * indistinguishable from "no failure". It stands in for a log line because the only places the
	 * condition is detectable are this function's caller and the MASS processors, all per-frame paths
	 * where this project does not log.
	 *
	 * Values match ETenabilityCriterion, carried as uint8 in the viewer struct to keep MobiusCore free
	 * of a ProjectMobius dependency, so they are matched numerically here.
	 */
	float TenabilityFailMarkerAtlasSlot(const uint8 FirstFailureCriterion)
	{
		switch (FirstFailureCriterion)
		{
		case 3: return 0.0f; // ThermalFED
		case 2: return 1.0f; // ToxicFED
		case 1: return 2.0f; // Visibility
		default: return 3.0f; // Temperature, LayerHeight, None -> unattributed diagnostic
		}
	}
}

void SAgentEgressTenability::Construct(const FArguments& InArgs, UAgentEgressTenabilityWidget& InThis)
{
	ParentWidget = &InThis;
	// Volatile so OnPaint runs every frame (no cached invalidation); all instance resolution happens
	// there, matching the known-good SAgentFollowIndicator. No Tick override is needed.
	ForceVolatile(true);
}

void SAgentEgressTenability::SetMeshAsset(
	USlateVectorArtData* InMeshAsset,
	const int32 InitialInstanceCapacity)
{
	if (MeshId == MAX_uint32 && InMeshAsset)
	{
		MeshId = AddMeshWithInstancing(*InMeshAsset, FMath::Max(InitialInstanceCapacity, 1));
	}
}

void SAgentEgressTenability::SetFailMarkerMeshAsset(
	USlateVectorArtData* InMeshAsset,
	const int32 InitialInstanceCapacity)
{
	if (FailMarkerMeshId == MAX_uint32 && InMeshAsset)
	{
		FailMarkerMeshId = AddMeshWithInstancing(*InMeshAsset, FMath::Max(InitialInstanceCapacity, 1));

		// Captured once here, not per paint. See the members' docs: a quad authored upright collapses to
		// zero area once Slate drops Z, and this is the only place that shows it.
		FailMarkerMeshExtent = InMeshAsset->GetDesiredSize();
		FailMarkerMeshVertexCount = InMeshAsset->GetVertexData().Num();
	}
}

int32 SAgentEgressTenability::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	// Resolve + submit the instance buffer HERE (not in Tick). SMeshWidget's custom verts are batched
	// with no render transform, so the position must be in this paint pass's absolute space; resolving
	// against the paint-space AllottedGeometry (rather than the tick-space geometry, whose accumulated
	// scale differs) is what makes the marker track correctly across window size, DPI, and camera moves.
	// This mirrors the known-good SAgentFollowIndicator exactly.
	SAgentEgressTenability* MutableThis = const_cast<SAgentEgressTenability*>(this);

	UAgentEgressTenabilityWidget* Widget = ParentWidget.Get();
	APlayerController* PlayerController = Widget ? Widget->GetOwningPlayer() : nullptr;

	if (!Widget || MeshId == MAX_uint32 || !PlayerController || !PlayerController->PlayerCameraManager)
	{
		MutableThis->ClearInstances();
		return SMeshWidget::OnPaint(
			Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	const TConstArrayView<FAgentEgressTenabilityViewer> AgentData = Widget->GetAgentEgressTenabilityData();
	FSlateInstanceBufferData PerInstanceUpdate;
	PerInstanceUpdate.Reserve(AgentData.Num());

	const bool bWantDebug = Widget->bShowDebugText;
	MutableThis->DebugMarkers.Reset(bWantDebug ? AgentData.Num() : 0);

	// Reset() keeps the allocation, so the marker scratch costs nothing per frame after the first.
	const bool bWantFailMarkers = Widget->bShowFailMarkers && FailMarkerMeshId != MAX_uint32;
	MutableThis->PendingFailMarkers.Reset();
	MutableThis->FailMarkerStats = FFailMarkerDebugStats();

	const FVector2D WidgetTopLeft = AllottedGeometry.LocalToAbsolute(FVector2D::ZeroVector);
	const FVector CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();

	for (const FAgentEgressTenabilityViewer& Agent : AgentData)
	{
		if (Agent.AgentID < 0)
		{
			continue;
		}

		// Fail markers are resolved BEFORE the bar's own guards below, deliberately. A marker is
		// pinned where the failure happened, so it must not depend on the agent still being visible:
		// the agent walks on, can leave every modelled room (clearing bHasTenabilityData), or can pass
		// behind the camera, and its failure point may still be on screen. Emitting after those
		// continues made the marker vanish along with the agent, which defeats a forensic pin.
		// Tallied before the emission gate, and independent of it, so the summary still reports what
		// the data contains when markers are toggled off or the mesh never registered.
		if (Agent.bTenabilityFailed)
		{
			++MutableThis->FailMarkerStats.FailedAgents;
			if (!Agent.FailureLocation.IsNearlyZero())
			{
				++MutableThis->FailMarkerStats.WithPose;
			}
		}

		// Fail marker. Anchored at FailureLocation, NOT at the agent, so it needs its own projection —
		// the agent walks on after failing while the marker stays where conditions went untenable.
		// That extra projection is paid only for agents that have actually failed, not per agent.
		//
		// FailureMask is NOT a has-failed test: an agent that has not failed retains the instantaneous
		// current-frame mask, which is routinely non-zero. bTenabilityFailed is the only such test.
		// The zero-location guard is not paranoia either — see FAgentEgressTenabilityViewer::
		// FailureLocation for the two windows where a genuinely failed agent has no pose yet.
		if (bWantFailMarkers && Agent.bTenabilityFailed && !Agent.FailureLocation.IsNearlyZero())
		{
			// Resolve the slot before projecting. The per-type gate keys off the icon that would be
			// drawn — FirstFailureCriterion via its slot — and NOT off FailureMask, which holds every
			// simultaneously failed criterion and would let "hide thermal" remove a marker visibly
			// showing the gas icon. Testing it first also means a hidden type costs no projection.
			const float AtlasSlot = TenabilityFailMarkerAtlasSlot(Agent.FirstFailureCriterion);

			FVector MarkerWorldLocation = Agent.FailureLocation;
			MarkerWorldLocation.Z += Widget->FailMarkerHeightOffset;

			FVector2D MarkerViewportPosition;
			const double MarkerDistance = FVector::Dist(CameraLocation, MarkerWorldLocation);
			if (Widget->IsFailMarkerSlotVisible(static_cast<int32>(AtlasSlot))
				&& MarkerDistance > UE_DOUBLE_SMALL_NUMBER
				&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
					PlayerController, MarkerWorldLocation, MarkerViewportPosition,
					/*bPlayerViewportRelative*/ false))
			{
				// FailMarkerMinimumScale, not the bar's MinimumScale: markers need a much higher floor
				// to stay legible, and the bar's default of 0.05 would render one at 1.6 px.
				const float MarkerScale = FMath::Clamp(
					Widget->ReferenceDistance / static_cast<float>(MarkerDistance),
					Widget->FailMarkerMinimumScale,
					Widget->MaximumScale);

				const FVector2D MarkerAbsolutePosition(
					WidgetTopLeft.X + MarkerViewportPosition.X * AllottedGeometry.Scale,
					WidgetTopLeft.Y + MarkerViewportPosition.Y * AllottedGeometry.Scale);

				// FirstFailureCriterion, never CurrentDominantCriterion: risks keep evolving after the
				// failure, so an icon driven by the live dominant criterion flickers between types.
				FSlateVectorArtInstanceData MarkerInstanceData;
				MarkerInstanceData.SetPosition(MarkerAbsolutePosition);
				MarkerInstanceData.SetScale(MarkerScale);
				MarkerInstanceData.SetBaseAddress(AtlasSlot);

				FPendingFailMarker& Pending = MutableThis->PendingFailMarkers.AddDefaulted_GetRef();
				Pending.InstanceData = FVector4f(MarkerInstanceData.GetData());
				Pending.CameraDistance = static_cast<float>(MarkerDistance);
				++MutableThis->FailMarkerStats.Emitted;
			}
		}

		// No measurement for this agent -> draw nothing. The instance encoding below packs criterion
		// and risk into a single scalar, so "no data" has no representable value in it: criterion
		// None with risk 0 is exactly what a measured-and-clear agent produces. Skipping the instance
		// is therefore the only way to keep the absence of data from reading as a clean bill of
		// health. Agents outside every modelled room, and every agent during a timeline rebuild, land
		// here; an agent carrying dose or one that has failed does not.
		if (!Agent.bHasTenabilityData)
		{
			continue;
		}

		// Anchor at the agent's head, then project with the DPI-aware UMG projection. It returns false
		// when the point is behind the camera (replacing the old off-screen pixel-bounds cull).
		FVector WorldLocation = Agent.AgentWorldPosition;
		WorldLocation.Z += Widget->WorldHeightOffset;

		FVector2D ViewportPosition;
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
				PlayerController, WorldLocation, ViewportPosition, /*bPlayerViewportRelative*/ false))
		{
			continue;
		}

		const double Distance = FVector::Dist(CameraLocation, WorldLocation);
		if (Distance <= UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}

		const float InstanceScale = FMath::Clamp(
			Widget->ReferenceDistance / static_cast<float>(Distance),
			Widget->MinimumScale,
			Widget->MaximumScale);

		// Viewport-logical -> absolute paint space: window top-left + logical * render scale
		// (identical to SAgentFollowIndicator::CreateRenderMeshData).
		const FVector2D AbsolutePosition(
			WidgetTopLeft.X + ViewportPosition.X * AllottedGeometry.Scale,
			WidgetTopLeft.Y + ViewportPosition.Y * AllottedGeometry.Scale);

		// Encode tenability into the single instance data scalar:
		//   value = ShownCriterion + clamp(DisplayRisk, 0, 0.999)
		// The material decodes floor() -> criterion (0=None..5=LayerHeight) for the
		// icon/accent colour, and frac() -> DisplayRisk for the bar fill length.
		const uint8 ShownCriterion = Agent.CurrentDominantCriterion;
		const float EncodedTenability =
			static_cast<float>(ShownCriterion) + FMath::Clamp(Agent.DisplayRisk, 0.0f, 0.999f);

		FSlateVectorArtInstanceData InstanceData;
		InstanceData.SetPosition(AbsolutePosition);
		InstanceData.SetScale(InstanceScale);
		InstanceData.SetBaseAddress(EncodedTenability);
		PerInstanceUpdate.Add(FVector4f(InstanceData.GetData()));


		if (bWantDebug)
		{
			FDebugMarker& Marker = MutableThis->DebugMarkers.AddDefaulted_GetRef();
			Marker.LocalPosition = AllottedGeometry.AbsoluteToLocal(AbsolutePosition);
			Marker.Scale = InstanceScale;
			Marker.DisplayRisk = Agent.DisplayRisk;
			Marker.ShownCriterion = ShownCriterion;
			Marker.CurrentVisibilityM = Agent.CurrentVisibilityM;
			Marker.AccumulatedToxicFED = Agent.AccumulatedToxicFED;
			Marker.AccumulatedThermalFED = Agent.AccumulatedThermalFED;
			Marker.CurrentTemperatureC = Agent.CurrentTemperatureC;
			Marker.bTenabilityFailed = Agent.bTenabilityFailed;
			Marker.bHasFailurePose = !Agent.FailureLocation.IsNearlyZero();
			Marker.FirstFailureCriterion = Agent.FirstFailureCriterion;
			Marker.FirstFailureTimeSeconds = Agent.FirstFailureTimeSeconds;
			Marker.TimelineIntervalCount = Agent.TimelineIntervalCount;
		}
	}

	MutableThis->UpdatePerInstanceBuffer(MeshId, PerInstanceUpdate);

	if (FailMarkerMeshId != MAX_uint32)
	{
		// Slate has no depth buffer. SMeshWidget batches instances into custom verts and draws them in
		// submit order, so instance order IS z-order: submit far first and the nearest marker paints
		// last, on top. The sort key is the marker's own point, which is why CameraDistance was
		// measured against FailureLocation and not reused from the bar loop's agent distance.
		MutableThis->PendingFailMarkers.Sort(
			[](const FPendingFailMarker& A, const FPendingFailMarker& B)
			{
				return A.CameraDistance > B.CameraDistance;
			});

		// With the master toggle off this submits empty, which costs one call and keeps the mesh
		// resident so switching back on needs no reallocation.
		FSlateInstanceBufferData FailMarkerUpdate;
		FailMarkerUpdate.Reserve(PendingFailMarkers.Num());
		for (const FPendingFailMarker& Pending : PendingFailMarkers)
		{
			FailMarkerUpdate.Add(Pending.InstanceData);
		}
		MutableThis->UpdatePerInstanceBuffer(FailMarkerMeshId, FailMarkerUpdate);
	}

	const int32 MaxLayerId = SMeshWidget::OnPaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		LayerId,
		InWidgetStyle,
		bParentEnabled);

	if (!bWantDebug)
	{
		return MaxLayerId;
	}

	const int32 TextLayer = MaxLayerId + 1;
	// Summary sits ABOVE the per-agent labels. Agents at similar screen height produce labels that all
	// land in one narrow band, and the summary used to share their layer at the widget's own top-left —
	// so the one line that explains a missing marker was the line buried under every other label.
	const int32 SummaryLayer = MaxLayerId + 2;
	const FSlateFontInfo DebugFont = FCoreStyle::GetDefaultFontStyle("Bold", 7);
	const FLinearColor TextColour(0.0f, 0.0f, 0.0f, 0.95f);

	// Where a marker WOULD be drawn, boxed on the debug layer. This is the test that splits the last
	// two rows of the diagnostic table apart, which no counter can: the box is drawn from the same
	// absolute position and the same emitted instance list the mesh uses, so
	//   box visible, no icon -> position and emission are correct; the mesh, material or quad is at
	//                           fault (winding / UV orientation is then the prime suspect)
	//   no box at all        -> emission or the marker's own projection is at fault, not the art
	// Positions come straight back out of the packed instance data (XY = absolute position, W = atlas
	// slot), so this needs no extra per-frame storage.
	for (const FPendingFailMarker& Pending : PendingFailMarkers)
	{
		const FVector2D MarkerAbsolute(Pending.InstanceData.X, Pending.InstanceData.Y);
		const FVector2D MarkerLocal = AllottedGeometry.AbsoluteToLocal(MarkerAbsolute);
		const FVector2D BoxSize(24.0f, 24.0f);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			TextLayer,
			AllottedGeometry.ToPaintGeometry(
				BoxSize, FSlateLayoutTransform(MarkerLocal - BoxSize * 0.5f)),
			FCoreStyle::Get().GetBrush("Debug.Border"),
			ESlateDrawEffect::None,
			FLinearColor(1.0f, 0.0f, 1.0f, 1.0f));

		FSlateDrawElement::MakeText(
			OutDrawElements,
			TextLayer,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(40.0f, 12.0f),
				FSlateLayoutTransform(MarkerLocal + FVector2D(14.0f, -6.0f))),
			FString::Printf(TEXT("s%d"), static_cast<int32>(Pending.InstanceData.W)),
			DebugFont,
			ESlateDrawEffect::None,
			FLinearColor(1.0f, 0.0f, 1.0f, 1.0f));
	}

	// One fail marker summary, drawn even when no agent qualifies. Separates the reasons a marker can
	// be absent, which are otherwise indistinguishable on screen: nothing has failed, the failures have
	// no captured pose yet, the marker mesh never registered, or a toggle rejected them. Given an
	// opaque plate and its own layer so an overlapping agent label can never hide it.
	{
		const FString Summary = FString::Printf(
			TEXT("FailMarkers: mesh=%s show=%s quad=%.1fx%.1f v%d | agents=%d failed=%d posed=%d emitted=%d"),
			FailMarkerMeshId == MAX_uint32 ? TEXT("UNREGISTERED") : TEXT("ok"),
			Widget->bShowFailMarkers ? TEXT("yes") : TEXT("NO"),
			FailMarkerMeshExtent.X,
			FailMarkerMeshExtent.Y,
			FailMarkerMeshVertexCount,
			AgentData.Num(),
			FailMarkerStats.FailedAgents,
			FailMarkerStats.WithPose,
			FailMarkerStats.Emitted);

		const FVector2D SummarySize(780.0f, 16.0f);
		const FVector2D SummaryPos(8.0f, 8.0f);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			SummaryLayer,
			AllottedGeometry.ToPaintGeometry(SummarySize, FSlateLayoutTransform(SummaryPos)),
			FCoreStyle::Get().GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			FLinearColor(1.0f, 1.0f, 0.0f, 1.0f));

		FSlateDrawElement::MakeText(
			OutDrawElements,
			SummaryLayer,
			AllottedGeometry.ToPaintGeometry(
				SummarySize, FSlateLayoutTransform(SummaryPos + FVector2D(2.0f, 1.0f))),
			Summary,
			DebugFont,
			ESlateDrawEffect::None,
			TextColour);
	}

	if (DebugMarkers.Num() == 0)
	{
		return SummaryLayer;
	}

	for (const FDebugMarker& Marker : DebugMarkers)
	{
		// "<live CRIT> R<risk> v<vis> fT<toxic> fR<thermal> T<temp> F<failed> P<has pose>
		//  <first-failure CRIT> t<first failure time> i<interval count>". The trailing group is what
		// tells a missing marker apart: F0 means the timeline never recorded a failure, so risk
		// saturating at R1.00 is irrelevant; F1 P0 means it failed but its pose has not been captured
		// yet; and on an F0 agent, i0 says why — no room-occupancy span for the offline solver to
		// search, so it is a room-attribution gap, not a criteria-threshold question.
		const FString Line = FString::Printf(
			TEXT("%s R%.2f v%.0f fT%.2f fR%.2f T%.0f F%d P%d %s t%.1f i%d"),
			TenabilityCriterionLabel(Marker.ShownCriterion),
			Marker.DisplayRisk,
			Marker.CurrentVisibilityM,
			Marker.AccumulatedToxicFED,
			Marker.AccumulatedThermalFED,
			Marker.CurrentTemperatureC,
			Marker.bTenabilityFailed ? 1 : 0,
			Marker.bHasFailurePose ? 1 : 0,
			TenabilityCriterionLabel(Marker.FirstFailureCriterion),
			Marker.FirstFailureTimeSeconds,
			Marker.TimelineIntervalCount);

		// Place the label just above the bar (bar sits at the projected point).
		const FVector2D TextPos = Marker.LocalPosition - FVector2D(40.0f * Marker.Scale, 14.0f * Marker.Scale);
		FSlateDrawElement::MakeText(
			OutDrawElements,
			TextLayer,
			AllottedGeometry.ToPaintGeometry(FVector2D(340.0f, 12.0f), FSlateLayoutTransform(TextPos)),
			Line,
			DebugFont,
			ESlateDrawEffect::None,
			TextColour);
	}

	return SummaryLayer;
}

void SAgentEgressTenability::ClearInstances()
{
	DebugMarkers.Reset();
	PendingFailMarkers.Reset();
	if (MeshId != MAX_uint32)
	{
		FSlateInstanceBufferData EmptyData;
		UpdatePerInstanceBuffer(MeshId, EmptyData);
	}
	if (FailMarkerMeshId != MAX_uint32)
	{
		FSlateInstanceBufferData EmptyData;
		UpdatePerInstanceBuffer(FailMarkerMeshId, EmptyData);
	}
}
