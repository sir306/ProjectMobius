// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/MobiusControllerSubsystem.h"

#include "KismetProceduralMeshLibrary.h"
#include "BuildingGenerator/RuntimeMeshBuilder.h"
#include "Components/CapsuleComponent.h"
#include "Diagnostics/MobiusClickLog.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

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
	UCapsuleComponent*& OutCapsuleComponent)
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

			// Get the runtime mesh generator actor and ignore it
			if (RuntimeMeshGeneratorActor.Get())
			{
				// Ignore the runtime mesh generator actor
				CollisionParams.AddIgnoredActor(RuntimeMeshGeneratorActor.Get());
			}
			else
			{
				// if hasn't been set, then attempt to set again
				GetRuntimeMeshBuilderFromWorld();
			}

			// bool bHit = CurrentPlayerController->GetWorld()->LineTraceSingleByChannel(
			// 	OutHitResult, MouseWorldPosition, MouseWorldPosition + (WorldDirection * 10000.0f), ECC_Visibility, CollisionParams);
			// bool bHit = CurrentPlayerController->GetWorld()->LineTraceSingleByProfile(OutHitResult,
			// 	MouseWorldPosition, MouseWorldPosition + (WorldDirection * 10000.0f), FName("Pawn"), CollisionParams);

			// ECCollisionChannel TraceChannel = ECC_GameTraceChannel1; // Assuming ECC_GameTraceChannel1 is set for capsule components
			
			bool bHit = CurrentPlayerController->GetWorld()->LineTraceSingleByChannel(
				OutHitResult, MouseWorldPosition, MouseWorldPosition + (WorldDirection * 10000.0f), ECC_GameTraceChannel1, CollisionParams);

			// Click-path diagnostics: a TRACE line sharing a click id with a BUTTON line means the click
			// was handled by BOTH the UI and the world (double-handling), not consumed by one of them.
			if (MobiusClickLog::IsEnabled())
			{
				MobiusClickLog::Log(TEXT("TRACE"), FString::Printf(
					TEXT("world line trace  hit=%s  actor=%s  component=%s"),
					bHit ? TEXT("YES") : TEXT("no"),
					bHit && OutHitResult.GetActor() ? *OutHitResult.GetActor()->GetName() : TEXT("-"),
					bHit && OutHitResult.GetComponent() ? *OutHitResult.GetComponent()->GetName() : TEXT("-")));
			}

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

void UMobiusControllerSubsystem::SelectPedestrianFromMousePosition()
{
	FHitResult HitResult;
	UCapsuleComponent* CapsuleComponent = nullptr;

	// Entry log: proves the Blueprint input action reached C++ for this click id, even when the trace misses.
	MobiusClickLog::Log(TEXT("TRACE"), TEXT("SelectPedestrianFromMousePosition (BP input action reached C++)"));

	LineTraceFromMousePosition(HitResult, CapsuleComponent);

	LastSelectedPedestrianCapsuleComponent = CapsuleComponent;
}

UCapsuleComponent* UMobiusControllerSubsystem::GetCapsuleComponent() const
{
	return LastSelectedPedestrianCapsuleComponent;
}

void UMobiusControllerSubsystem::GetRuntimeMeshBuilderFromWorld()
{
	// Get the runtime mesh generator actor and ignore it
	AActor* FoundActor = UGameplayStatics::GetActorOfClass(GetWorld(), ARuntimeMeshBuilder::StaticClass());
	if (auto CastedActor = Cast<ARuntimeMeshBuilder>(FoundActor))
	{
		RuntimeMeshGeneratorActor = CastedActor;
	}
	else
	{
		RuntimeMeshGeneratorActor = nullptr; // No runtime mesh generator actor found
	}
}
