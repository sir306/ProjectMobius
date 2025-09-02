// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/FlowSectionCounter.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/CustomSlateWidgets/FieldAndTextWidget.h"
#include "Util/WidgetUtilHelpers.h"

void UFlowSectionCounter::NativePreConstruct()
{
	Super::NativePreConstruct();
	
	// Push initial text values to children if they exist
	if (SectionHeaderFieldAndTextWidget)
	{
		SectionHeaderFieldAndTextWidget->SetTitleText(SectionHeaderText);
		SectionHeaderFieldAndTextWidget->SetFieldText(SectionHeaderAgentCountText);
		SectionHeaderFieldAndTextWidget->bAutoCenter = true;
	}
	if (FlowTypeAndValueFieldAndTextWidget)
	{
		FlowTypeAndValueFieldAndTextWidget->SetTitleText(FlowTypeTitleText);
		FlowTypeAndValueFieldAndTextWidget->SetFieldText(FlowValueText);
		FlowTypeAndValueFieldAndTextWidget->bAutoCenter = true;
	}
	
}

void UFlowSectionCounter::NativeConstruct()
{
	Super::NativeConstruct();
	
}

void UFlowSectionCounter::NativeDestruct()
{
	Super::NativeDestruct();
}

TSharedRef<SWidget> UFlowSectionCounter::RebuildWidget()
{
	return Super::RebuildWidget();
}

void UFlowSectionCounter::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}

void UFlowSectionCounter::InitializeFromParent(const FFlowSectionCounterInitParams& Params)
{
	// Defensive: ensure expected children exist
	if (!SectionHeaderFieldAndTextWidget || !FlowTypeAndValueFieldAndTextWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowSectionCounter missing FieldAndText children (Header or Value)."));
		return;
	}

	// TODO: backgrounds adjust padding/colors here
	// if (BackgroundBorderInner) { /* BackgroundBorderInner->SetPadding(...); */ }
	// if (BackgroundBorderEdge)  { /* Edge thickness / brush etc. */ }

	// Fill our immediate slot if requested (Canvas/Grid etc.)
	if (Params.bFillParentSlot && Slot)
	{
		WidgetUtilHelpers::FillParentSlot(this);
	}

	// Ensure the text the parent expects is applied (parent may have passed live values)
	SectionHeaderFieldAndTextWidget->SetTitleText(SectionHeaderText);
	SectionHeaderFieldAndTextWidget->SetFieldText(SectionHeaderAgentCountText);

	FlowTypeAndValueFieldAndTextWidget->SetTitleText(FlowTypeTitleText);
	FlowTypeAndValueFieldAndTextWidget->SetFieldText(FlowValueText);

	// Compute per-row boxes inside this section’s allocated cell
	const FMargin Pad = Params.InnerPadding;
	const FVector2D Inner(
		FMath::Max(0.f, Params.AllocatedSize.X - (Pad.Left + Pad.Right)),
		FMath::Max(0.f, Params.AllocatedSize.Y - (Pad.Top  + Pad.Bottom))
	);

	// Split vertically between header (row 0) and flow value (row 1)
	const float HeaderFrac = FMath::Clamp(Params.TitleFraction, 0.05f, 0.95f);
	const FVector2D HeaderBox(Inner.X, Inner.Y * HeaderFrac);
	const FVector2D ValueBox (Inner.X, Inner.Y * (1.f - HeaderFrac));

	// Pick the largest font sizes that fit each box (binary search + font measure)
	const int32 Min = Params.MinFontSize;
	const int32 Max = Params.MaxFontSize;
	const float Safety = Params.FitPaddingScale;

	const int32 HeaderFontPx = WidgetUtilHelpers::FindFittingFontSizeForFieldAndText(
		SectionHeaderFieldAndTextWidget, HeaderBox, Min, Max, Safety);

	const int32 ValueFontPx  = WidgetUtilHelpers::FindFittingFontSizeForFieldAndText(
		FlowTypeAndValueFieldAndTextWidget, ValueBox, Min, Max, Safety);

	// Apply
	SectionHeaderFieldAndTextWidget->SetFontSize((float)HeaderFontPx);
	FlowTypeAndValueFieldAndTextWidget->SetFontSize((float)ValueFontPx);

	// TODO: determine if center/align inside our root grid cell here if needed
	// WidgetUtilHelpers::SetGridSlotAlignment(SectionHeaderFieldAndTextWidget, HAlign_Fill, VAlign_Fill);
	// WidgetUtilHelpers::SetGridSlotAlignment(FlowTypeAndValueFieldAndTextWidget, HAlign_Fill, VAlign_Fill);
}
