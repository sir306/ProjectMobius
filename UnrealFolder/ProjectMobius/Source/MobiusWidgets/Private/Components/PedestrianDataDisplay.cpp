// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/PedestrianDataDisplay.h"

#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/TextBlock.h"
#include "Components/CustomSlateWidgets/FieldAndTextWidget.h"
#include "InWorldUI/AgentInfoDisplay.h"
#include "Subsystems/StatisticSubsystem.h"

void UPedestrianDataDisplay::SynchronizeProperties()
{

	
	Super::SynchronizeProperties();
	// Set up the titles for the text blocks
	SetupTextBlockTitles();
	// Update the text blocks with the last updated agent data
	UpdateFieldTextBlocks();
	// Auto setup font sizes for all title fields -> this method will auto scale our text blocks based on the size of the parent widget
	SetupTitleFieldWidgetFontSize();
}

void UPedestrianDataDisplay::NativePreConstruct()
{
	Super::NativePreConstruct();
	ConfigureTextBlockStyles();

	// Set up the titles for the text blocks
	SetupTextBlockTitles();
	// Update the text blocks with the last updated agent data
	UpdateFieldTextBlocks();
	// Auto setup font sizes for all title fields 
	SetupTitleFieldWidgetFontSize();
}

void UPedestrianDataDisplay::NativeConstruct()
{
	Super::NativeConstruct();

	// bind to the stats subsystem to update the data display
	if (auto World = GetWorld())
	{
		if (auto StatSub = World->GetSubsystem<UStatisticSubsystem>())
		{
			StatSub->OnSelectedAgentInfoChanged.AddUObject(this, &UPedestrianDataDisplay::UpdateFieldTextBlocks);
			// todo: make sure to cleanup delegates
		}
	}
}



void UPedestrianDataDisplay::ConfigureTextBlockStyles() const
{
	// TODO: move this to a utility function or class for better reusability
	auto SetTextBlockAlignment = [](UFieldAndTextWidget* TitleFieldWidget, EHorizontalAlignment HAlign, EVerticalAlignment VAlign)
	{
		if (!TitleFieldWidget) return;

		// Slot cast: assumes these TextBlocks are in a UGridPanel or UHorizontalBox, etc.
		if (UPanelSlot* Slot = TitleFieldWidget->Slot)
		{
			if (UGridSlot* GridSlot = Cast<UGridSlot>(Slot))
			{
				GridSlot->SetHorizontalAlignment(HAlign);
				GridSlot->SetVerticalAlignment(VAlign);
			}
			// else if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Slot))
			// {
			// 	HSlot->SetHorizontalAlignment(HAlign);
			// 	HSlot->SetVerticalAlignment(VAlign);
			// }
			// else if (UOverlaySlot* OSlot = Cast<UOverlaySlot>(Slot))
			// {
			// 	OSlot->SetHorizontalAlignment(HAlign);
			// 	OSlot->SetVerticalAlignment(VAlign);
			// }
			else
			{
				// fallback — if your layout uses some other slot type
				UE_LOG(LogTemp, Warning, TEXT("Unsupported slot type for text block %s"), *TitleFieldWidget->GetName());
			}
		}
	};

	SetTextBlockAlignment(TitleFieldWidget1, HAlign_Fill, VAlign_Center);
	SetTextBlockAlignment(TitleFieldWidget2, HAlign_Fill, VAlign_Center);
	SetTextBlockAlignment(TitleFieldWidget3, HAlign_Fill, VAlign_Center);
	SetTextBlockAlignment(TitleFieldWidget4, HAlign_Fill, VAlign_Center);
	SetTextBlockAlignment(TitleFieldWidget5, HAlign_Fill, VAlign_Center);
	SetTextBlockAlignment(TitleFieldWidget6, HAlign_Fill, VAlign_Center);
	SetTextBlockAlignment(TitleFieldWidget7, HAlign_Fill, VAlign_Center);
	SetTextBlockAlignment(TitleFieldWidget8, HAlign_Fill, VAlign_Center);
	// SetTextBlockAlignment(TitleFieldWidget1, HAlign_Center, VAlign_Center);
	// SetTextBlockAlignment(TitleFieldWidget2, HAlign_Center, VAlign_Center);
	// SetTextBlockAlignment(TitleFieldWidget3, HAlign_Center, VAlign_Center);
	// SetTextBlockAlignment(TitleFieldWidget4, HAlign_Center, VAlign_Center);
	// SetTextBlockAlignment(TitleFieldWidget5, HAlign_Center, VAlign_Center);
	// SetTextBlockAlignment(TitleFieldWidget6, HAlign_Center, VAlign_Center);
	// SetTextBlockAlignment(TitleFieldWidget7, HAlign_Center, VAlign_Center);
	// SetTextBlockAlignment(TitleFieldWidget8, HAlign_Center, VAlign_Center);
}

