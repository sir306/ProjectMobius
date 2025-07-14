// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/MobiusControllerSubsystem.h"

#include "Components/CapsuleComponent.h"

UMobiusControllerSubsystem::UMobiusControllerSubsystem()
{
}

void UMobiusControllerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UMobiusControllerSubsystem::Deinitialize()
{
	// Clear any references or cleanup tasks here if needed
	CurrentPlayerController = nullptr;
	
	Super::Deinitialize();
}

bool UMobiusControllerSubsystem::SetCurrentPlayerController(APlayerController* PlayerController)
{
	// Check if the PlayerController is valid
	if (PlayerController)
	{
		// Set the current player controller
		CurrentPlayerController = PlayerController;
		return true;
	}
	else
	{
		//UE_LOG(LogTemp, Warning, TEXT("UMobiusControllerSubsystem::SetCurrentPlayerController - PlayerController is null"));
		return false;
	}
}

void UMobiusControllerSubsystem::GetCurrentPlayerController(APlayerController*& OutPlayerController) const
{
	OutPlayerController = CurrentPlayerController;
}

bool UMobiusControllerSubsystem::ProjectMouseScreenToWorld(FVector& OutMouseWorldPosition,
	FVector& OutWorldDirection) const
{
	// Check if the current player controller is valid
	if (CurrentPlayerController)
	{
		// Get the mouse position in screen space
		float MouseX, MouseY;
		CurrentPlayerController->GetMousePosition(MouseX, MouseY);

		// Deproject the mouse position to world space
		if (CurrentPlayerController->DeprojectScreenPositionToWorld(MouseX, MouseY, OutMouseWorldPosition, OutWorldDirection))
		{
			// Successfully deprojected the mouse position
			return true;
		}
		else
		{
			//UE_LOG(LogTemp, Warning, TEXT("UMobiusControllerSubsystem::ProjectMouseScreenToWorld - Deprojection failed"));
			return false;
		}
	}
	else
	{
		//UE_LOG(LogTemp, Warning, TEXT("UMobiusControllerSubsystem::ProjectMouseScreenToWorld - Controller is null"));
		return false;
	}
}

bool UMobiusControllerSubsystem::LineTraceFromMousePosition(FHitResult& OutHitResult,
	UCapsuleComponent*& OutCapsuleComponent) const
{
	// Check if the current player controller is valid
	if (CurrentPlayerController)
	{
		FVector MouseWorldPosition, WorldDirection;
		if (ProjectMouseScreenToWorld(MouseWorldPosition, WorldDirection))
		{
			// Perform a line trace from the mouse position in the world
			FCollisionQueryParams CollisionParams;
			CollisionParams.AddIgnoredActor(CurrentPlayerController->GetPawn()); // Ignore the player pawn

			// bool bHit = CurrentPlayerController->GetWorld()->LineTraceSingleByChannel(
			// 	OutHitResult, MouseWorldPosition, MouseWorldPosition + (WorldDirection * 10000.0f), ECC_Visibility, CollisionParams);
			bool bHit = CurrentPlayerController->GetWorld()->LineTraceSingleByProfile(OutHitResult,
				MouseWorldPosition, MouseWorldPosition + (WorldDirection * 10000.0f), FName("Pawn"), CollisionParams);

			if (bHit && OutHitResult.GetComponent()->IsA<UCapsuleComponent>())
			{
				OutCapsuleComponent = Cast<UCapsuleComponent>(OutHitResult.GetComponent());
				return true; // Hit a capsule component
			}
			else
			{
				OutCapsuleComponent = nullptr; // No capsule component hit
				// Log a warning if no capsule component was hit
				//UE_LOG(LogTemp, Warning, TEXT("UMobiusControllerSubsystem::LineTraceFromMousePosition - No capsule component hit"));
				return false;
			}
		}
		else
		{
			// Log a warning if the deprojection failed
			//UE_LOG(LogTemp, Warning, TEXT("UMobiusControllerSubsystem::LineTraceFromMousePosition - Deprojection failed"));
			return false; // Deprojection failed
		}
	}
	else
	{
		// Log a warning if the player controller is null
		//UE_LOG(LogTemp, Warning, TEXT("UMobiusControllerSubsystem::LineTraceFromMousePosition - Player controller is null"));
		return false; // Player controller is null
	}
}
