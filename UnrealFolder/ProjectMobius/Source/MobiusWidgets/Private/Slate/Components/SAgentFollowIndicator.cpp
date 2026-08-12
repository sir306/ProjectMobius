// Fill out your copyright notice in the Description page of Project Settings.


#include "Slate/Components/SAgentFollowIndicator.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "UI/InWorld/AgentInfoDisplay.h"
#include "Slate/SlateVectorArtInstanceData.h"

void SAgentFollowIndicator::Construct(const FArguments& InArgs, UAgentInfoDisplay& InThis)
{
	ParentWidget = &InThis;
	FollowIndicator = InArgs._FollowIndicator.Get();

	if (FollowIndicator)
	{
		// Load Follow Texture from asset
		IconTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Script/Engine.Texture2D'/Game/01_Dev/Widgets/WidgetMaterials/Textures/T_DownArrow.T_DownArrow'"));
	}
	else
	{
		// Load Hover Texture from asset
		IconTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Script/Engine.Texture2D'/Game/01_Dev/Widgets/WidgetMaterials/Textures/T_QuestionIcon.T_QuestionIcon'"));
	}
}

int32 SAgentFollowIndicator::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                                     const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
                                     const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!ParentWidget)
	{
		// Shouldn't happen (child widget of ParentWidget), but improper removal could leave this dangling.
		return SMeshWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	const int32 MeshId = ParentWidget->HoverWidgetMeshViewerID;

	// Get all the pedestrian agent data from the parent widget that we need for rendering
	FAgentMeshViewer PedestrianAgentData = ParentWidget->SelectedAgentData;//This may not be thread safe, and may need to be protected with a mutex or similar if accessed from multiple threads
	const FAgentMeshViewer HoveredAgentData = ParentWidget->HoveredAgentData;

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
	if (!this->FollowIndicator)
	{
		if (HoveredAgentData.AgentID != PedestrianAgentData.AgentID)
		{
			// Different data
			PedestrianAgentData = HoveredAgentData;
		}
	}

	CreateRenderMeshData(PedestrianAgentData, AllottedGeometry, PerInstanceUpdate);

	//TODO: need to do this once, likely store a variable and check for nullptr
	auto MatInst = const_cast<SAgentFollowIndicator*>(this)->ConvertToMID(MeshId);

	if (MatInst)
	{
		// If hover widget, then we need to override the texture for the hover texture
		if (!FollowIndicator)
		{
			if (PedestrianAgentData.AgentID != HoveredAgentData.AgentID)
			{
				
				// If they are different we need to update the texture for the hovered agent
				MatInst->SetTextureParameterValue(FName(TEXT("AgentTexture")), IconTexture);
			}
		}
		
		// Six colour bands over SpeedFractionOfMax = CurrentSpeed / EntityMaxSpeed, where max_speed is a
		// PER-AGENT field read from the dataset. So this is v/v_free for that individual: an agent with a
		// 0.7 m/s free speed walking unimpeded shows the same blue as one doing 1.8 m/s. That is the right
		// quantity for "is this person being held up", and it is NOT comparable between agents.
		//
		// THE SIX BANDS AND THE BLUE->RED RAMP ARE FRUIN'S PRESENTATION. THE EDGES ARE NOT FRUIN'S, AND
		// DELIBERATELY SO. Fruin Level of Service is defined on DENSITY (m^2/person), not on speed, and its
		// boundaries are 3.24/2.32/1.39/0.93/0.46 m^2/p -- the ones the density heatmap really does use
		// (UDynamicPixelRenderingTexture::CalculateLevelOfService). The edges below are equal sixths of free
		// speed instead. Do not "correct" them to Fruin without reading the next paragraph, because that
		// conversion has been done and rejected once already.
		//
		// Pushing Fruin's density boundaries through a speed-density relation is the documented route, and
		// it gives useless colours here. Weidmann (1993) v = v_free*[1-exp(-1.913*(1/rho - 1/5.4))] puts the
		// five Fruin edges at v/v_free = 0.997 / 0.983 / 0.900 / 0.760 / 0.409; the SFPE Handbook's linear
		// Nelson-MacLennan form, S = 1.40*(1-0.266*D), gives 0.918 / 0.885 / 0.809 / 0.714 / 0.422. Either
		// way FOUR of the six bands live above v/v_free = 0.76 -- LOS A spans 0.3% of the scale under
		// Weidmann and LOS B spans 1.4%. On a single-agent indicator, whose whole job is showing one person
		// change over time, that would sit blue-cyan almost permanently and then lurch. Even spacing shows
		// the change; LOS fidelity does not.
		//
		// Consequence to be aware of when reading the colour: this scale UNDER-REPORTS congestion against
		// LOS. Under Weidmann an agent at half their free speed (green here) is in LOS E, and at a third
		// (yellow here) is already past LOS F. Green means "half speed", not "comfortable".
		//
		// Known gap: STOPPED and SLOW are both red. For accessibility work "stationary at a bottleneck for
		// 8 s" is a stronger signal than "15% of free speed" and wants its own state, not a sixth band.
		if (PedestrianAgentData.SpeedFractionOfMax >= 0.8335f) // fastest band
		{
			MatInst->SetVectorParameterValue(FName(TEXT("SpeedChangeIndicator")), FLinearColor(0.0f, 0.0f, 1.0f, 1.0f)); // Blue
		}
		else if (PedestrianAgentData.SpeedFractionOfMax >= 0.6668f) // second fastest
		{
			MatInst->SetVectorParameterValue(FName(TEXT("SpeedChangeIndicator")), FLinearColor(0.0f, 1.0f, 1.0f, 1.0f)); // Cyan
		}
		else if (PedestrianAgentData.SpeedFractionOfMax >= 0.5001f) // third fastest
		{
			MatInst->SetVectorParameterValue(FName(TEXT("SpeedChangeIndicator")), FLinearColor(0.0f, 1.0f, 0.0f, 1.0f)); // Green
		}
		else if (PedestrianAgentData.SpeedFractionOfMax >= 0.3334f) // fourth fastest
		{
			MatInst->SetVectorParameterValue(FName(TEXT("SpeedChangeIndicator")), FLinearColor(1.0f, 1.0f, 0.0f, 1.0f)); // Yellow
		}
		else if (PedestrianAgentData.SpeedFractionOfMax >= 0.1667f) // fifth fastest
		{
			MatInst->SetVectorParameterValue(FName(TEXT("SpeedChangeIndicator")), FLinearColor(1.0f, 0.25f, 0.0f, 1.0f)); // Orange
		}
		else // slowest
		{
			MatInst->SetVectorParameterValue(FName(TEXT("SpeedChangeIndicator")), FLinearColor(1.0f, 0.0f, 0.0f, 1.0f)); // Red
		}

		

		// TODO: we can improve this to be the only SMeshWidget, we just need to figure out how to handle the different textures for each instance
		
		// if (HoveredAgentData.AgentID != PedestrianAgentData.AgentID)
		// {
		// 	// if they are different we need to update the texture for the hovered agent
		// }
		// else if (HoveredAgentData.AgentID == PedestrianAgentData.AgentID)
		// {
		// 	// hover same as selected so no change needed to texture
		// }
	}

	const_cast<SAgentFollowIndicator*>(this)->UpdatePerInstanceBuffer(MeshId, PerInstanceUpdate);
	SMeshWidget::OnPaint(
				Args, AllottedGeometry, MyCullingRect,
				OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	return CurrentLayer;
}

void SAgentFollowIndicator::CreateRenderMeshData(const FAgentMeshViewer& PedestrianAgentData, const FGeometry& AllottedGeometry, FSlateInstanceBufferData& PerInstanceUpdate) const
{
	// If the agent ID is valid, we will render the mesh -> -1 is a special case for no agent selected, -2 is a special case for completed agents
	if (PedestrianAgentData.AgentID != -1 && PedestrianAgentData.AgentID != -2)
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
}
