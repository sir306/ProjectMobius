// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MobiusControllerSubsystem.generated.h"

class UCapsuleComponent;
class UMobiusControllerSubsystem;
/*
 * The TMassExternalSubsystemTraits is required for this subsystem so it can be used with mass entity
 * i.e. the representation processor that calls on this subsystem
 */
template<>
struct TMassExternalSubsystemTraits<UMobiusControllerSubsystem> final
{
	enum
	{
		ThreadSafeRead = true,
		ThreadSafeWrite = false,
		GameThreadOnly = true, // needs to be game thread as we calling rendering api
	};
};

/**
 * 
 */
UCLASS()
class MOBIUSCORE_API UMobiusControllerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:
	/** Constructor */
	UMobiusControllerSubsystem();
	
	/** Initializer */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** De-Initializer */
	virtual void Deinitialize() override;

	/**
	 * Set the current player controller
	 * @param[APlayerController] PlayerController The player controller to set -> Mobius Controller is a subclass of APlayerController so this is safe
	 * @return returns true if the player controller was set successfully, false otherwise
	 */
	bool SetCurrentPlayerController(APlayerController* PlayerController);

	/**
	 * Get the current player controller
	 * @return returns the current player controller, or nullptr if it is not set
	 */
	void GetCurrentPlayerController(APlayerController*& OutPlayerController) const;

	/**
	 * Performs a calculation of the users mouse position into the world and the direction of the mouse, returns true if the calculation was successful
	 * 
	 * @param[FVector] OutMouseWorldPosition The world position of the mouse
	 * @param[FVector] OutWorldDirection The direction of the mouse in the world
	 * @return bool returns true if the calculation was successful, false otherwise
	 */
	bool ProjectMouseScreenToWorld(FVector& OutMouseWorldPosition, FVector& OutWorldDirection) const;

	/**
	 * Perform a line trace from the player controller's current mouse position in the world, this line trace will
	 * only search for capsule components, returning the first hit result and true if a hit was found, otherwise false and null hit result
	 *
	 * @param[FHitResult] OutHitResult The hit result of the line trace
	 * @param[UCapsuleComponent] OutCapsuleComponent The capsule component that was hit, if any
	 * @return bool returns true if a hit was found, false otherwise
	 */
	bool LineTraceFromMousePosition(FHitResult& OutHitResult, UCapsuleComponent*& OutCapsuleComponent) const;

	
	/**TODO: TBD if we can or want to use interfaces with mass ai entities, if so then we can use this method to perform a line trace
	 * Perform line trace from the player controller's camera to the mouse position in the world, this line trace will only seacrh for actors that implement the IClickable interface
	 */


private:
	/** A ptr to the player controller */
	UPROPERTY()
	TObjectPtr<APlayerController> CurrentPlayerController;
};
