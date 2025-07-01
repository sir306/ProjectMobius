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
}

void UScalabilitySettingWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void UScalabilitySettingWidget::UpdateScalabilityLevel()
{
	if (auto PerformanceUtilSubsystem = GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>())
	{
		// Get the scalability level list from the widget
		TArray<FString> ScalabilityLevelList = GetScalabilityLevelList();

		// Apply the scalability level via the performance subsystem
		PerformanceUtilSubsystem->ApplyConsoleCommands(ScalabilityLevelList);
	}
}