void UPedestrianDataDisplay::SetupTextBlockTitles() const
{
	const TArray<TPair<UFieldAndTextWidget*, FString>> FieldTitles = {
		{ TitleFieldWidget1, TEXT("Agent ID") },
		{ TitleFieldWidget2, TEXT("Name") },
		{ TitleFieldWidget3, TEXT("Gender") },
		{ TitleFieldWidget4, TEXT("Demographic") },
		{ TitleFieldWidget5, TEXT("Speed") },
		{ TitleFieldWidget6, TEXT("Gait Speed") },
		{ TitleFieldWidget7, TEXT("Height") },
		{ TitleFieldWidget8, TEXT("Position") }
	};

	for (const TPair<UFieldAndTextWidget*, FString>& Pair : FieldTitles)
	{
		if (Pair.Key)
		{
			Pair.Key->SetTitleText(FText::FromString(Pair.Value));
		}
	}

	// TODO: for now widget field 6 will be collapsed and not used as the gait speed is not yet implemented
	// NOTE: the grid panel for this row has been set to 0 so will need updating to the correct value when we implement the gait speed
	if (TitleFieldWidget6)
	{
		TitleFieldWidget6->SetVisibility(ESlateVisibility::Collapsed);
	}
}
//TODO:Move these lambdas to the text utility helper interface/class
void UPedestrianDataDisplay::UpdateFieldTextBlocks() const
{
	auto UpdateIfChanged = [](UFieldAndTextWidget* Widget, const FText& NewText)
	{
		if (!Widget) return;

		if (!Widget->FieldText.EqualTo(NewText))
		{
			Widget->SetFieldText(NewText);
		}
	};

	auto UpdateIfChangedNumber = [](UFieldAndTextWidget* Widget, int32 NewNumber)
	{
		if (!Widget) return;

		FText NewText = FText::AsNumber(NewNumber);
		if (!Widget->FieldText.EqualTo(NewText))
		{
			Widget->SetFieldText(NewText);
		}
	};

	auto UpdateIfChangedFloat = [](UFieldAndTextWidget* Widget, float NewFloat)
	{
		if (!Widget) return;

		FText NewText = FText::FromString(FString::Printf(TEXT("%.2f"), NewFloat));
		if (!Widget->FieldText.EqualTo(NewText))
		{
			Widget->SetFieldText(NewText);
		}
	};

	auto UpdateIfChangedVector = [](UFieldAndTextWidget* Widget, const FVector& Vec)
	{
		if (!Widget) return;

		FText NewText = FText::FromString(
			FString::Printf(TEXT("%.2f, %.2f, %.2f"), Vec.X, Vec.Y, Vec.Z));

		if (!Widget->FieldText.EqualTo(NewText))
		{
			Widget->SetFieldText(NewText);
		}
	};

	auto LastUpdatedAgentMeshViewerData = InWorldSMeshDisplay->SelectedAgentData;

	if (InWorldSMeshDisplay->HoveredAgentData.AgentID != -1 && InWorldSMeshDisplay->HoveredAgentData.AgentID != -2)
	{
		LastUpdatedAgentMeshViewerData = InWorldSMeshDisplay->HoveredAgentData;
	}

	if (LastUpdatedAgentMeshViewerData.AgentID == -1 || LastUpdatedAgentMeshViewerData.AgentID == -2) // Check if no agent is selected or agent has completed sim
	{
		// We had an agent that was selected but now it is not selected
		if (TitleFieldWidget1 && LastUpdatedAgentMeshViewerData.AgentID == -1)
		{
			// collapse the header grid panel - only if it is visible
			if (WidgetHeadGridPanel && WidgetHeadGridPanel->GetVisibility() != ESlateVisibility::Collapsed)
			{
				// Hide the grid panel if no agent is selected and clear old fields
				UpdateIfChangedNumber(TitleFieldWidget1, -1);
				UpdateIfChanged(TitleFieldWidget2, FText::FromString("N/A"));
				UpdateIfChanged(TitleFieldWidget3, FText::FromString("N/A"));
				UpdateIfChanged(TitleFieldWidget4, FText::FromString("N/A"));
				UpdateIfChanged(TitleFieldWidget5, FText::FromString("N/A"));
				UpdateIfChanged(TitleFieldWidget6, FText::FromString("N/A"));
				UpdateIfChanged(TitleFieldWidget7, FText::FromString("N/A"));
				UpdateIfChanged(TitleFieldWidget8, FText::FromString("N/A"));
				WidgetHeadGridPanel->SetVisibility(ESlateVisibility::Collapsed);

				// Notify any listeners that the visibility has changed
				OnSelectedAgentComponentNowVisible.Broadcast(false);
			}
		}
		else // Agent has left sim
		{
			UpdateIfChanged(TitleFieldWidget5, FText::FromString("N/A"));
			UpdateIfChanged(TitleFieldWidget6, FText::FromString("N/A"));
			UpdateIfChanged(TitleFieldWidget8, FText::FromString("N/A"));
		}
		
	}
	else
	{
		// if the widget is collapsed and we have selected an agent, with a new ID we should show the grid panel
		if (WidgetHeadGridPanel && WidgetHeadGridPanel->GetVisibility() == ESlateVisibility::Collapsed
			&& !TitleFieldWidget1->FieldText.EqualTo(FText::AsNumber(LastUpdatedAgentMeshViewerData.AgentID)))
		{
			WidgetHeadGridPanel->SetVisibility(ESlateVisibility::Visible);// should change to visible but self not hit testable -> TODO: update BP logic to handle this

			// Notify any listeners that the visibility has changed
			OnSelectedAgentComponentNowVisible.Broadcast(true);
			
		}
		
		
		UpdateIfChangedNumber(TitleFieldWidget1, LastUpdatedAgentMeshViewerData.AgentID);
		UpdateIfChanged(TitleFieldWidget2, LastUpdatedAgentMeshViewerData.AgentName);
		UpdateIfChanged(TitleFieldWidget3, LastUpdatedAgentMeshViewerData.Gender);
		UpdateIfChanged(TitleFieldWidget4, LastUpdatedAgentMeshViewerData.Demographic);
		UpdateIfChangedFloat(TitleFieldWidget5, LastUpdatedAgentMeshViewerData.AgentSpeed);
		UpdateIfChangedFloat(TitleFieldWidget6, LastUpdatedAgentMeshViewerData.GaitDirectionalSpeed);
		UpdateIfChangedFloat(TitleFieldWidget7, LastUpdatedAgentMeshViewerData.AgentHeight);
		UpdateIfChangedVector(TitleFieldWidget8, LastUpdatedAgentMeshViewerData.AgentWorldPosition);
		
	}
	SetupTitleFieldWidgetFontSize();
}

