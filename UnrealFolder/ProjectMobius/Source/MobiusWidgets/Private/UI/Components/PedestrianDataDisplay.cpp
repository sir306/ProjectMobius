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

	// §3.4/D69 B-RISK tenability section titles + units (exact per CLAUDE.md B-RISK rules). Collapsed
	// until an agent with tenability data is displayed (UpdateBRiskTenabilitySection). Caption is
	// title-only; "B-RISK" keeps its caps (C2 exception), the rest sentence case.
	if (BRiskSectionCaption)
	{
		BRiskSectionCaption->SetUpdateTitleText(FText::FromString(TEXT("B-RISK tenability")));
		BRiskSectionCaption->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (BRiskVisibilityField)
	{
		BRiskVisibilityField->SetUpdateTitleText(FText::FromString(TEXT("Visibility (m)")));
		BRiskVisibilityField->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (BRiskToxicFEDField)
	{
		BRiskToxicFEDField->SetUpdateTitleText(FText::FromString(TEXT("Toxic FED")));
		BRiskToxicFEDField->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (BRiskThermalFEDField)
	{
		BRiskThermalFEDField->SetUpdateTitleText(FText::FromString(TEXT("Thermal FED")));
		BRiskThermalFEDField->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (BRiskTemperatureField)
	{
		// Degree sign built at runtime to keep the source file plain ASCII.
		BRiskTemperatureField->SetUpdateTitleText(
			FText::FromString(FString::Printf(TEXT("Temperature (%cC)"), static_cast<TCHAR>(0x00B0))));
		BRiskTemperatureField->SetVisibility(ESlateVisibility::Collapsed);
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

	// §3.4/D69: B-RISK tenability rows for the agent currently on screen (selected or hovered).
	UpdateBRiskTenabilitySection(LastUpdatedAgentMeshViewerData.AgentID);

	SetupTitleFieldWidgetFontSize();
}

void UPedestrianDataDisplay::UpdateBRiskTenabilitySection(int32 SelectedAgentID) const
{
	// Nothing to do until the asset gains the B-RISK rows (BindWidgetOptional).
	if (!BRiskSectionCaption && !BRiskVisibilityField && !BRiskToxicFEDField
		&& !BRiskThermalFEDField && !BRiskTemperatureField)
	{
		return;
	}

	// Locate the displayed agent's tenability snapshot. Q48/R3: the egress-health array is NON-empty even
	// with no B-RISK loaded — every pedestrian carries FAgentEgressTenabilityFragment by default, so the
	// processor publishes an all-zero entry per agent regardless. "Has an entry" is therefore NOT a valid
	// "B-RISK loaded" signal (the old gate showed all-zero rows as data). Gate instead on
	// UStatisticSubsystem::IsBRiskTenabilityActive(), which the AgentEgressHealthProcessor drives from
	// UBRiskEgressSubsystem::AreAgentTimelinesCurrent() (true only when the scenario's per-agent timelines
	// are loaded AND current). When inactive, Found stays nullptr -> all B-RISK rows collapse, normal rows
	// keep full height. Module-safe: UStatisticSubsystem lives in MobiusCore (already a dependency).
	const FAgentEgressTenabilityViewer* Found = nullptr;
	if (SelectedAgentID >= 0)
	{
		if (const UWorld* World = GetWorld())
		{
			if (const UStatisticSubsystem* StatSub = World->GetSubsystem<UStatisticSubsystem>())
			{
				if (StatSub->IsBRiskTenabilityActive())
				{
					for (const FAgentEgressTenabilityViewer& Entry : StatSub->GetAgentEgressHealthData())
					{
						if (Entry.AgentID == SelectedAgentID)
						{
							Found = &Entry;
							break;
						}
					}
				}
			}
		}
	}

	const ESlateVisibility RowVisibility = Found
		? ESlateVisibility::SelfHitTestInvisible
		: ESlateVisibility::Collapsed;

	if (BRiskSectionCaption)
	{
		BRiskSectionCaption->SetVisibility(RowVisibility);
	}

	// Helper to show a value field (Mono) or collapse it. ValueText is prebuilt at each call site with a
	// literal format (FString::Printf requires a literal format string). Values are LIVE sampled
	// tenability, not endpoints, so formatting is display-only; FED dimensionless (3 dp), vis/temp 1 dp.
	auto ApplyValueField = [RowVisibility, Found](UFieldAndTextWidget* Field, const FString& ValueText)
	{
		if (!Field)
		{
			return;
		}
		Field->SetVisibility(RowVisibility);
		if (Found)
		{
			Field->SetUpdateFieldText(FText::FromString(ValueText));
			Field->SetFieldFontFace(FName(TEXT("Mono")));
		}
	};

	ApplyValueField(BRiskVisibilityField,  Found ? FString::Printf(TEXT("%.1f"), Found->CurrentVisibilityM)   : FString());
	ApplyValueField(BRiskToxicFEDField,    Found ? FString::Printf(TEXT("%.3f"), Found->AccumulatedToxicFED)   : FString());
	ApplyValueField(BRiskThermalFEDField,  Found ? FString::Printf(TEXT("%.3f"), Found->AccumulatedThermalFED) : FString());
	ApplyValueField(BRiskTemperatureField, Found ? FString::Printf(TEXT("%.1f"), Found->CurrentTemperatureC)   : FString());
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
	// D143: the row multiplier must track the number of VISIBLE rows (set by SetupTitleFieldWidgetFontSize),
	// not a hard-coded 8 — when the 5 B-RISK rows are on screen the cell must grow ~50% taller to hold all
	// ~12 rows, else the extra rows overflow and collapse the agent rows. Floored at 8 so the agent-only
	// case keeps its original grow factor.
	const int32 GrowRowCount = FMath::Max(8, CachedVisibleRowCount);
	float RequiredWidth = FMath::Clamp(InTextSize.X * 2 / BoxSize.X, 0.10f, 0.25f); // This will be the coefficient for the column
	float RequiredHeight = FMath::Clamp(InTextSize.Y * 1.5f * GrowRowCount / BoxSize.Y, 0.12f, 0.42f); // This will be the coefficient for the row

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

	// D143: the header grid holds the 8 agent rows PLUS 5 optional B-RISK rows (BindWidgetOptional) that
	// toggle visible only when tenability data is loaded (UpdateBRiskTenabilitySection). All 13 rows must
	// be fitted+budgeted together, in grid-row order, or the extra B-RISK rows steal the fixed panel height
	// (they were content-sized: no RowFill entry) and collapse the agent rows to zero. Ordered: agent rows
	// 0..7, then caption(8)/visibility(9)/toxic(10)/thermal(11)/temp(12).
	TArray<UFieldAndTextWidget*> AllRowWidgets;
	AllRowWidgets.Reserve(TitleFieldWidgets.Num() + 5);
	for (const TObjectPtr<UFieldAndTextWidget>& Widget : TitleFieldWidgets)
	{
		AllRowWidgets.Add(Widget);
	}
	AllRowWidgets.Add(BRiskSectionCaption);
	AllRowWidgets.Add(BRiskVisibilityField);
	AllRowWidgets.Add(BRiskToxicFEDField);
	AllRowWidgets.Add(BRiskThermalFEDField);
	AllRowWidgets.Add(BRiskTemperatureField);

	// Count visible rows and drive WidgetHeadGridPanel's RowFill deterministically: every VISIBLE row gets
	// an equal fill weight so the panel height divides evenly across exactly the rows on screen; every
	// collapsed row (the gait row6 always, all B-RISK rows when no tenability data) gets fill 0 so it takes
	// zero height. This replaces the static asset RowFill ([0.125 x8] with the B-RISK rows content-sized) —
	// content-sized rows claimed their desired height first and starved the fill agent rows. Only re-apply
	// when the pattern actually changes (SetRowFill invalidates layout).
	int32 VisibleRowCount = 0;
	bool bRowFillChanged = false;
	for (int32 i = 0; i < AllRowWidgets.Num(); ++i)
	{
		UFieldAndTextWidget* Widget = AllRowWidgets[i];
		const bool bVisible = Widget && Widget->GetVisibility() != ESlateVisibility::Collapsed;
		if (bVisible)
		{
			++VisibleRowCount;
		}
		const float DesiredFill = bVisible ? 0.125f : 0.0f;
		const bool bHaveEntry = WidgetHeadGridPanel->RowFill.IsValidIndex(i);
		if (!bHaveEntry || !FMath::IsNearlyEqual(WidgetHeadGridPanel->RowFill[i], DesiredFill))
		{
			WidgetHeadGridPanel->SetRowFill(i, DesiredFill);
			bRowFillChanged = true;
		}
	}

	// Floor of 8 keeps the agent-only case (7 visible rows: gait collapsed) pixel-identical to the pre-B-RISK
	// budget, which always divided by 8. With B-RISK loaded this rises (12) so fonts + panel-grow scale for
	// the taller stack instead of overflowing the 8-row cell.
	const int32 EffectiveRowCount = FMath::Max(8, VisibleRowCount);

	// Widest current text across every VISIBLE row — half of the D4 cache key.
	FVector2D TextSize(0.0f, 0.0f);
	for (UFieldAndTextWidget* Widget : AllRowWidgets)
	{
		if (Widget && Widget->GetVisibility() != ESlateVisibility::Collapsed)
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
	// paint-space size, the widest field text AND the visible row count (a B-RISK toggle changes the row
	// budget with box+text otherwise steady). When all three are unchanged since the last fit, skip — it
	// otherwise calls SetFont per row (each invalidates a text block) and resizes the grid slot every
	// update, forcing a Slate prepass/paint. Never skip on the frame the RowFill pattern just changed.
	if (!bRowFillChanged
		&& EffectiveRowCount == CachedVisibleRowCount
		&& PanelBox.Equals(CachedFontFitBoxSize, 0.5f)
		&& TextSize.Equals(CachedFontFitTextSize, 0.5f))
	{
		return;
	}
	CachedFontFitBoxSize = PanelBox;
	CachedFontFitTextSize = TextSize;
	CachedVisibleRowCount = EffectiveRowCount;

	// Insights scope for the part the D4 cache should make RARE: the binary-search fit + SetFont per row +
	// grid-slot resize. If this scope's Count tracks FontFit's Count, the early-return is NOT engaging.
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PedData::FontFit_Recompute");

	// Each title+field pair measures combined (side by side), so it gets the full panel width and one of the
	// EffectiveRowCount rows of height. Budgeting by the live visible count (not a hard-coded 8) is what
	// keeps 12 B-RISK+agent rows fitting inside the cell instead of overflowing it.
	const FVector2D SlotBox(PanelBox.X * 0.95f, PanelBox.Y / static_cast<float>(EffectiveRowCount));

	// Largest INTEGER size that fits every visible widget: integer because fractional sizes defeat the Slate
	// font cache and shimmer; min across widgets so all rows share one size and all fit (agent + B-RISK rows
	// then render at the SAME font, per owner). Floor of 8: readability beats strict fit — the slot resize
	// below grows the cell to hold the text instead. Ceiling 14 = the shared Label size.
	int32 FittedSize = 14;
	for (UFieldAndTextWidget* Widget : AllRowWidgets)
	{
		if (Widget && Widget->GetVisibility() != ESlateVisibility::Collapsed)
		{
			FittedSize = FMath::Min(FittedSize,
				UWidgetUtilHelpers::FindFittingFontSizeForFieldAndText(Widget, SlotBox, 8, 14));
		}
	}

	FVector2D FittedTextSize(0.0f, 0.0f);
	for (UFieldAndTextWidget* Widget : AllRowWidgets)
	{
		if (Widget && Widget->GetVisibility() != ESlateVisibility::Collapsed)
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
