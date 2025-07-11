// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/MassProcessor/Collisions/PedestrianCollisionProcessor.h"

#include "MassExecutionContext.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "MassAI/Tags/MassAITags.h"
#include "Subsystems/StatisticSubsystem.h"

UPedestrianCollisionProcessor::UPedestrianCollisionProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
	bRequiresGameThreadExecution = true;
}

void UPedestrianCollisionProcessor::ConfigureQueries()
{
	// The required fragments for this processor
	EntityQuery.AddSharedRequirement<FSimulationFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);


	// The Entity Query Required fragments for this processor
	EntityQuery.AddRequirement<FEntityInfoFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FEntityMovementFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FEntityRenderingFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FEntityCollisionFragment>(EMassFragmentAccess::ReadWrite);

	/* Add subsystem requirements */
	// Representation subsystem
	EntityQuery.AddSubsystemRequirement<UStatisticSubsystem>(EMassFragmentAccess::ReadWrite);

	// Required Query Tags
	EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::None);
	//TODO: add tag for when we do toggle logic if we want to toggle the collision processor on and off

	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);
}

void UPedestrianCollisionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context)
	{
		auto Entities = Context.GetEntities();
		const TArrayView<FEntityCollisionFragment>& EntityCollisions = Context.GetMutableFragmentView<FEntityCollisionFragment>();
		const TArrayView<FEntityMovementFragment>& EntityMovementFragment = Context.GetMutableFragmentView<FEntityMovementFragment>();

		// get the mouse position in the world
		FVector MouseWorldPosition;
		FVector WorldDirection;
		bool bDeprojected = GetWorld()->GetFirstPlayerController()->DeprojectMousePositionToWorld(MouseWorldPosition, WorldDirection);
		
		// FVector2D ScreenPosition;
		// GetWorld()->GetFirstLocalPlayerFromController()->ViewportClient->GetMousePosition(ScreenPosition);
		// bDeprojected = UGameplayStatics::DeprojectScreenToWorld(GetWorld()->GetFirstPlayerController(), ScreenPosition, MouseWorldPosition, WorldDirection);
		
		//TODO: We have two methods that rely on the agent subsystem we should assign it to a variable and use it
		for (int i = 0; i < Entities.Num(); i++)
		{
			auto Entity = Entities[i];

			auto CurrentEntityMovementFragment = EntityMovementFragment[i];
			auto& EntityCollision = EntityCollisions[i];

			FVector CurrentLocation = CurrentEntityMovementFragment.CurrentLocation;
			CurrentLocation.Z += EntityCollision.Capsule.Get()->GetScaledCapsuleHalfHeight(); // Adjust the Z offset for the capsule collision

			// update the collision fragment with the current location and rotation
			EntityCollision.Capsule.Get()->SetWorldLocation(CurrentEntityMovementFragment.CurrentLocation);
			EntityCollision.Capsule.Get()->SetWorldRotation(CurrentEntityMovementFragment.CurrentRotation);


			// is it the current entity that is being clicked on or hovered over
			if (bDeprojected)//only able to perform a trace if the deprojection was successful
			{
				FHitResult HitResult;
				FCollisionQueryParams CollisionParams;
				CollisionParams.AddIgnoredActor(GetWorld()->GetFirstPlayerController()->GetPawn()); // Ignore the player pawn

				// Perform a line trace from the mouse position in the world
				bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, MouseWorldPosition, MouseWorldPosition + WorldDirection * 1000.0f, ECC_Visibility, CollisionParams);

				if (bHit && HitResult.GetActor() == EntityCollision.Capsule.Get()->GetOwner())
				{
					// If the capsule is hit, we can log or handle the collision
					UE_LOG(LogTemp, Warning, TEXT("Capsule hit: %s"), *HitResult.GetActor()->GetName());
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Deprojection failed"));
			}
		}
	}));
}