void UPedestrianDataDisplay::ResizeGridPanelParentSlotToFitLargeText(FVector2D& InTextSize) const
{
	// The Header Grid Panel is allocated to ScreenGrid panel slot column 1 row 2 -> need to resize the parent slot to fit the text
	// row is allocated 0.18 fill and column is allocated 0.13 fill

	if (ScreenGrid == nullptr)
	{
		// Do not proceed if ScreenGrid is not set -> this will cause nullptr access crashes
		return;
	}

	// Calculate the grid size
	FVector2D BoxSize = ScreenGrid->GetDesiredSize();// Desired size may not be the value we want

	// 0.13 is slot width percent for screen 0.18 is the slot height percent

	BoxSize = ScreenGrid->GetPaintSpaceGeometry().GetAbsoluteSize();

	if (BoxSize == FVector2D::ZeroVector)
	{
		BoxSize = ScreenGrid->GetPaintSpaceGeometry().GetRenderBoundingRect().GetSize();
	}

	// calculate the required size for the TitleFieldWidgets to fit the text and what the coefficient will be for the row and column
	float RequiredWidth = InTextSize.X *2 / BoxSize.X; // This will be the coefficient for the column
	float RequiredHeight = InTextSize.Y *1.5f * 8 / BoxSize.Y; // This will be the coefficient for the row

	// Check if the the required width and height are not the same as the current coefficients -> TODO: add a tolerance check here so we don't resize if the values are very close
	if (ScreenGrid->ColumnFill.Num() > 1 && ScreenGrid->RowFill.Num() > 2)
	{
		float CurrentWidthCoefficient = ScreenGrid->ColumnFill[1];
		float CurrentHeightCoefficient = ScreenGrid->RowFill[2];

		if (FMath::IsNearlyEqual(CurrentWidthCoefficient, RequiredWidth, 0.01f) &&
			FMath::IsNearlyEqual(CurrentHeightCoefficient, RequiredHeight, 0.01f))
		{
			return; // No need to resize if the coefficients are already set
		}
	}
	
	// Apply the new coefficient to the grid panel slot
	ScreenGrid->SetColumnFill(1, RequiredWidth);
	ScreenGrid->SetRowFill(2, RequiredHeight);

	// Our spacer panel coefficients are set to 0.86 and 0.715 respectively and need to be set to fit the new size
	float SpacerColumnCoefficient = 0.99f - RequiredWidth;// 0.99f is the default spacer and header grid fill so minus the required width
	ScreenGrid->SetColumnFill(0, SpacerColumnCoefficient); // Set the spacer column fill to the new coefficient

	float SpacerRowCoefficient = 0.895f - RequiredHeight; // 0.895f is the default spacer and header grid fill so minus the required height
	ScreenGrid->SetRowFill(1, SpacerRowCoefficient);
}

