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

	const FVector2D WidgetTopLeft = AllottedGeometry.LocalToAbsolute(FVector2D::ZeroVector);
	const FVector CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();

	for (const FAgentEgressTenabilityViewer& Agent : AgentData)
	{
		if (Agent.AgentID < 0)
		{
			continue;
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
			}
		}

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

	if (DebugMarkers.Num() == 0)
	{
		return MaxLayerId;
	}

	// Per-agent debug text: "<CRIT> R<DisplayRisk> v<vis> fT<toxic> fR<thermal> T<temp>".
	const int32 TextLayer = MaxLayerId + 1;
	const FSlateFontInfo DebugFont = FCoreStyle::GetDefaultFontStyle("Bold", 7);
	const FLinearColor TextColour(0.0f, 0.0f, 0.0f, 0.95f);

	for (const FDebugMarker& Marker : DebugMarkers)
	{
		const FString Line = FString::Printf(
			TEXT("%s R%.2f v%.0f fT%.2f fR%.2f T%.0f"),
			TenabilityCriterionLabel(Marker.ShownCriterion),
			Marker.DisplayRisk,
			Marker.CurrentVisibilityM,
			Marker.AccumulatedToxicFED,
			Marker.AccumulatedThermalFED,
			Marker.CurrentTemperatureC);

		// Place the label just above the bar (bar sits at the projected point).
		const FVector2D TextPos = Marker.LocalPosition - FVector2D(40.0f * Marker.Scale, 14.0f * Marker.Scale);
		FSlateDrawElement::MakeText(
			OutDrawElements,
			TextLayer,
			AllottedGeometry.ToPaintGeometry(FVector2D(160.0f, 12.0f), FSlateLayoutTransform(TextPos)),
			Line,
			DebugFont,
			ESlateDrawEffect::None,
			TextColour);
	}

	return TextLayer;
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
