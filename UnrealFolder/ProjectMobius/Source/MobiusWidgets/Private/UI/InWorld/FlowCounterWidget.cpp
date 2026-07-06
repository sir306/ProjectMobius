// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/InWorld/FlowCounterWidget.h"

#include "UI/Components/FlowSectionCounter.h"
#include "Components/UniformGridPanel.h"
#include "UI/Components/FieldAndTextWidget.h"
#include "Util/WidgetUtilHelpers.h"

void UFlowCounterWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	
	if (RootUniformGridPanel && FlowDataUniformGridPanel && LiveAgentCountFieldAndTextWidget)
	{
		// ensure the the live agent count field is configured correctly
		LiveAgentCountFieldAndTextWidget->bAutoCenter = true;
		LiveAgentCountFieldAndTextWidget->bIsTitleAboveField = false;

		// assign components to respective slots
		RootUniformGridPanel->AddChildToUniformGrid(LiveAgentCountFieldAndTextWidget, 0, 0);
		RootUniformGridPanel->AddChildToUniformGrid(FlowDataUniformGridPanel, 1, 0);

		// ensure the flow data uniform grid panel and Live Agent Count Widget fills its cell
		UWidgetUtilHelpers::UniformGridFillCell(LiveAgentCountFieldAndTextWidget);
		UWidgetUtilHelpers::UniformGridFillCell(FlowDataUniformGridPanel);

	}
	
}

void UFlowCounterWidget::NativeConstruct()
{
	Super::NativeConstruct();
	InitializeCounterWidget();
}

void UFlowCounterWidget::NativeDestruct()
{
	Super::NativeDestruct();
}

void UFlowCounterWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	InitializeCounterWidget();
}

void UFlowCounterWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bNeedsSectionStyleUpdate)
		return;

	// MyGeometry is the allotted geometry from Slate's layout pass — valid before first paint,
	// unlike GetCachedGeometry() which is zero until after the first paint completes.
	if (MyGeometry.GetLocalSize().IsNearlyZero()) { return; }

	if (UpdateFlowSectionCountersStyleInternal(MyGeometry))
		bNeedsSectionStyleUpdate = false;
}

int32 UFlowCounterWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 i = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle,
									  bParentEnabled);
	
	//UpdateFlowSectionCountersStyle();
	
	return i; 
}

void UFlowCounterWidget::InitializeCounterWidget()
{
	if (TitleText.IsEmpty() || !LiveAgentCountFieldAndTextWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowCounterWidget missing TitleText or LiveAgentCountFieldAndTextWidget child."));
		return;
	}

	LiveAgentCountFieldAndTextWidget->SetUpdateTitleText(TitleText);
	LiveAgentCountFieldAndTextWidget->SetUpdateFieldText(FText::AsNumber(0));

	// Initialize the current live agent count to 0
	CurrentLiveAgentCount = 0;

	// Initialize the flow data sections style
	//UpdateFlowSectionCountersStyle();
}

void UFlowCounterWidget::UpdateLiveAgentCount(const int32 NewValue)
{
	if (!LiveAgentCountFieldAndTextWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowCounterWidget missing LiveAgentCountFieldAndTextWidget child."));
		return;
	}

	// Early out if the value hasn't changed
	if (CurrentLiveAgentCount == NewValue) return;;
	
	// Ensure we clamp to a minimum of 0
	CurrentLiveAgentCount = FMath::Clamp(NewValue, 0, NewValue);
	
	// Update the text field if the value has changed
}

void UFlowCounterWidget::UpdateLiveAgentCountField()
{
}

void UFlowCounterWidget::AddFlowSectionCounter()
{
	CurrentFlowDataSections++;
	//UpdateFlowSectionCountersStyle();
	bNeedsSectionStyleUpdate = true;
}

void UFlowCounterWidget::RemoveFlowSectionCounter()
{
	// Decrement the section count - as we clamp to 1 we can safely decrement first here
	CurrentFlowDataSections--;
	
	// Clamp to a minimum of 1 section
	SetFlowDataSections(CurrentFlowDataSections);

	// Update the style after changing the section count
	//UpdateFlowSectionCountersStyle();
	bNeedsSectionStyleUpdate = true;
}

void UFlowCounterWidget::UpdateFlowSectionCountersStyle()
{
	// Blueprint-callable wrapper — uses CachedGeometry for manual/Blueprint calls.
	// NativeTick calls UpdateFlowSectionCountersStyleInternal directly with MyGeometry.
	UpdateFlowSectionCountersStyleInternal(GetCachedGeometry());
}

