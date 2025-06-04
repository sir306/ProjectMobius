// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/Fragments/EntityInfoFragment.h"

#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"

FEntityInfoFragment::FEntityInfoFragment()
{
	// Set the default values
	EntityID = 0;
	EntityName = "Default[0]";
	EntitySimTimeS = "0.0";
	EntityMaxSpeed = 1.0f;
	EntityM_Plane = "F#0";
	EntityMap = 0;
	// Values to store location and rotation of entity
	CurrentLocation = FVector(0.0f, 0.0f, 0.0f);
	CurrentRotation = FRotator(0.0f, 0.0f, 0.0f);
	// value to store the enitity's current render
	bRenderAgent = true;
	InstanceID = 0;
	bIsMale = true;
	bReadyToDestroy = false;
	CurrentSpeed = 0.0f;
	CurrentMovementBracket = EPedestrianMovementBracket::Emb_NotMoving;
}

FEntityInfoFragment::FEntityInfoFragment(int32 InEntityID, FString InEntityName, FString InEntitySimTimeS, float InEntityMaxSpeed, FString InEntityM_Plane, int32 InEntityMap)
{
	// Set the values
	EntityID = InEntityID;
	EntityName = InEntityName;
	EntitySimTimeS = InEntitySimTimeS;
	EntityMaxSpeed = InEntityMaxSpeed;
	EntityM_Plane = InEntityM_Plane;
	EntityMap = InEntityMap;
	// Values to store location and rotation of entity -- TODO add construction for these
	CurrentLocation = FVector(0.0f, 0.0f, 0.0f);
	CurrentRotation = FRotator(0.0f, 0.0f, 0.0f);
	// value to store the enitity's current render
	bRenderAgent = true;
	InstanceID = 0;
	bIsMale = !(EntityName.Contains("Female"));
	bReadyToDestroy = false;
	CurrentSpeed = 0.0f;
	CurrentMovementBracket = EPedestrianMovementBracket::Emb_NotMoving;

	// if the entity name contains a child, adult or elderly then we need to set the age demographic accordingly
	if (EntityName.Contains("Child"))
	{
		AgeDemographic = EAgeDemographic::Ead_Child;
	}
	else if (EntityName.Contains("Elderly"))
	{
		AgeDemographic = EAgeDemographic::Ead_Elderly;
	}
	else if (EntityName.Contains("Adult"))
	{
		AgeDemographic = EAgeDemographic::Ead_Adult;
	}
	else // no valid age demographic found -> TODO: for now just set it to adult but need to think on how we want to handle this
	{
		AgeDemographic = EAgeDemographic::Ead_Adult;
	}
}

FEntityInfoFragment::~FEntityInfoFragment()
{
}
