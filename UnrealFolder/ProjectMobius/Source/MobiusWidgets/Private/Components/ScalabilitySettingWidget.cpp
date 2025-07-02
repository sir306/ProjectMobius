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
	
	// Ensure the widget is initialized with the current scalability level
	if (auto PerformanceUtilSubsystem = GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>())
	{
		ScalabilityLevel = PerformanceUtilSubsystem->GetScalabilityLevel(ScalabilityCategory);
	}
}

void UScalabilitySettingWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Update the scalability level when the widget is constructed
	UpdateScalabilityLevel();
}

void UScalabilitySettingWidget::UpdateScalabilityLevel()
{
	if (auto PerformanceUtilSubsystem = GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>())
	{
		if (ScalabilityCategory != ESc_Resolution)
		{
			// Apply the scalability level to the Performance Util Subsystem
			PerformanceUtilSubsystem->ApplyScalabilityLevel(ScalabilityLevel, ScalabilityCategory);
		
			// Log the applied scalability level
			UE_LOG(LogTemp, Log, TEXT("Applied Scalability Level: %s for Category: %s"), 
				*UEnum::GetValueAsString(ScalabilityLevel), 
				*UEnum::GetValueAsString(ScalabilityCategory));
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
