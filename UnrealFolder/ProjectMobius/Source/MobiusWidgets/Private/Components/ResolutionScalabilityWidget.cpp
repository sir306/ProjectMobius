// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/ResolutionScalabilityWidget.h"
#include "Subsystems/PerformanceUtilSubsystem.h"

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
