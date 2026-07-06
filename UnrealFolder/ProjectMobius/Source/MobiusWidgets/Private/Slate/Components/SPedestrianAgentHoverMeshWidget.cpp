// Fill out your copyright notice in the Description page of Project Settings.


#include "Slate/Components/SPedestrianAgentHoverMeshWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Fonts/FontMeasure.h"
#include "UI/InWorld/AgentInfoDisplay.h"
#include "Slate/SlateVectorArtInstanceData.h"

void SPedestrianAgentHoverMeshWidget::Construct(const FArguments& InArgs, UAgentInfoDisplay& InThis)
{
	ParentWidget = &InThis;
	Text = InArgs._Text;
}

int32 SPedestrianAgentHoverMeshWidget::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	if (!ParentWidget)
	{
		// Shouldn't happen (child widget of ParentWidget), but improper removal could leave this dangling.
		return SMeshWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	const int32 MeshId = ParentWidget->HoverWidgetMeshViewerID;

	// Get all the pedestrian agent data from the parent widget that we need for rendering
	const TArray<FAgentMeshViewer> PedestrianAgentData = ParentWidget->PedestrianHoverAgentData;//This may not be thread safe, and may need to be protected with a mutex or similar if accessed from multiple threads

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
	//FSlateElementBatcher* Batcher = ICustomSlateElement::
	//Batcher.AddTextElement

	if (PedestrianAgentData.Num() != 0)
	{
		// loop through the agent data and update the mesh instance data in reverse order
		for (int32 i = PedestrianAgentData.Num() - 1; i >= 0; --i)
		{
			FAgentMeshViewer AgentData = PedestrianAgentData[i];

			if (ParentWidget->SelectedAgentData.AgentID == AgentData.AgentID)
			{
				// If this is the selected agent, we want to draw the follow indicator
				// This will be handled by the SAgentFollowIndicator widget
				continue;
			}

			FVector2D ScreenPosition = AllottedGeometry.GetAbsolutePositionAtCoordinates(FVector2f(0.5f, 0.5f));
			float SizeScale = ParentWidget->BaseSize / ParentWidget->ReferenceDistance;

			APlayerController* PlayerController = ParentWidget->GetOwningPlayer();
			bool bProjected = false;

			if (PlayerController && PlayerController->PlayerCameraManager)
			{
				FVector WorldLocation = AgentData.AgentWorldPosition;
				// as our world location is at the feet of the agent we need to offset it by the height of the agent
				WorldLocation.Z += AgentData.AgentHeight;

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

				BuildUIFromAgentData(
					AgentData, PerInstanceUpdate, CurrentLayer,
					ScreenPosition, SizeScale, Args,
					AllottedGeometry, MyCullingRect,
					OutDrawElements, LayerId,
					InWidgetStyle, bParentEnabled);
			}
		}
	}
	
	const_cast<SPedestrianAgentHoverMeshWidget*>(this)->UpdatePerInstanceBuffer(MeshId, PerInstanceUpdate);
	SMeshWidget::OnPaint(
				Args, AllottedGeometry, MyCullingRect,
				OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	return CurrentLayer;
}

void SPedestrianAgentHoverMeshWidget::BuildUIFromAgentData(const FAgentMeshViewer& AgentData, FSlateInstanceBufferData& PerInstanceUpdate, int32& CurrentLayer,
                                                      FVector2D& ScreenPosition, float& SizeScale,const FPaintArgs& Args,
                                                      const FGeometry& AllottedGeometry,
                                                      const FSlateRect& MyCullingRect,
                                                      FSlateWindowElementList& OutDrawElements,
                                                      int32& LayerId,
                                                      const FWidgetStyle& InWidgetStyle,
                                                      bool bParentEnabled) const
{
	FText AgentText = CreateUITextFromAgentData(AgentData);
	
	// Draw Text on top
	if (!AgentText.IsEmpty())
	{
		// Desired box dimensions in pixels
		float BoxWidth = ParentWidget->BaseSize * SizeScale;
		float BoxHeight = ParentWidget->BaseSize * SizeScale;

		// We want 15% padding around the text width, so we multiply the dimensions by 0.85
		BoxWidth *= 0.85f;

		// Measure text at default size
		int32 DefaultFontSize = 16;
		FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Regular", DefaultFontSize);

		TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		FVector2D TextSize = FontMeasureService->Measure(AgentText, FontInfo);

		// Compute scale factor to fit in box (maintain aspect ratio)
		float ScaleX = BoxWidth / TextSize.X;
		float ScaleY = BoxHeight / TextSize.Y;
		float UniformScale = FMath::Min(ScaleX, ScaleY);

		// Adjust font size
		int32 FinalFontSize = FMath::Clamp(FMath::FloorToInt(DefaultFontSize * UniformScale), 0, 64);

		// Update Mesh Instance
		FSlateVectorArtInstanceData InstanceData;
		InstanceData.SetPosition(ScreenPosition);
		InstanceData.SetScale(SizeScale);
		PerInstanceUpdate.Add(
			TArray<UE::Math::TVector4<float>>::ElementType(InstanceData.GetData()));

		// TODO: we should add small white placeholder text like ---
		// to indicate the fields but at the font size of 1-5 it wont be readable
		
		
		// if the font is below 6, -> we won't be able to read it but if its below 6 we should at least render the box
		// to give some visual feedback to the user
		if (FinalFontSize >= 6)
		{
			

			// SMeshWidget::OnPaint(
			// 	Args, AllottedGeometry, MyCullingRect,
			// 	OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
		
			FontInfo.Size = FinalFontSize;

			// Re-measure at new font size if needed
			TextSize = FontMeasureService->Measure(AgentText, FontInfo);

			float VerticalOffset = BoxHeight * 0.5f;
			//float VerticalOffset = BoxHeight / 2.0f;

			// Center text horizontally
			FVector2D TextPosition = ScreenPosition;
			//TextPosition.X -= TextSize.X * 0.5f;
			//TextPosition.Y -= (TextSize.Y * 0.5f); 
			TextPosition -= TextSize * 0.5f; 
			
			// Now draw with PaintGeometry (no scale in LayoutTransform!)
			FSlateLayoutTransform LayoutTransform(TextPosition);

			FVector2f LocalSize(TextPosition.X, TextPosition.Y);

			FPaintGeometry PaintGeometry(
				LayoutTransform,
				LocalSize,
				TextPosition,
				false
			);
			
		
			FSlateDrawElement::MakeText(
				OutDrawElements,
				1 + LayerId,
				//++CurrentLayer,
				PaintGeometry,
				AgentText,
				FontInfo,
				ESlateDrawEffect::None,
				FLinearColor::White);
		}
	}
}

FText SPedestrianAgentHoverMeshWidget::CreateUITextFromAgentData(const FAgentMeshViewer& AgentData) const
{
	
	return FText::Format(
			NSLOCTEXT("PedestrianAgentMeshWidget", "AgentInfoFormat",
					  "ID: {0}\n"
					  "Name: {1}\n"
					  "Gender: {2}\n"
					  "Demographic: {3}\n"
					  "Speed: {4} m/s\n"
					  "Gait Speed: {5} m/s\n"
					  "Height: {6} cm\n"
					  "Position: {7}, {8}, {9}"),
			FText::AsNumber(AgentData.AgentID),
			AgentData.AgentName,
			AgentData.Gender,
			AgentData.Demographic,
			FText::AsNumber(AgentData.AgentSpeed),
			FText::AsNumber(AgentData.GaitDirectionalSpeed),
			FText::AsNumber(AgentData.AgentHeight),
			FText::AsNumber(AgentData.AgentWorldPosition.X),
			FText::AsNumber(AgentData.AgentWorldPosition.Y),
			FText::AsNumber(AgentData.AgentWorldPosition.Z)
		);
}
