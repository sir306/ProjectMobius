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
	if (MeshId != MAX_uint32)
	{
		FSlateInstanceBufferData EmptyData;
		UpdatePerInstanceBuffer(MeshId, EmptyData);
	}
}
