// Fill out your copyright notice in the Description page of Project Settings.


#include "Widgets/Simulation/SimulationSetupWidget.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"

void USimulationSetupWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

}

void USimulationSetupWidget::NativeConstruct()
{
	// Call the parent implementation
	Super::NativeConstruct();

	// Setup the widget components
	SetupWidgetComponents();
}

void USimulationSetupWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
}

void USimulationSetupWidget::SetupWidgetComponents()
{
	// Setup the time dilation text block
	if(CurrentTimeDilationScaleText->IsValidLowLevel())
	{
		CurrentTimeDilationScaleText->SetVisibility(ESlateVisibility::Visible);

		// Update the current time dilation scale text
		UpdateCurrentTimeDilationScaleText();
	}

	// Setup the time dilation editable text
	if(TimeDilationScaleEditableTextBox->IsValidLowLevel())
	{
		TimeDilationScaleEditableTextBox->SetVisibility(ESlateVisibility::Visible);
	}
	
	// Setup the update time dilation button
	if(UpdateTimeDilationButton->IsValidLowLevel())
	{
		UpdateTimeDilationButton->SetVisibility(ESlateVisibility::Visible);

		// Bind the button click event if not in design time
		if(!IsDesignTime())
		{
			UpdateTimeDilationButton->OnClicked.AddDynamic(this, &USimulationSetupWidget::UpdateTimeDilation);
		}

	}
}

void USimulationSetupWidget::UpdateCurrentTimeDilationScaleText()
{
	// Get the current time dilation scale
	UWorld* World = GetWorld();
	if (World)
	{
		float CurrentTimeDilationScale = IProjectMobiusInterface::GetMobiusGameInstanceSimulationTimeDilatationFactor(World);
		CurrentTimeDilationScaleText->SetText(FText::FromString(FString::SanitizeFloat(CurrentTimeDilationScale)));
	}
}

void USimulationSetupWidget::UpdateTimeDilation()
{
	//TODO: Setup the editable text box to only accept numerical values

	// Get the text from the editable text box
	FString TimeDilationScaleString = TimeDilationScaleEditableTextBox->GetText().ToString();

	// Convert the string to a float
	float TimeDilationScale = FCString::Atof(*TimeDilationScaleString);

	// Update the time dilation scale
	UpdateTimeDilationScale(TimeDilationScale);

	// Update the current time dilation scale text
	UpdateCurrentTimeDilationScaleText();
}

void USimulationSetupWidget::UpdateTimeDilationScale(float TimeDilationScale)
{
	UWorld* World = GetWorld();

	if (World)
	{
		IProjectMobiusInterface::UpdateMobiusGameInstanceSimulationTimeDilatationFactor(World, TimeDilationScale);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("World is null"));
	}
}
