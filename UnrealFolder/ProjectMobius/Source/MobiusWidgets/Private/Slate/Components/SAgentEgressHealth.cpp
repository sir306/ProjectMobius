// Fill out your copyright notice in the Description page of Project Settings.

#include "Slate/Components/SAgentEgressHealth.h"

#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "SceneView.h"
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
}

void SAgentEgressTenability::Construct(const FArguments& InArgs, UAgentEgressTenabilityWidget& InThis)
{
	ParentWidget = &InThis;
	SetCanTick(true);
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

void SAgentEgressTenability::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SMeshWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	UAgentEgressTenabilityWidget* Widget = ParentWidget.Get();
	if (!Widget || MeshId == MAX_uint32)
	{
		return;
	}

	APlayerController* PlayerController = Widget->GetOwningPlayer();
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UGameViewportClient* ViewportClient = LocalPlayer ? LocalPlayer->ViewportClient : nullptr;
	if (!PlayerController || !ViewportClient || !ViewportClient->Viewport)
	{
		ClearInstances();
		return;
	}

	FSceneViewProjectionData ProjectionData;
	if (!LocalPlayer->GetProjectionData(ViewportClient->Viewport, ProjectionData))
	{
		ClearInstances();
		return;
	}

	FVector2D ViewportSize = FVector2D::ZeroVector;
	ViewportClient->GetViewportSize(ViewportSize);
	if (ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f)
	{
		ClearInstances();
		return;
	}

	const FMatrix ViewProjectionMatrix = ProjectionData.ComputeViewProjectionMatrix();
	const FIntRect ViewRect = ProjectionData.GetConstrainedViewRect();

	const TConstArrayView<FAgentEgressTenabilityViewer> AgentData = Widget->GetAgentEgressTenabilityData();
	FSlateInstanceBufferData PerInstanceUpdate;
	PerInstanceUpdate.Reserve(AgentData.Num());

	const bool bWantDebug = Widget->bShowDebugText;
	DebugMarkers.Reset(bWantDebug ? AgentData.Num() : 0);

	for (const FAgentEgressTenabilityViewer& Agent : AgentData)
	{
		if (Agent.AgentID < 0)
		{
			continue;
		}

		// Anchor at the agent origin; the above-head clearance is applied in screen space
		// below (see ScreenSpaceHeadOffset) so it never drifts with the camera angle.
		const FVector WorldLocation = Agent.AgentWorldPosition;

		FVector2D PixelPosition;
		if (!FSceneView::ProjectWorldToScreen(
				WorldLocation,
				ViewRect,
				ViewProjectionMatrix,
				PixelPosition)
			|| !PlayerController->PostProcessWorldToScreen(WorldLocation, PixelPosition, false))
		{
			continue;
		}

		if (PixelPosition.X < 0.0f || PixelPosition.Y < 0.0f
			|| PixelPosition.X > ViewportSize.X || PixelPosition.Y > ViewportSize.Y)
		{
			continue;
		}

		const double Distance = FVector::Dist(ProjectionData.ViewOrigin, WorldLocation);
		if (Distance <= UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}

		const float InstanceScale = FMath::Clamp(
			Widget->ReferenceDistance / static_cast<float>(Distance),
			Widget->MinimumScale,
			Widget->MaximumScale);

		// Lift the marker above the agent in screen space, scaled by the marker size so the
		// clearance tracks the agent's apparent height — a consistent gap above the head at any
		// distance, with none of the frustum-dependent drift a projected world-Z offset produced.
		PixelPosition.Y -= Widget->ScreenSpaceHeadOffset * InstanceScale;

		// Custom-vert meshes (SMeshWidget) are batched with NO render transform (see
		// FSlateElementBatcher::AddCustomVerts), so the instance position must be in absolute
		// window space (physical pixels). The game UMG layer is anchored to the viewport origin,
		// so the projected viewport pixel IS that absolute position. Deliberately NOT routed
		// through this widget's AllottedGeometry: this is a plain UWidget in a WBP slot that does
		// not fill the viewport, and scaling by (LocalSize/ViewportSize) compressed every marker
		// into the slot's sub-rect and made the overlay drift as the window resized. For a
		// full-viewport slot this value is identical to the old LocalToAbsolute() result.
		const FVector2D AbsolutePosition = PixelPosition;

		// Debug text goes through the normal (geometry-transformed) paint path, so convert the
		// absolute pixel back into this widget's local space regardless of where the slot sits.
		const FVector2D WidgetLocalPosition = AllottedGeometry.AbsoluteToLocal(AbsolutePosition);

		// Encode tenability into the single instance data scalar:
		//   value = ShownCriterion + clamp(DisplayRisk, 0, 0.999)
		// The material decodes floor() -> criterion (0=None..5=LayerHeight) for the
		// icon/accent colour, and frac() -> DisplayRisk for the bar fill length.
		// Use the live current dominant criterion so the bar reflects the scrubbed
		// time (the first-failure criterion is retained separately for ASET analytics).
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
			FDebugMarker& Marker = DebugMarkers.AddDefaulted_GetRef();
			Marker.LocalPosition = WidgetLocalPosition;
			Marker.Scale = InstanceScale;
			Marker.DisplayRisk = Agent.DisplayRisk;
			Marker.ShownCriterion = ShownCriterion;
			Marker.CurrentVisibilityM = Agent.CurrentVisibilityM;
			Marker.AccumulatedToxicFED = Agent.AccumulatedToxicFED;
			Marker.AccumulatedThermalFED = Agent.AccumulatedThermalFED;
			Marker.CurrentTemperatureC = Agent.CurrentTemperatureC;
		}
	}

	UpdatePerInstanceBuffer(MeshId, PerInstanceUpdate);
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
	int32 MaxLayerId = SMeshWidget::OnPaint(
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
	if (MeshId != MAX_uint32)
	{
		FSlateInstanceBufferData EmptyData;
		UpdatePerInstanceBuffer(MeshId, EmptyData);
	}
}
