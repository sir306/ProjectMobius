// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassSubsystemBase.h"
#include "MassExternalSubsystemTraits.h"
#include "PedestrianSignalSubsystem.generated.h"

/** */
namespace PedestrianDataSignals::Signals
{
	// Activate signal for pedestrian collisions - at present only used for selecting agents
	const FName ActivateCollisions = TEXT("ActivateCollisions");
	// Deactivate signal for pedestrian collisions
	const FName DeactivateCollisions = TEXT("DeactivateCollisions");

	// Activate Signal for Flow Counter Logic
	const FName ActivateFlowCounter = TEXT("ActivateFlowCounter");
	// Deactivate Signal for Flow Counter Logic
	const FName DeactivateFlowCounter = TEXT("DeactivateFlowCounter");
}

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UPedestrianSignalSubsystem : public UMassSubsystemBase
{
	GENERATED_BODY()

public:
	/** */
	UPedestrianSignalSubsystem();

	/** */
	UFUNCTION(BlueprintCallable)
	void CollisionsSettingChanged(uint8 EnableDisable/* 0 = Disable, 1 = Enable */);

	/** */
	void ActivateCollisions();

	/** */
	void DeactivateCollisions();

	/** */
	UFUNCTION(BlueprintCallable)
	void FlowCounterSettingChanged(uint8 EnableDisable/* 0 = Disable, 1 = Enable */);

	/** */
	void ActivateFlowCounter();

	/** */
	void DeactivateFlowCounter();

protected:
	// USubsystem implementation Begin
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// USubsystem implementation End
	
};


template<>
struct TMassExternalSubsystemTraits<UPedestrianSignalSubsystem>
{
	enum
	{
		GameThreadOnly = true
	};
};