bool UFlowCounterWidget::UpdateFlowSectionCountersStyleInternal(const FGeometry& WidgetGeometry)
{
	if (FlowDataUniformGridPanel == nullptr) { return false; }
	if (!FlowSectionCounterWidgetClass) { return false; }
	if (WidgetGeometry.GetLocalSize().IsNearlyZero()) { return false; }
	if (CurrentFlowDataSections <= 0) { return false; }

	const int32 CurrentChildren = FlowDataUniformGridPanel->GetChildrenCount();
	int32 Difference = CurrentChildren - CurrentFlowDataSections;

	// TODO: may need to assess if the ptr array count matches the uniform grid children count

	if (Difference == 0) { return true; }

	if (Difference > 0)
	{
		// removal not yet implemented
	}
	else
	{
		FlowSectionCounters.Reserve(CurrentFlowDataSections);
		Difference = FMath::Abs(Difference);

		for (int32 i = 0; i < Difference; i++)
		{
			UFlowSectionCounter* NewSectionCounter = CreateNewFlowSectionCounterWidget();
			if (NewSectionCounter)
			{
				int32 Column = FlowSectionCounters.Add(NewSectionCounter);

				NewSectionCounter->SectionHeaderText = FText::FromString(FString::Printf(TEXT("Section %d Count:"), Column + 1));
				NewSectionCounter->SectionHeaderAgentCountText = FText::FromString("0");
				NewSectionCounter->FlowTypeTitleText = FText::FromString("Section Flow Rate:");
				NewSectionCounter->FlowValueText = FText::FromString("0.00m/s");

				FlowDataUniformGridPanel->AddChildToUniformGrid(NewSectionCounter, 0, Column);

				NewSectionCounter->SectionHeaderFieldAndTextWidget->SetUpdateTitleText(NewSectionCounter->SectionHeaderText);
				NewSectionCounter->SectionHeaderFieldAndTextWidget->SetUpdateFieldText(NewSectionCounter->SectionHeaderAgentCountText);
				NewSectionCounter->FlowTypeAndValueFieldAndTextWidget->SetUpdateTitleText(NewSectionCounter->FlowTypeTitleText);
				NewSectionCounter->FlowTypeAndValueFieldAndTextWidget->SetUpdateFieldText(NewSectionCounter->FlowValueText);

				UWidgetUtilHelpers::UniformGridFillCell(NewSectionCounter);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Failed to create new FlowSectionCounter widget."));
			}
		}
	}

	// TODO: implement scroll logic

	// Use geometry passed from NativeTick (always valid) instead of stale PaintSpaceGeometry
	const FVector2D DrawSize = WidgetGeometry.GetLocalSize();

	// Each section's text gets 80% of its uniform-grid cell width; full panel height as before.
	const FVector2D FitBox(DrawSize.X / CurrentFlowDataSections * 0.8f, DrawSize.Y);

	// Largest INTEGER size that fits every section's flow-value text (the wider of the two rows),
	// via the shared binary-search helper — same fit as FlowSectionCounter::InitializeFromParent.
	// Integer replaces the previous 0.1pt rounding, which defeated the Slate font cache and
	// shimmered as values changed during playback.
	int32 FittedSize = 14; // ceiling matches the previous clamp
	bool bMeasuredAny = false;
	for (UFlowSectionCounter* Widget : FlowSectionCounters)
	{
		if (Widget && Widget->FlowTypeAndValueFieldAndTextWidget)
		{
			if (Widget->FlowTypeAndValueFieldAndTextWidget->GetTextSize().IsNearlyZero())
			{
				continue; // Slate text not built yet for this section
			}
			bMeasuredAny = true;
			FittedSize = FMath::Min(FittedSize,
				UWidgetUtilHelpers::FindFittingFontSizeForFieldAndText(
					Widget->FlowTypeAndValueFieldAndTextWidget, FitBox, 1, 14));
		}
	}

	// Child widgets not yet measured — retry next tick
	if (!bMeasuredAny) { return false; }

	const float FinalFontSize = static_cast<float>(FittedSize);
	for (int32 i = 0; i < CurrentFlowDataSections; i++)
	{
		if (FlowSectionCounters[i] == nullptr) { continue; }
		// Only push the font when it differs — SetFontSize invalidates the text block.
		if (!FMath::IsNearlyEqual(FlowSectionCounters[i]->SectionHeaderFieldAndTextWidget->GetFontSize(), FinalFontSize))
		{
			FlowSectionCounters[i]->SectionHeaderFieldAndTextWidget->SetFontSize(FinalFontSize);
		}
		if (!FMath::IsNearlyEqual(FlowSectionCounters[i]->FlowTypeAndValueFieldAndTextWidget->GetFontSize(), FinalFontSize))
		{
			FlowSectionCounters[i]->FlowTypeAndValueFieldAndTextWidget->SetFontSize(FinalFontSize);
		}
	}

	return true;
}

UFlowSectionCounter* UFlowCounterWidget::CreateNewFlowSectionCounterWidget()
{
	if (!FlowSectionCounterWidgetClass->IsValidLowLevel()){ return nullptr; }
	
	// Create the widget and return it
	auto NewWidget = CreateWidget(this, FlowSectionCounterWidgetClass);

	// Defensive: ensure we created the widget and it is of the expected type
	if (!NewWidget) { return nullptr; }
	if (!NewWidget->IsA<UFlowSectionCounter>()) { return nullptr; }

	// Cast new widget to expected type
	auto NewFlowSectionCounter = Cast<UFlowSectionCounter>(NewWidget);
	
	return NewFlowSectionCounter;
}
