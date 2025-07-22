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
}

void UPedestrianDataDisplay::NativePreConstruct()
{
	Super::NativePreConstruct();
	ConfigureTextBlockStyles();

	// Set up the titles for the text blocks
	SetupTextBlockTitles();
	// Update the text blocks with the last updated agent data
	UpdateFieldTextBlocks();
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
}

