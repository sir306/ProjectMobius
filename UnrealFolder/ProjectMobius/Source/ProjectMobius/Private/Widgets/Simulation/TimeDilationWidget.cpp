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

#include "Widgets/Simulation/TimeDilationWidget.h"
#include "Components/TextBlock.h" // UTextBlock component
//#include "Components/RichTextBlock.h" // URichTextBlock component TODO
#include "Kismet/GameplayStatics.h"
#include <MassAI/SubSystems/TimeDilationSubSystem.h>


void UTimeDilationWidget::NativeConstruct()
{
	// Call the parent class construct function
	Super::NativeConstruct();

	// Update the simulation time text block
	//UpdateSimulationTimeTextBlock();

	SetTimeDilationSubsytem();
}

void UTimeDilationWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	// Call the parent class tick function
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (TimeDilationSubSystem)
	{
		// Update the simulation time text block
		UpdateSimulationTimeTextBlock();
	}
	else 
	{
		SetTimeDilationSubsytem();
	}
}

void UTimeDilationWidget::UpdateSimulationTimeTextBlock()
{
	//TODO: change to FTEXT so we can format the text better and have it localized
	// create a formatted string to display the current simulation time as we want to show hh:mm:ss:ms
	FString SimulationTimeText = FString("Simulation Elapsed Time:");

	// Add the current simulation time to the formatted string
	SimulationTimeText += TimeDilationSubSystem->GetCurrentSimTimeStr();

	// Set the text block to display the formatted string
	CurrentSimTimeTextBlock->SetText(FText::FromString(SimulationTimeText));
}

void UTimeDilationWidget::SetTimeDilationSubsytem()
{
	if (GetWorld() == nullptr)
	{
		return;
	}
	// Get the TimeDilationSubSystem
	TimeDilationSubSystem = GetWorld()->GetSubsystem<UTimeDilationSubSystem>();
}
