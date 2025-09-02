// Fill out your copyright notice in the Description page of Project Settings.


#include "InWorldUI/FlowCounterWidget.h"

#include "Components/FlowSectionCounter.h"
#include "Components/UniformGridPanel.h"
#include "Components/CustomSlateWidgets/FieldAndTextWidget.h"
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
		WidgetUtilHelpers::UniformGridFillCell(LiveAgentCountFieldAndTextWidget);
		WidgetUtilHelpers::UniformGridFillCell(FlowDataUniformGridPanel);

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
	
	UpdateFlowSectionCountersStyle();
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

	LiveAgentCountFieldAndTextWidget->SetTitleText(TitleText);
	LiveAgentCountFieldAndTextWidget->SetFieldText(FText::AsNumber(0));

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
	UpdateFlowSectionCountersStyle();
}

void UFlowCounterWidget::RemoveFlowSectionCounter()
{
	// Decrement the section count - as we clamp to 1 we can safely decrement first here
	CurrentFlowDataSections--;
	
	// Clamp to a minimum of 1 section
	SetFlowDataSections(CurrentFlowDataSections);

	// Update the style after changing the section count
	UpdateFlowSectionCountersStyle();
}

void UFlowCounterWidget::UpdateFlowSectionCountersStyle()
{
	// Defensive: ensure we have the uniform grid panel to add to
	if (FlowDataUniformGridPanel == nullptr) { return; }

	// Defensive: ensure we have a valid section counter widget class to spawn
	if (!FlowSectionCounterWidgetClass) { return; }

	// ensure geometry has been computed
	if (GetCachedGeometry().GetLocalSize().IsNearlyZero() ) {return;}
	

	// get the current number of children in the grid panel
	const int32 CurrentChildren = FlowDataUniformGridPanel->GetChildrenCount();

	// get the difference between current children and desired sections
	int32 Difference = CurrentChildren - CurrentFlowDataSections;

	// TODO: may need to assess if the ptr array count matches the uniform grid children count

	// if the same number of children as sections, we can return early
	if (Difference == 0) { return; }

	// if we have more children than sections, we need to remove some
	if (Difference > 0)
	{
		
	}
	else // we have fewer children than sections, we need to add some
	{
		// Reserve space in the array for the new widgets
		FlowSectionCounters.Reserve(CurrentFlowDataSections);

		// convert difference to positive value - we know it's negative here and this just makes the loop clearer
		Difference = FMath::Abs(Difference);
		
		// create and add the new widgets
		for (int32 i = 0; i < Difference; i++)
		{
			UFlowSectionCounter* NewSectionCounter = CreateNewFlowSectionCounterWidget();
			if (NewSectionCounter)
			{
				int32 Column = FlowSectionCounters.Add(NewSectionCounter);

				// configure text before adding to the grid
				NewSectionCounter->SectionHeaderText = FText::FromString(FString::Printf(TEXT("Section %d Count:"), Column + 1));
				NewSectionCounter->SectionHeaderAgentCountText = FText::FromString("0");
				NewSectionCounter->FlowTypeTitleText = FText::FromString("Section Flow Rate:");
				NewSectionCounter->FlowValueText = FText::FromString("0.00m/s");
				
				FlowDataUniformGridPanel->AddChildToUniformGrid(NewSectionCounter, 0, Column);

				NewSectionCounter->SectionHeaderFieldAndTextWidget->SetTitleText(NewSectionCounter->SectionHeaderText);
				NewSectionCounter->SectionHeaderFieldAndTextWidget->SetFieldText(NewSectionCounter->SectionHeaderAgentCountText);
				
				NewSectionCounter->FlowTypeAndValueFieldAndTextWidget->SetTitleText(NewSectionCounter->FlowTypeTitleText);
				NewSectionCounter->FlowTypeAndValueFieldAndTextWidget->SetFieldText(NewSectionCounter->FlowValueText);
				
				// Ensure the new widget fills its cell
				WidgetUtilHelpers::UniformGridFillCell(NewSectionCounter);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Failed to create new FlowSectionCounter widget."));
			}
		}
	}

	// TODO: implement scroll logic

	// Recompute the uniform grid layout

	// Get the current draw size of the grid panel
	FVector2D DrawSize = RootUniformGridPanel->GetPaintSpaceGeometry().GetAbsoluteSize();

	// Fallback to render bounding rect size if absolute size is zero
	if (DrawSize == FVector2D::ZeroVector)
	{
		DrawSize = RootUniformGridPanel->GetPaintSpaceGeometry().GetRenderBoundingRect().GetSize();
	}

	// Compute the layout
	float CellWidth = DrawSize.X / CurrentFlowDataSections;

	FVector2D TextSize = FVector2D::ZeroVector;

	// log the count of section counters
	UE_LOG(LogTemp, Warning, TEXT("FlowSectionCounters count: %d"), FlowSectionCounters.Num());
	// difference count
	UE_LOG(LogTemp, Warning, TEXT("Difference count: %d"), Difference);
	// current flow data sections
	UE_LOG(LogTemp, Warning, TEXT("CurrentFlowDataSections: %d"), CurrentFlowDataSections);
	// draw size
	UE_LOG(LogTemp, Warning, TEXT("DrawSize: %s"), *DrawSize.ToString());
	// cell width
	UE_LOG(LogTemp, Warning, TEXT("CellWidth: %f"), CellWidth);

	float CurrentFontSize = 14.0f; // default font size
	
	// loop through all the title field widgets and get the largest text measurement size
	for (UFlowSectionCounter* Widget : FlowSectionCounters)
	{
		if (Widget)
		{
			FVector2D CurrentTextSize = Widget->FlowTypeAndValueFieldAndTextWidget->GetTextSize();
			TextSize.X = FMath::Max(TextSize.X, CurrentTextSize.X);
			TextSize.Y = FMath::Max(TextSize.Y, CurrentTextSize.Y);
			CurrentFontSize = Widget->FlowTypeAndValueFieldAndTextWidget->GetFontSize();
		}
	}

	// if the text size is zero, we can't compute a font size
	if (TextSize == FVector2D::ZeroVector) { return; }

	// log font size and text size
	UE_LOG(LogTemp, Warning, TEXT("CurrentFontSize: %f"), CurrentFontSize);
	UE_LOG(LogTemp, Warning, TEXT("TextSize: %s"), *TextSize.ToString());

	// need to calculate the margins and padding, so have to account for that in the cell width

	// TODO: get the padding correctly etc
	CellWidth *= 0.8f; // assume 20% padding/margin for now
	
	// calculate the font size that will fit in the cell width	
	// Compute scale factor to fit in box (maintain aspect ratio) this way we can scale the font size appropriately
	float ScaleX = (CellWidth / TextSize.X) ? CellWidth / TextSize.X : 0.0f; // Avoid division by zero
	float ScaleY = (DrawSize.Y / TextSize.Y) ? DrawSize.Y / TextSize.Y : 0.0f; // Avoid division by zero
	float UniformScale = FMath::Min( FMath::Clamp(ScaleX, 0, ScaleX),  FMath::Clamp(ScaleY, 0, ScaleY)); // Ensure scale is non-negative and min val of 0
	
	// Adjust font size
	float FinalFontSize = FMath::Clamp((CurrentFontSize * UniformScale), 1, 20); // Text should never be allowed to be bigger than 20

	// Apply the new layout to the grid and its children
	for (int32 i = 0; i < CurrentFlowDataSections; i++)
	{
		if (FlowSectionCounters[i] == nullptr) { continue; }
		FlowSectionCounters[i]->SectionHeaderFieldAndTextWidget->SetFontSize(FinalFontSize);
		FlowSectionCounters[i]->FlowTypeAndValueFieldAndTextWidget->SetFontSize(FinalFontSize);
	}

	// log final font size and the text size and uniform scale
	UE_LOG(LogTemp, Warning, TEXT("FinalFontSize: %f"), FinalFontSize);
	UE_LOG(LogTemp, Warning, TEXT("UniformScale: %f"), UniformScale);
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
