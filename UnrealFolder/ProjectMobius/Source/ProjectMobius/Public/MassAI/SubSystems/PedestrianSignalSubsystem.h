// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassSubsystemBase.h"
#include "MassExternalSubsystemTraits.h"
#include "PedestrianSignalSubsystem.generated.h"

namespace PedestrianDataSignals::Signals
{
	const FName ActivateCollisions = TEXT("ActivateCollisions");
	const FName DeactivateCollisions = TEXT("DeactivateCollisions");
}

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UPedestrianSignalSubsystem : public UMassSubsystemBase
{
	GENERATED_BODY()

public:
	UPedestrianSignalSubsystem();

	UFUNCTION()
	void CollisionsSettingChanged(uint8 EnableDisable/* 0 = Disable, 1 = Enable */);
	UFUNCTION(BlueprintCallable)
	void CollisionsSettingChanged2(bool EnableDisable/* 0 = Disable, 1 = Enable */);

	void ActivateCollisions();
	void DeactivateCollisions();

	void AssignEntitysToSignal(const TArray<FMassEntityHandle>& Entities)
	{
		EntitiesToSignal.Append(Entities);
	}

	void ClearEntitiesToSignal()
	{ 
		EntitiesToSignal.Empty(); 
	}

protected:
	// USubsystem implementation Begin
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// USubsystem implementation End

private:
	/**
	 * 
	 */
	UPROPERTY()
	TArray<FMassEntityHandle> EntitiesToSignal;
};


template<>
struct TMassExternalSubsystemTraits<UPedestrianSignalSubsystem>
{
	enum
	{
		GameThreadOnly = true
	};
};