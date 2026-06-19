// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Slate/SMeshWidget.h"

class UAgentEgressHealthWidget;
class USlateVectorArtData;

/** Draws every active agent health bar in one hardware-instanced Slate mesh. */
class MOBIUSWIDGETS_API SAgentEgressHealth final : public SMeshWidget
{
public:
	SLATE_BEGIN_ARGS(SAgentEgressHealth)
		{
		}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UAgentEgressHealthWidget& InThis);
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

	TWeakObjectPtr<UAgentEgressHealthWidget> ParentWidget;
	uint32 MeshId = MAX_uint32;
};