void UPedestrianDataDisplay::ResizeScreenGridToDefaultSize() const
{
	
	if (ScreenGrid)
	{
		// Our default panel size for the header grid
		ScreenGrid->SetColumnFill(1, 0.13f); // Column 1 is the header column
		ScreenGrid->SetRowFill(2, 0.18f); // Row 2 is the header row

		// reset the spacer panel coefficients
		ScreenGrid->SetColumnFill(0, 0.86f); 
		ScreenGrid->SetRowFill(1, 0.715f);
	}
}

void UPedestrianDataDisplay::GetScreenGridCoefficients(int32 Col, int32 Row, float& OutWidthCoefficient,
                                                       float& OutHeightCoefficient) const
{
	/* The Assumption is if a fill is not set then the coefficient is 0.0f @note: this isn't technically true though
	 * as the default fill is 0.0f, but we know that this widget never has a fill of 0.0f */
	
	// Set the out coefficients to 0.0f by default -> this will be used if the row or column is invalid
	OutWidthCoefficient = 0.0f;
	OutHeightCoefficient = 0.0f;
	
	// valid row
	if (ScreenGrid && ScreenGrid->RowFill.Num() > Row)
	{
		OutWidthCoefficient = ScreenGrid->RowFill[Row];
	}
	// valid column
	if (ScreenGrid && ScreenGrid->ColumnFill.Num() > Col)
	{
		OutHeightCoefficient = ScreenGrid->ColumnFill[Col];	
	}

}

