// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/ScalabilitySettingWidget.h"

#include "Subsystems/PerformanceUtilSubsystem.h"

void UScalabilitySettingWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}

void UScalabilitySettingWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	
	InitializeScalabilityLevel();
}

void UScalabilitySettingWidget::NativeConstruct()
{
	Super::NativeConstruct();

	InitializeScalabilityLevel();
	// Update the scalability level when the widget is constructed
	UpdateScalabilityLevel();
}

void UScalabilitySettingWidget::InitializeScalabilityLevel()
{
	// Ensure the widget is initialized with the current scalability level
	if (auto PerformanceUtilSubsystem = GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>())
	{
		ScalabilityLevel = PerformanceUtilSubsystem->GetScalabilityLevel(ScalabilityCategory);

		// Check we don't have different level from global and if so, update scalability to match and call update scalability for this category
		if (PerformanceUtilSubsystem->GetGlobalScalabilitySetting() != EGss_Custom) // global isn't custom so the value must match	
		{
			uint8 GlobalScalabilityLevel = static_cast<uint8>(PerformanceUtilSubsystem->GetGlobalScalabilitySetting());
			if (ScalabilityLevel != GlobalScalabilityLevel)
			{
				// Update the scalability level to match the global setting
				ScalabilityLevel = static_cast<EScalabilitySettings>(GlobalScalabilityLevel);
				PerformanceUtilSubsystem->ApplyScalabilityLevel(ScalabilityLevel, ScalabilityCategory);
			}
		}
		
	}
}

void UScalabilitySettingWidget::UpdateScalabilityLevel()
{
	if (auto PerformanceUtilSubsystem = GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>())
	{
		if (ScalabilityCategory != ESc_Resolution)
		{
			// Apply the scalability level to the Performance Util Subsystem
			PerformanceUtilSubsystem->ApplyScalabilityLevel(ScalabilityLevel, ScalabilityCategory);

		}
		else if (ScalabilityCategory == ESc_Global)
		{
			// currently done in BP
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
