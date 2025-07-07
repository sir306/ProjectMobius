// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/Scalability/ResolutionScalabilityWidget.h"
#include "Subsystems/PerformanceUtilSubsystem.h"
#include "Util/WidgetUtilHelpers.h"

// lambda to convert FIntPoint to FString with a formatting of 'X: {x}, Y: {y}'
auto FormatResolution = [](const FIntPoint& Resolution) -> FString
{
	return FString::Printf(TEXT("X: %d, Y: %d"), Resolution.X, Resolution.Y);
};

void UResolutionScalabilityWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	UpdateCurrentScreenResolutionVal();
}

void UResolutionScalabilityWidget::UpdateCurrentScreenResolutionVal()
{
	if (auto PerformanceUtilSubsystem = GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>())
	{
		if (ScalabilityCategory == ESc_Resolution)
		{
			CurrentScreenResolution = PerformanceUtilSubsystem->GetCurrentScreenResolution();

			if (CurrentScreenResolution == FIntPoint(0,0))
			{
				CurrentScreenResolution = FIntPoint(1280, 720);// Default resolution if none is set (as seen in the .ini file)
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Performance Util Subsystem not found!"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Performance Util Subsystem not found!"));
		}
	}
}

void UResolutionScalabilityWidget::UpdateScalabilityLevel()
{
	
}

TArray<FIntPoint> UResolutionScalabilityWidget::GetAllAvailableResolutions() const
{
	if (auto PerformanceUtilSubsystem = GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>())
	{
		return PerformanceUtilSubsystem->GetSystemScreenResolutions();
	}
	return TArray<FIntPoint>();
}

void UResolutionScalabilityWidget::UpdateScreenResolution(FIntPoint NewResolution)
{
	CurrentScreenResolution = NewResolution;
	
	if (auto PerformanceUtilSubsystem = GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>())
	{
		if (ScalabilityCategory == ESc_Resolution)
		{
			if (CurrentScreenResolution == FIntPoint(0,0) || CurrentScreenResolution.X <= 0 || CurrentScreenResolution.Y <= 0)
			{
				// If the resolution is set to (0,0), we will set it to the current screen resolution or the default resolution
				UpdateCurrentScreenResolutionVal();
			}
			
			PerformanceUtilSubsystem->UpdateScreenResolutions(CurrentScreenResolution);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Performance Util Subsystem not found!"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Performance Util Subsystem not found!"));
		}
	}
}

void UResolutionScalabilityWidget::PopulateResolutionComboBox()
{
	// Check if the Performance Util Subsystem and combo box is valid
	if (!GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>() || !ResolutionComboBox)
	{
		UE_LOG(LogTemp, Warning, TEXT("Performance Util Subsystem or Resolution Combo Box not found!"));
		return;
	}
	
	// Get all available resolutions from the Performance Util Subsystem
	TArray<FIntPoint> AvailableResolutions = GetAllAvailableResolutions();

	TArray<FString> ResolutionOptions;
	
	// build the resolution options array
	for (const FIntPoint& Resolution : AvailableResolutions)
	{
		ResolutionOptions.Add(FormatResolution(Resolution));
	}

	// Create the current resolution string
	FString CurrentResolutionStr = FormatResolution(CurrentScreenResolution);

	// Update the combo box options with the formatted resolution strings
	WidgetUtilHelpers::UpdateComboBoxOptions(ResolutionComboBox, ResolutionOptions, CurrentResolutionStr);
}

void UResolutionScalabilityWidget::SetSelectedDropdownOption(const FIntPoint& NewOption)
{
	FString NewOptionStr = FormatResolution(NewOption);
	WidgetUtilHelpers::FindAndSetComboBoxOption(ResolutionComboBox, NewOptionStr);
}
