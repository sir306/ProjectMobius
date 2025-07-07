// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/Scalability/AdjustAvatarModel.h"

#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
#include "Subsystems/PerformanceUtilSubsystem.h"

void UAdjustAvatarModel::NativePreConstruct()
{
	Super::NativePreConstruct();
}

void UAdjustAvatarModel::NativeConstruct()
{
	Super::NativeConstruct();
}

EPedestrianScalabilitySettings UAdjustAvatarModel::GetCurrentAvatarModelType() const
{
	//TODO: we have method duplicates that we need to rename and call where appropriate

	// Get the current avatar model type from the performance util subsystem?? or from the MRS subsystem
	if (UMRS_RepresentationSubsystem* MRS_RepSystem = GetWorld()->GetSubsystem<UMRS_RepresentationSubsystem>())
	{
		return MRS_RepSystem->GetPedestrianScalabilitySetting();
	}
	
	return EPedestrianScalabilitySettings::EPss_High;
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
	if (auto MRS_RepSystem = GetWorld()->GetSubsystem<UMRS_RepresentationSubsystem>())
	{
		MRS_RepSystem->SetPedestrianScalabilitySetting(CurrentAvatarModelType);
	}
}
