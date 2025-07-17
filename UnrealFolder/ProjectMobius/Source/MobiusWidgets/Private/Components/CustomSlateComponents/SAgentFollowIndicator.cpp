// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/CustomSlateComponents/SAgentFollowIndicator.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "InWorldUI/AgentInfoDisplay.h"
#include "Slate/SlateVectorArtInstanceData.h"

void SAgentFollowIndicator::Construct(const FArguments& InArgs, UAgentInfoDisplay& InThis)
{
	ParentWidget = &InThis;
}

int32 SAgentFollowIndicator::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	//TODO: need to nullptr check for ParentWidget->this shouldn't happen as it is a child widget of the parent but improper removal may lead to this
	
	const int32 MeshId = ParentWidget->HoverWidgetMeshViewerID;

	// Get all the pedestrian agent data from the parent widget that we need for rendering
	const FAgentMeshViewer PedestrianAgentData = ParentWidget->SelectedAgentData;//This may not be thread safe, and may need to be protected with a mutex or similar if accessed from multiple threads

	// first create an empty layer
	int32 CurrentLayer = SMeshWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	
	
	if (MeshId == -1)
	{
		// No mesh to render
		return CurrentLayer;
	}

	// Create a buffer to hold the per-instance data
	FSlateInstanceBufferData PerInstanceUpdate;

	if (PedestrianAgentData.AgentID != -1)
	{
		FVector2D ScreenPosition = AllottedGeometry.GetAbsolutePositionAtCoordinates(FVector2f(0.5f, 0.5f));
		float SizeScale = ParentWidget->BaseSize / ParentWidget->ReferenceDistance;

		APlayerController* PlayerController = ParentWidget->GetOwningPlayer();
		bool bProjected = false;

		if (PlayerController && PlayerController->PlayerCameraManager)
		{
			FVector WorldLocation = PedestrianAgentData.AgentWorldPosition;
			// as our world location is at the feet of the agent we need to offset it by the height of the agent
			WorldLocation.Z += PedestrianAgentData.AgentHeight;

			const FVector CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
			const float Distance = FVector::Dist(CameraLocation, WorldLocation);

			bProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
				PlayerController, WorldLocation, ScreenPosition, false);

			FVector2D ProjectedPosition = ScreenPosition;

			if (bProjected && Distance > KINDA_SMALL_NUMBER)
			{
				SizeScale = FMath::Clamp(ParentWidget->ReferenceDistance / Distance, 0.0f, 5.0f);

				FVector2D TopLeft = AllottedGeometry.LocalToAbsolute(FVector2D::ZeroVector);

				ScreenPosition.Y = TopLeft.Y + FVector2D(ProjectedPosition * AllottedGeometry.Scale).Y;
				ScreenPosition.Y -= (ParentWidget->BaseSize * SizeScale) * 0.75f; // Offset to float above the mesh

				ScreenPosition.X = TopLeft.X + ProjectedPosition.X * AllottedGeometry.Scale;
			}

		}
		FSlateVectorArtInstanceData InstanceData;
		InstanceData.SetPosition(ScreenPosition);
		InstanceData.SetScale(SizeScale);
		PerInstanceUpdate.Add(
			TArray<UE::Math::TVector4<float>>::ElementType(InstanceData.GetData()));
	}


	const_cast<SAgentFollowIndicator*>(this)->UpdatePerInstanceBuffer(MeshId, PerInstanceUpdate);
	SMeshWidget::OnPaint(
				Args, AllottedGeometry, MyCullingRect,
				OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	return CurrentLayer;
}
