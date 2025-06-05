// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 Nicholas R. Harding
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.  
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,  
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL  
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR  
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING  
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS  
 * IN THE SOFTWARE.
 */

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
