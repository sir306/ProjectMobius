// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Components/PedestrianDataDisplay.h"

#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/TextBlock.h"
#include "UI/Components/FieldAndTextWidget.h"
#include "Util/WidgetUtilHelpers.h"
#include "UI/InWorld/AgentInfoDisplay.h"
#include "Subsystems/StatisticSubsystem.h"

void UPedestrianDataDisplay::SynchronizeProperties()
{

	
	Super::SynchronizeProperties();
	TitleFieldWidgets = {TitleFieldWidget1, TitleFieldWidget2, TitleFieldWidget3,
		TitleFieldWidget4, TitleFieldWidget5, TitleFieldWidget6,
		TitleFieldWidget7, TitleFieldWidget8};
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
	TitleFieldWidgets = {TitleFieldWidget1, TitleFieldWidget2, TitleFieldWidget3,
		TitleFieldWidget4, TitleFieldWidget5, TitleFieldWidget6,
		TitleFieldWidget7, TitleFieldWidget8};
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
	TitleFieldWidgets = {TitleFieldWidget1, TitleFieldWidget2, TitleFieldWidget3,
		TitleFieldWidget4, TitleFieldWidget5, TitleFieldWidget6,
		TitleFieldWidget7, TitleFieldWidget8};

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
	

	for (UFieldAndTextWidget* Widget : TitleFieldWidgets)
	{
		UWidgetUtilHelpers::SetGridSlotAlignment(Widget, HAlign_Fill, VAlign_Center);
	}
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
	const TArray<FString> FieldTitles = {
		TEXT("Agent ID"), TEXT("Name"), TEXT("Gender"), TEXT("Demographic"),
		TEXT("Speed"), TEXT("Gait Speed"), TEXT("Height"), TEXT("Position")
	};

	for (int32 Index = 0; Index < TitleFieldWidgets.Num(); ++Index)
	{
		if (TitleFieldWidgets[Index])
		{
			TitleFieldWidgets[Index]->SetUpdateTitleText(FText::FromString(FieldTitles[Index]));
		}
	}

	// TODO: for now widget field 6 will be collapsed and not used as the gait speed is not yet implemented
	// NOTE: the grid panel for this row has been set to 0 so will need updating to the correct value when we implement the gait speed
	if (TitleFieldWidget6)
	{
		TitleFieldWidget6->SetVisibility(ESlateVisibility::Collapsed);
	}
}
void UPedestrianDataDisplay::UpdateFieldTextBlocks() const
{

	auto LastUpdatedAgentMeshViewerData = InWorldSMeshDisplay->SelectedAgentData;

	if (InWorldSMeshDisplay->HoveredAgentData.AgentID != -1 && InWorldSMeshDisplay->HoveredAgentData.AgentID != -2)
	{
		LastUpdatedAgentMeshViewerData = InWorldSMeshDisplay->HoveredAgentData;
	}

	if (LastUpdatedAgentMeshViewerData.AgentID == -1 || LastUpdatedAgentMeshViewerData.AgentID == -2) // Check if no agent is selected or agent has completed sim
	{
		// We had an agent that was selected but now it is not selected
		if (TitleFieldWidgets.IsValidIndex(0) && LastUpdatedAgentMeshViewerData.AgentID == -1)
		{
			// collapse the header grid panel - only if it is visible
			if (WidgetHeadGridPanel && WidgetHeadGridPanel->GetVisibility() != ESlateVisibility::Collapsed)
			{
				// Hide the grid panel if no agent is selected and clear old fields
				UWidgetUtilHelpers::UpdateNumberIfChanged(TitleFieldWidgets[0], -1);
				for (int32 i = 1; i < TitleFieldWidgets.Num(); ++i)
				{
					UWidgetUtilHelpers::UpdateTextIfChanged(TitleFieldWidgets[i], FText::FromString(TEXT("N/A"))); 
				}
				WidgetHeadGridPanel->SetVisibility(ESlateVisibility::Collapsed);

				// Notify any listeners that the visibility has changed
				OnSelectedAgentComponentNowVisible.Broadcast(false);
			}
		}
		else // Agent has left sim
		{
			if (TitleFieldWidgets.Num() >= 8)
			{
				UWidgetUtilHelpers::UpdateTextIfChanged(TitleFieldWidgets[4], FText::FromString(TEXT("N/A")));
				UWidgetUtilHelpers::UpdateTextIfChanged(TitleFieldWidgets[5], FText::FromString(TEXT("N/A")));
				UWidgetUtilHelpers::UpdateTextIfChanged(TitleFieldWidgets[7], FText::FromString(TEXT("N/A")));
			}
		}
		
	}
	else
	{
		// if the widget is collapsed and we have selected an agent, with a new ID we should show the grid panel
		if (WidgetHeadGridPanel && WidgetHeadGridPanel->GetVisibility() == ESlateVisibility::Collapsed
			&& TitleFieldWidgets.IsValidIndex(0) && !TitleFieldWidgets[0]->FieldText.EqualTo(FText::AsNumber(LastUpdatedAgentMeshViewerData.AgentID)))
		{
			WidgetHeadGridPanel->SetVisibility(ESlateVisibility::Visible);// should change to visible but self not hit testable -> TODO: update BP logic to handle this

			// Notify any listeners that the visibility has changed
			OnSelectedAgentComponentNowVisible.Broadcast(true);
			
		}
		
		
		if (TitleFieldWidgets.Num() >= 8)
		{
			UWidgetUtilHelpers::UpdateNumberIfChanged(TitleFieldWidgets[0], LastUpdatedAgentMeshViewerData.AgentID);
			UWidgetUtilHelpers::UpdateTextIfChanged(TitleFieldWidgets[1], LastUpdatedAgentMeshViewerData.AgentName);
			UWidgetUtilHelpers::UpdateTextIfChanged(TitleFieldWidgets[2], LastUpdatedAgentMeshViewerData.Gender);
			UWidgetUtilHelpers::UpdateTextIfChanged(TitleFieldWidgets[3], LastUpdatedAgentMeshViewerData.Demographic);
			UWidgetUtilHelpers::UpdateFloatIfChanged(TitleFieldWidgets[4], LastUpdatedAgentMeshViewerData.AgentSpeed);
			UWidgetUtilHelpers::UpdateFloatIfChanged(TitleFieldWidgets[5], LastUpdatedAgentMeshViewerData.GaitDirectionalSpeed);
			UWidgetUtilHelpers::UpdateFloatIfChanged(TitleFieldWidgets[6], LastUpdatedAgentMeshViewerData.AgentHeight);
			UWidgetUtilHelpers::UpdateVectorIfChanged(TitleFieldWidgets[7], LastUpdatedAgentMeshViewerData.AgentWorldPosition);
		}
		
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

	// LOCAL size: the fill coefficients below are ratios of text extents (logical units) to panel
	// size, so the panel size must be logical too — GetAbsoluteSize() is physical px including DPI
	// scale and skewed the coefficients on scaled monitors. Zero until first prepass: bail, caller
	// re-enters on the next data update once geometry exists.
	const FVector2D BoxSize = ScreenGrid->GetPaintSpaceGeometry().GetLocalSize();
	if (BoxSize.IsNearlyZero())
	{
		return;
	}

	// calculate the required size for the TitleFieldWidgets to fit the text and what the coefficient will be for the row and column.
	// Clamped to a sane band around the design defaults (0.13 / 0.18): the coefficients feed back into
	// the fit's target cell indirectly, and unclamped values collapsed or ballooned the panel.
	float RequiredWidth = FMath::Clamp(InTextSize.X * 2 / BoxSize.X, 0.10f, 0.25f); // This will be the coefficient for the column
	float RequiredHeight = FMath::Clamp(InTextSize.Y * 1.5f * 8 / BoxSize.Y, 0.12f, 0.30f); // This will be the coefficient for the row

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

void UPedestrianDataDisplay::SetupTitleFieldWidgetFontSize() const
{
	// Insights scope for the whole font-fit (always pays the 8x GetTextSize measure). Compiled out of shipping.
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PedData::FontFit");

	if (!WidgetHeadGridPanel || !ScreenGrid)
	{
		return;
	}

	// Fit against the TARGET cell (screen grid size x the default fill coefficients), never against
	// WidgetHeadGridPanel's own current size: this method resizes that panel from the fitted text,
	// so measuring it back created a feedback loop that collapsed the panel to a micro box (fit to
	// tiny box -> tiny font -> resize slot smaller -> tinier box). The screen grid is the stable
	// fullscreen parent. LOCAL size = logical units, the space font sizes live in (GetAbsoluteSize
	// was physical px and over-scaled text on scaled monitors).
	const FVector2D ScreenBox = ScreenGrid->GetPaintSpaceGeometry().GetLocalSize();

	// Geometry is zero until the first prepass after construction. Bail — this runs on every data
	// update, so it re-enters with real geometry within a frame of first paint.
	if (ScreenBox.IsNearlyZero())
	{
		return;
	}

	// Default allocation of the header cell within the screen grid (see ResizeScreenGridToDefaultSize).
	const FVector2D PanelBox(ScreenBox.X * 0.13f, ScreenBox.Y * 0.18f);

	// Widest current text across the 8 field widgets — half of the D4 cache key.
	const TArray<TObjectPtr<UFieldAndTextWidget>>& LocalWidgets = TitleFieldWidgets;
	FVector2D TextSize(0.0f, 0.0f);
	for (const TObjectPtr<UFieldAndTextWidget>& Widget : LocalWidgets)
	{
		if (Widget)
		{
			const FVector2D CurrentTextSize = Widget->GetTextSize();
			TextSize.X = FMath::Max(TextSize.X, CurrentTextSize.X);
			TextSize.Y = FMath::Max(TextSize.Y, CurrentTextSize.Y);
		}
	}
	if (TextSize.IsNearlyZero())
	{
		// Underlying Slate text blocks not built yet — nothing measurable to fit.
		return;
	}

	// D4 geometry cache: the fitted font + grid-slot resize below are a pure function of the panel's
	// paint-space size and the widest field text. When both are unchanged since the last fit, skip —
	// it otherwise calls SetFont x8 (each invalidates a text block) and resizes the grid slot every
	// update, forcing a Slate prepass/paint. Text size is part of the key (not box alone) because
	// these fields change during playback/hover. Converges to a no-op once panel size and text settle.
	if (PanelBox.Equals(CachedFontFitBoxSize, 0.5f) && TextSize.Equals(CachedFontFitTextSize, 0.5f))
	{
		return;
	}
	CachedFontFitBoxSize = PanelBox;
	CachedFontFitTextSize = TextSize;

	// Insights scope for the part the D4 cache should make RARE: the binary-search fit + SetFont x8 +
	// grid-slot resize. If this scope's Count tracks FontFit's Count, the early-return is NOT engaging.
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PedData::FontFit_Recompute");

	// Each title+field pair measures combined (side by side), so it gets the full panel width and
	// one of the 8 rows of height. The previous half-box budget forced the fit to the floor and the
	// whole panel rendered undersized at fullscreen.
	const FVector2D SlotBox(PanelBox.X * 0.95f, PanelBox.Y / 8.0f);

	// Largest INTEGER size that fits every widget: integer because fractional sizes defeat the Slate
	// font cache and shimmer; min across widgets so all 8 rows share one size and all fit. Replaces
	// the old ratio-scale + hysteresis math (whose flicker the :339 TODO complained about — integer
	// quantization plus the D4 cache provides that stability structurally). Floor of 8: readability
	// beats strict fit — below that the panel is useless anyway, and the slot resize below grows the
	// cell to hold the text instead. Ceiling 14 = the shared Label size.
	int32 FittedSize = 14;
	for (const TObjectPtr<UFieldAndTextWidget>& Widget : LocalWidgets)
	{
		if (Widget)
		{
			FittedSize = FMath::Min(FittedSize,
				UWidgetUtilHelpers::FindFittingFontSizeForFieldAndText(Widget, SlotBox, 8, 14));
		}
	}

	FVector2D FittedTextSize(0.0f, 0.0f);
	for (const TObjectPtr<UFieldAndTextWidget>& Widget : LocalWidgets)
	{
		if (Widget)
		{
			// Only push the font when it actually differs — SetFontSize invalidates the text block.
			if (!FMath::IsNearlyEqual(Widget->GetFontSize(), static_cast<float>(FittedSize)))
			{
				Widget->SetFontSize(static_cast<float>(FittedSize));
			}
			const FVector2D CurrentTextSize = Widget->GetTextSize();
			FittedTextSize.X = FMath::Max(FittedTextSize.X, CurrentTextSize.X);
			FittedTextSize.Y = FMath::Max(FittedTextSize.Y, CurrentTextSize.Y);
		}
	}

	ResizeGridPanelParentSlotToFitLargeText(FittedTextSize);
}
