// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Slate/SMeshWidget.h"

class UAgentEgressTenabilityWidget;
class USlateVectorArtData;

/** Draws every active agent health bar in one hardware-instanced Slate mesh. */
class MOBIUSWIDGETS_API SAgentEgressTenability final : public SMeshWidget
{
public:
	SLATE_BEGIN_ARGS(SAgentEgressTenability)
		{
		}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UAgentEgressTenabilityWidget& InThis);
	void SetMeshAsset(USlateVectorArtData* InMeshAsset, int32 InitialInstanceCapacity);

	/**
	 * Registers the fail marker quad as a second instanced mesh. Must be called AFTER SetMeshAsset:
	 * Slate has no depth buffer, so the order meshes are registered in is what decides which paints
	 * on top, and markers belong above bars.
	 */
	void SetFailMarkerMeshAsset(USlateVectorArtData* InMeshAsset, int32 InitialInstanceCapacity);

protected:
	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	void ClearInstances();

	/** One screen-resolved agent marker, resolved and drawn as debug text in OnPaint. */
	struct FDebugMarker
	{
		FVector2D LocalPosition = FVector2D::ZeroVector;
		float Scale = 1.0f;
		float DisplayRisk = 0.0f;
		uint8 ShownCriterion = 0;
		float CurrentVisibilityM = 0.0f;
		float AccumulatedToxicFED = 0.0f;
		float AccumulatedThermalFED = 0.0f;
		float CurrentTemperatureC = 0.0f;
	};

	/**
	 * One fail marker resolved this frame, held until the whole set can be depth sorted.
	 * Instances cannot be submitted as they are found: submit order is z-order here.
	 */
	struct FPendingFailMarker
	{
		FVector4f InstanceData = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		float CameraDistance = 0.0f;
	};

	TWeakObjectPtr<UAgentEgressTenabilityWidget> ParentWidget;
	uint32 MeshId = MAX_uint32;
	uint32 FailMarkerMeshId = MAX_uint32;

	/** Per-agent debug values resolved this frame, drawn as text above each bar. */
	TArray<FDebugMarker> DebugMarkers;

	/**
	 * Fail markers awaiting the far-to-near sort. A member rather than a local so its capacity
	 * survives between frames and OnPaint allocates nothing; only failed agents land here, which is
	 * a handful rather than the whole crowd.
	 */
	TArray<FPendingFailMarker> PendingFailMarkers;
};