void UPedestrianDataDisplay::SetupTitleFieldWidgetFontSize() const
{
	// // Reset Grid Panel to default size
	// ResizeScreenGridToDefaultSize();
	
	// Array of all title field widgets
	TArray<UFieldAndTextWidget*> TitleFieldWidgets = {
		TitleFieldWidget1, TitleFieldWidget2, TitleFieldWidget3,
		TitleFieldWidget4, TitleFieldWidget5, TitleFieldWidget6,
		TitleFieldWidget7, TitleFieldWidget8
	};
	FVector2D TextSize(0.0f, 0.0f);
	
	// loop through all the title field widgets and get the largest text measurement size
	for (UFieldAndTextWidget* Widget : TitleFieldWidgets)
	{
		if (Widget)
		{
			FVector2D CurrentTextSize = Widget->GetTextSize();
			TextSize.X = FMath::Max(TextSize.X, CurrentTextSize.X);
			TextSize.Y = FMath::Max(TextSize.Y, CurrentTextSize.Y);
		}
	}

	float DefaultFontSize = TitleFieldWidgets[0]->GetFontSize(); // Get the default font size from the first widget
	//TODO: this is not right?? maybe being called before the widget is fully constructed? or a prepass has been completed
	// Calculate the box width from the grid panel
	FVector2D BoxSize = WidgetHeadGridPanel->GetDesiredSize();// Desired size may not be the value we want

	// 0.13 is slot width percent for screen 0.18 is the slot height percent

	BoxSize = WidgetHeadGridPanel->GetPaintSpaceGeometry().GetAbsoluteSize();

	if (BoxSize == FVector2D::ZeroVector)
	{
		BoxSize = WidgetHeadGridPanel->GetPaintSpaceGeometry().GetRenderBoundingRect().GetSize();
		// log fallback size
		UE_LOG(LogTemp, Warning, TEXT("BoxSize is zero, using bounding rect size: %s"), *BoxSize.ToString());
		return;
	}

	BoxSize *= 0.5f; // each text slot takes up 50% of the box size, so we scale down to fit
	
	// Compute scale factor to fit in box (maintain aspect ratio)
	float ScaleX = (BoxSize.X / TextSize.X) ? BoxSize.X / TextSize.X : 0.0f; // Avoid division by zero
	float ScaleY = (BoxSize.Y / TextSize.Y) ? BoxSize.Y / TextSize.Y : 0.0f; // Avoid division by zero
	float UniformScale = FMath::Min( FMath::Clamp(ScaleX, 0, ScaleX),  FMath::Clamp(ScaleY, 0, ScaleY)); // Ensure scale is non-negative and min val of 0
	
	// Adjust font size
	float FinalFontSize = FMath::Clamp((DefaultFontSize * UniformScale), 1, 14); // Text should never be allowed to be bigger than 14
	
	if (FinalFontSize < 10)
	{
		// Recalculate Text size for a font size of 10
		// loop through all the title field widgets and get the largest text measurement size
		for (UFieldAndTextWidget* Widget : TitleFieldWidgets)
		{
			if (Widget)
			{
				Widget->SetFontSize(10);
				FVector2D CurrentTextSize = Widget->GetTextSize();
				TextSize.X = FMath::Max(TextSize.X, CurrentTextSize.X);
				TextSize.Y = FMath::Max(TextSize.Y, CurrentTextSize.Y);
			}
		}
	}
	else
	{
		// Set the font size for each title field widget
		for (UFieldAndTextWidget* Widget : TitleFieldWidgets)
		{
			if (Widget)
			{
				Widget->SetFontSize(FinalFontSize);
			}
		}
	}
	
	ResizeGridPanelParentSlotToFitLargeText(TextSize);
	
}
