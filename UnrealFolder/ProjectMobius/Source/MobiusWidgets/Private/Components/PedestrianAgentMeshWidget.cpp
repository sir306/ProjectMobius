// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/PedestrianAgentMeshWidget.h"

#include "NiagaraCommon.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "InWorldUI/AgentInfoDisplay.h"
#include "Slate/SlateVectorArtInstanceData.h"

void SPedestrianAgentMeshWidget::Construct(const FArguments& InArgs, UAgentInfoDisplay& InThis)
{
	ParentWidget = &InThis;
}

int32 SPedestrianAgentMeshWidget::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const int32 MeshId = ParentWidget->AgentID;

	if (MeshId == -1)
	{
		// No mesh, nothing to draw
		return SMeshWidget::OnPaint(
			Args, AllottedGeometry, MyCullingRect,
			OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	

	// Defaults
	//FVector2D ScreenPosition = FVector2D(FVector2D::ZeroVector); // Center of widget
	FVector2D ScreenPosition = FVector2D(AllottedGeometry.GetLayoutBoundingRect().GetCenter2f().X,AllottedGeometry.GetLayoutBoundingRect().GetCenter2f().Y); // Center of widget
	float SizeScale = ParentWidget->BaseSize / ParentWidget->ReferenceDistance;
	//SizeScale = 1.0f; // Reset to 1.0f for now
	
	APlayerController* PlayerController = ParentWidget->GetOwningPlayer();
	bool bProjected = false;

	if (PlayerController && PlayerController->PlayerCameraManager)
	{
		FVector WorldLocation(0, 0, 0); // World origin

		
		const FVector CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
		const float Distance = FVector::Dist(CameraLocation, WorldLocation);
		//WorldLocation += FVector(((ParentWidget->BaseSize*0.5f) *ParentWidget->ReferenceDistance / Distance ), ((ParentWidget->BaseSize*0.5f) *ParentWidget->ReferenceDistance / Distance ), 0);

		bProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
			PlayerController, WorldLocation, ScreenPosition, false);

		if (bProjected && Distance > KINDA_SMALL_NUMBER)
		{
			// Compute scale based on distance
			SizeScale = FMath::Clamp(
				ParentWidget->ReferenceDistance / Distance,
				0.1f, 1.0f);
		}
	}

	// Prepare instance data
	FSlateVectorArtInstanceData InstanceData;
	InstanceData.SetPosition(ScreenPosition);
	InstanceData.SetScale(SizeScale);

	FSlateInstanceBufferData PerInstanceUpdate;
	PerInstanceUpdate.Add(
		TArray<UE::Math::TVector4<float>>::ElementType(InstanceData.GetData()));

	// Update per-instance buffer
	const_cast<SPedestrianAgentMeshWidget*>(this)->UpdatePerInstanceBuffer(MeshId, PerInstanceUpdate);

	return SMeshWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}