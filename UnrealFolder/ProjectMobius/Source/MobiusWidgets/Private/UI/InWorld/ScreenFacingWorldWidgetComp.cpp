// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/InWorld/ScreenFacingWorldWidgetComp.h"

#include "Camera/CameraComponent.h"


// Sets default values for this component's properties
UScreenFacingWorldWidgetComp::UScreenFacingWorldWidgetComp()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void UScreenFacingWorldWidgetComp::BeginPlay()
{
	Super::BeginPlay();

	// Find the camera component from the controller
	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* PlayerPawn = PlayerController->GetPawn())
		{
			CameraPtr = PlayerPawn->FindComponentByClass<UCameraComponent>();
			if (CameraPtr == nullptr)
			{
				UE_LOG(LogTemp, Warning, TEXT("ScreenFacingWorldWidgetComp: No CameraComponent found on PlayerPawn."));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ScreenFacingWorldWidgetComp: No Pawn found for PlayerController."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ScreenFacingWorldWidgetComp: No PlayerController found."));
	}

	// if we have a valid camera, set up a timer to update the widget rotation at the specified rate
	if (CameraPtr)
	{
		GetWorld()->GetTimerManager().SetTimer(UpdateWidgetRotationTimerHandle, this,
		                                       &UScreenFacingWorldWidgetComp::UpdateWidgetToFaceCamera, UpdateRate, true);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ScreenFacingWorldWidgetComp: CameraPtr is null, cannot set timer to update widget rotation."));
	}
	
}


void UScreenFacingWorldWidgetComp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UpdateWidgetRotationTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}


// Called every frame
void UScreenFacingWorldWidgetComp::TickComponent(float DeltaTime, ELevelTick TickType,
                                                 FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

void UScreenFacingWorldWidgetComp::UpdateWidgetToFaceCamera()
{
	// get the new rotation to face the camera
	const FRotator NewRotation = GetRotationToFaceCamera();

	// Update the widget component's rotation to face the camera
	SetWorldRotation(NewRotation);
}

FRotator UScreenFacingWorldWidgetComp::GetRotationToFaceCamera() const
{
	FRotator RotatorToReturn = FRotator(0.0f, 0.0f, 0.0f);

	// Check if we have a valid camera
	if (CameraPtr != nullptr)
	{
		// Get the location of the widget component and the camera
		const FVector WidgetLocation = GetComponentLocation();
		const FVector CameraLocation = CameraPtr->GetComponentLocation();
		
		// Calculate the direction vector from the widget to the camera
		const FVector DirectionToCamera = (CameraLocation - WidgetLocation).GetSafeNormal();

		// Calculate the rotation that would make the widget face the camera
		RotatorToReturn = DirectionToCamera.Rotation();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ScreenFacingWorldWidgetComp: CameraPtr is null."));
	}

	return RotatorToReturn;
}
