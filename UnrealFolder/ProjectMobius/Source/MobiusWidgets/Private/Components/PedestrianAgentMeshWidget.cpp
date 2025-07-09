// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/PedestrianAgentMeshWidget.h"

#include "NiagaraCommon.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Fonts/FontMeasure.h"
#include "InWorldUI/AgentInfoDisplay.h"
#include "Slate/SlateVectorArtInstanceData.h"

void SPedestrianAgentMeshWidget::Construct(const FArguments& InArgs, UAgentInfoDisplay& InThis)
{
	ParentWidget = &InThis;
	Text = InArgs._Text;
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
		// No mesh to render
		return SMeshWidget::OnPaint(
			Args, AllottedGeometry, MyCullingRect,
			OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	//FVector2D ScreenPosition = FVector2D::ZeroVector; // fallback
	FVector2D ScreenPosition = AllottedGeometry.GetAbsolutePositionAtCoordinates(FVector2f(0.5f, 0.5f));
	float SizeScale = ParentWidget->BaseSize / ParentWidget->ReferenceDistance;

	APlayerController* PlayerController = ParentWidget->GetOwningPlayer();
	bool bProjected = false;

	if (PlayerController && PlayerController->PlayerCameraManager)
	{
		const FVector WorldLocation(0, 0, 170);
		const FVector CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
		const float Distance = FVector::Dist(CameraLocation, WorldLocation);

		bProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
			PlayerController, WorldLocation, ScreenPosition, false);
		FVector2D ProjectedPosition = ScreenPosition;
		if (bProjected && Distance > KINDA_SMALL_NUMBER)
		{
			SizeScale = FMath::Clamp(
				ParentWidget->ReferenceDistance / Distance,
				0.1f, 1.0f);

			// Get widget top-left in absolute coords
			FVector2D TopLeft = AllottedGeometry.LocalToAbsolute(FVector2D::ZeroVector);

			// Apply screen position offset properly
			ScreenPosition.Y = TopLeft.Y + FVector2D(ProjectedPosition * AllottedGeometry.Scale).Y;
			
			//ScreenPosition.X = ProjectedPosition.X - TopLeft.X;
			ScreenPosition.X = FVector2D(ProjectedPosition.X * AllottedGeometry.Scale).X;
		}
		
	}

	//TODO: this works but needs to be improved to offset the text properly and scale it to the size of the mesh,
	// also need to offset the vertical position of the mesh, based on the distance, as up close it works but distance causes it to overlap with the agent and not float above it properly.

	// Update Mesh Instance
	FSlateVectorArtInstanceData InstanceData;
	InstanceData.SetPosition(ScreenPosition);
	InstanceData.SetScale(SizeScale);

	FSlateInstanceBufferData PerInstanceUpdate;
	PerInstanceUpdate.Add(
		TArray<UE::Math::TVector4<float>>::ElementType(InstanceData.GetData()));

	const_cast<SPedestrianAgentMeshWidget*>(this)->UpdatePerInstanceBuffer(MeshId, PerInstanceUpdate);

	// Paint the mesh first
	int32 CurrentLayer = SMeshWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// Draw Text on top
	if (Text.IsSet() && !Text.Get().IsEmpty())
	{
		// draw text slightly above mesh
		FVector2D TextPosition = ScreenPosition;

		// adjust Y: move up (negative Y)
		TextPosition.Y -= 40.0f;

		FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Regular", 16);
		
		FVector2f LocalSize(TextPosition.X, TextPosition.Y);
		FSlateLayoutTransform LayoutTransform(TextPosition);

		FPaintGeometry PaintGeometry(
			LayoutTransform,
			LocalSize,
			TextPosition,
			false
		);
		
		FSlateDrawElement::MakeText(
			OutDrawElements,
			++CurrentLayer,
			PaintGeometry,
			Text.Get(),
			FontInfo,
			ESlateDrawEffect::None,
			FLinearColor::White);
	}

	return CurrentLayer;
}
