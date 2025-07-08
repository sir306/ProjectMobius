// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/Scalability/AdjustAvatarModel.h"

#include "Components/CheckBox.h"
#include "Subsystems/PerformanceUtilSubsystem.h"

void UAdjustAvatarModel::NativePreConstruct()
{
	Super::NativePreConstruct();
}

void UAdjustAvatarModel::NativeConstruct()
{
	Super::NativeConstruct();
	// Get the current avatar model type from the performance util subsystem and bind to its delegate
	if (UPerformanceUtilSubsystem* PerformanceUtilSubsystem = GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>())
	{
		AutoUpdateToggleState();
		PerformanceUtilSubsystem->OnAutoScalabilityChanged.AddUObject(this, &UAdjustAvatarModel::AutoUpdateToggleState);
	}
}

void UAdjustAvatarModel::NativeDestruct()
{
	// Unbind the delegate to avoid dangling pointers
	if (UPerformanceUtilSubsystem* PerformanceUtilSubsystem = GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>())
	{
		PerformanceUtilSubsystem->OnAutoScalabilityChanged.RemoveAll(this);
	}
	
	Super::NativeDestruct();
}

UAdjustAvatarModel::~UAdjustAvatarModel()
{
	
}

EPedestrianScalabilitySettings UAdjustAvatarModel::GetCurrentAvatarModelType() const
{
	//TODO: we have method duplicates that we need to rename and call where appropriate

	// Get the current avatar model type from the performance util subsystem
	if (UPerformanceUtilSubsystem* PerformanceUtilSubsystem = GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>())
	{
		return PerformanceUtilSubsystem->GetCurrentPedestrianAvatarType();
	}
	
	return EPedestrianScalabilitySettings::EPss_High;
}

void UAdjustAvatarModel::AutoUpdateToggleState()
{
	// while the logic is being developed we can just call this method but may require need to change it later
	CurrentAvatarModelType = GetCurrentAvatarModelType();

	// return if the checkbox is not valid
	if (!HighLowPolyToggle)
	{
		return;
	}
	
	if (CurrentAvatarModelType == EPss_High)
	{
		HighLowPolyToggle->SetCheckedState(ECheckBoxState::Unchecked);
	}
	else
	{
		HighLowPolyToggle->SetCheckedState(ECheckBoxState::Checked);
	}
}

void UAdjustAvatarModel::ToggleAvatarModel()
{
	// while developing just assign the value here from the subsystem
	CurrentAvatarModelType = GetCurrentAvatarModelType();
	
	if (CurrentAvatarModelType == EPedestrianScalabilitySettings::EPss_High)
	{
		CurrentAvatarModelType = EPedestrianScalabilitySettings::EPss_Low;
	}
	else
	{
		CurrentAvatarModelType = EPedestrianScalabilitySettings::EPss_High;
	}

	// set the new type on the MRS subsystem
	if (UPerformanceUtilSubsystem* PerformanceUtilSubsystem = GetWorld()->GetSubsystem<UPerformanceUtilSubsystem>())
	{
		PerformanceUtilSubsystem->SetCurrentPedestrianAvatarType(CurrentAvatarModelType);
	}
}
