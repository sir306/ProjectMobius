// Fill out your copyright notice in the Description page of Project Settings.

#include "Slate/Components/SAgentEgressHealth.h"

#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "Slate/SlateVectorArtData.h"
#include "Slate/SlateVectorArtInstanceData.h"
#include "UI/InWorld/AgentEgressHealthWidget.h"

void SAgentEgressHealth::Construct(const FArguments& InArgs, UAgentEgressHealthWidget& InThis)
{
	ParentWidget = &InThis;
	SetCanTick(true);
	ForceVolatile(true);
}

void SAgentEgressHealth::SetMeshAsset(
	USlateVectorArtData* InMeshAsset,
	const int32 InitialInstanceCapacity)
{
	if (MeshId == MAX_uint32 && InMeshAsset)
	{
		MeshId = AddMeshWithInstancing(*InMeshAsset, FMath::Max(InitialInstanceCapacity, 1));
	}
}

void SAgentEgressHealth::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SMeshWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	UAgentEgressHealthWidget* Widget = ParentWidget.Get();
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
	const FVector2D LocalWidgetSize = AllottedGeometry.GetLocalSize();
	if (ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f
		|| LocalWidgetSize.X <= 0.0f || LocalWidgetSize.Y <= 0.0f)
	{
		ClearInstances();
		return;
	}

	const FMatrix ViewProjectionMatrix = ProjectionData.ComputeViewProjectionMatrix();
	const FIntRect ViewRect = ProjectionData.GetConstrainedViewRect();
	const FVector2D PixelToWidgetScale(
		LocalWidgetSize.X / ViewportSize.X,
		LocalWidgetSize.Y / ViewportSize.Y);

	const TConstArrayView<FAgentEgressHealthViewer> AgentData = Widget->GetAgentEgressHealthData();
	FSlateInstanceBufferData PerInstanceUpdate;
	PerInstanceUpdate.Reserve(AgentData.Num());

	for (const FAgentEgressHealthViewer& Agent : AgentData)
	{
		if (Agent.AgentID < 0)
		{
			continue;
		}

		FVector WorldLocation = Agent.AgentWorldPosition;
		WorldLocation.Z += Widget->WorldHeightOffset;

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

		const FVector2D WidgetLocalPosition = PixelPosition * PixelToWidgetScale;
		const FVector2D AbsolutePosition = AllottedGeometry.LocalToAbsolute(WidgetLocalPosition);

		FSlateVectorArtInstanceData InstanceData;
		InstanceData.SetPosition(AbsolutePosition);
		InstanceData.SetScale(InstanceScale);
		InstanceData.SetBaseAddress(FMath::Clamp(Agent.AgentEgressHealth, 0.0f, 1.0f));
		PerInstanceUpdate.Add(FVector4f(InstanceData.GetData()));
	}

	UpdatePerInstanceBuffer(MeshId, PerInstanceUpdate);
}

int32 SAgentEgressHealth::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	return SMeshWidget::OnPaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		LayerId,
		InWidgetStyle,
		bParentEnabled);
}

void SAgentEgressHealth::ClearInstances()
{
	if (MeshId != MAX_uint32)
	{
		FSlateInstanceBufferData EmptyData;
		UpdatePerInstanceBuffer(MeshId, EmptyData);
	}
}
