// Fill out your copyright notice in the Description page of Project Settings.


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
