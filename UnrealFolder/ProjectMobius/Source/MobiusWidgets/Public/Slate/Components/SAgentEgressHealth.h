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

	virtual void Tick(
		const FGeometry& AllottedGeometry,
		double InCurrentTime,
		float InDeltaTime) override;

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

	/** One screen-resolved agent marker, cached in Tick and drawn as debug text in OnPaint. */
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

	TWeakObjectPtr<UAgentEgressTenabilityWidget> ParentWidget;
	uint32 MeshId = MAX_uint32;

	/** Per-agent debug values resolved this frame, drawn as text above each bar. */
	TArray<FDebugMarker> DebugMarkers;
};
