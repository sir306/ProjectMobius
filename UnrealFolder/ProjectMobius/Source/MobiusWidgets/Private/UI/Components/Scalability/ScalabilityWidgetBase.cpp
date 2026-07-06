// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Components/Scalability/ScalabilityWidgetBase.h"

#include "Subsystems/PerformanceUtilSubsystem.h"

void UScalabilityWidgetBase::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}

void UScalabilityWidgetBase::NativePreConstruct()
{
	InitializeScalabilityLevel();
	
	Super::NativePreConstruct();
	
}

void UScalabilityWidgetBase::NativeConstruct()
{
	InitializeScalabilityLevel();
	// Update the scalability level when the widget is constructed
	UpdateScalabilityLevel();
	
	Super::NativeConstruct();
}

void UScalabilityWidgetBase::InitializeScalabilityLevel()
{
	// Design-time contexts (UMG Designer preview, asset thumbnails, FWidgetRenderer) have no valid
	// game world or world subsystems. NativePreConstruct runs this in those contexts, where
	// GetWorld()->GetSubsystem<>() dereferences a torn-down subsystem collection and crashes
	// (GetSubsystemInternal -> TMapBase::FindRef). Only touch the subsystem in a real game/PIE world.
	if (IsDesignTime())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	// Ensure the widget is initialized with the current scalability level
	if (auto PerformanceUtilSubsystem = World->GetSubsystem<UPerformanceUtilSubsystem>())
	{
		ScalabilityLevel = PerformanceUtilSubsystem->GetScalabilityLevel(ScalabilityCategory);

		// Check we don't have different level from global and if so, update scalability to match and call update scalability for this category
		if (PerformanceUtilSubsystem->GetGlobalScalabilitySetting() != EGss_Custom) // global isn't custom so the value must match	
		{
			uint8 GlobalScalabilityLevel = PerformanceUtilSubsystem->GetGlobalScalabilitySetting();
			if (ScalabilityLevel != GlobalScalabilityLevel)
			{
				// Update the scalability level to match the global setting
				ScalabilityLevel = static_cast<EScalabilitySettings>(GlobalScalabilityLevel);
				PerformanceUtilSubsystem->ApplyScalabilityLevel(ScalabilityLevel, ScalabilityCategory);
			}
		}
		
	}
}

void UScalabilityWidgetBase::UpdateScalabilityLevel()
{
	// Same design-time / world guard as InitializeScalabilityLevel — NativeConstruct can also fire
	// in preview/render contexts with no valid game world.
	if (IsDesignTime())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	if (auto PerformanceUtilSubsystem = World->GetSubsystem<UPerformanceUtilSubsystem>())
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
