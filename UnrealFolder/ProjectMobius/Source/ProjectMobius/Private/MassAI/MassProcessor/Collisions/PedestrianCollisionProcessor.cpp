// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/MassProcessor/Collisions/PedestrianCollisionProcessor.h"

#include "MassExecutionContext.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentRepresenatationFragment.h"
#include "MassAI/Tags/MassAITags.h"
#include "Subsystems/MobiusControllerSubsystem.h"
#include "Subsystems/StatisticSubsystem.h"

class UMobiusControllerSubsystem;

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
	// We need to read and write to the StatisticSubsystem for collision statistics and widget displays
	EntityQuery.AddSubsystemRequirement<UStatisticSubsystem>(EMassFragmentAccess::ReadWrite);
	// We need to read the MobiusControllerSubsystem for mouse position and world direction
	ProcessorRequirements.AddSubsystemRequirement<UMobiusControllerSubsystem>(EMassFragmentAccess::ReadOnly);

	// Required Query Tags
	EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::None);
	//TODO: add tag for when we do toggle logic if we want to toggle the collision processor on and off
	EntityQuery.AddTagRequirement<FDisplayEntityDetailsTag>(EMassFragmentPresence::Optional);
	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);
}

void UPedestrianCollisionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{

	// get the mouse position in the world
	FVector MouseWorldPosition;
	FVector WorldDirection;
	bool bDeprojected = GetWorld()->GetFirstPlayerController()->DeprojectMousePositionToWorld(MouseWorldPosition, WorldDirection);

	auto MobiusControllerSubsystem = ExecutionContext.GetWorld()->GetSubsystem<UMobiusControllerSubsystem>();

	
	
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this, bDeprojected, WorldDirection, MouseWorldPosition](FMassExecutionContext& Context)
	{
		auto Entities = Context.GetEntities();
		const TArrayView<FEntityCollisionFragment>& EntityCollisions = Context.GetMutableFragmentView<FEntityCollisionFragment>();
		const TArrayView<FEntityMovementFragment>& EntityMovementFragment = Context.GetMutableFragmentView<FEntityMovementFragment>();

	
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
			EntityCollision.Capsule.Get()->SetWorldLocation(CurrentLocation);
			EntityCollision.Capsule.Get()->SetWorldRotation(CurrentEntityMovementFragment.CurrentRotation);


			
		}
	}));

	bool bLineTraceHit = false;
	FHitResult HitResult;
	UCapsuleComponent* CapsuleComponent = nullptr;

	if (MobiusControllerSubsystem)
	{
		bLineTraceHit = MobiusControllerSubsystem->LineTraceFromMousePosition(HitResult, CapsuleComponent);
	}
	
	// We do another EntityQuery for each chunk to check for collisions with updated capsule components locations, provided that our line trace is successful
	if (!bLineTraceHit)
	{
		return;
	}
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this, HitResult, CapsuleComponent](FMassExecutionContext& Context)
	{
		auto Entities = Context.GetEntities();
		const TArrayView<FEntityCollisionFragment>& EntityCollisions = Context.GetMutableFragmentView<FEntityCollisionFragment>();
		const TArrayView<FEntityRenderingFragment>& EntityRenderingFragments = Context.GetMutableFragmentView<FEntityRenderingFragment>();
		
		//TODO: We have two methods that rely on the agent subsystem we should assign it to a variable and use it
		for (int i = 0; i < Entities.Num(); i++)
		{
			auto Entity = Entities[i];

			//auto CurrentEntityMovementFragment = EntityMovementFragment[i];
			auto& EntityCollision = EntityCollisions[i];

			if (EntityCollision.Capsule.Get() == CapsuleComponent)
			{
				// Log the collision
				//UE_LOG(LogTemp, Warning, TEXT("Collision detected for Entity ID: %d"), Entity.Index);
				EntityRenderingFragments[i].showPedestrianStats = 1; // Show the stats for this entity

				// check if the entity has the tag, if not add it
				if (!Context.DoesArchetypeHaveTag<FDisplayEntityDetailsTag>())
				{
					Context.Defer().AddTag<FDisplayEntityDetailsTag>(Entity); // Mark the collection with stats tag
				}
			}
			else
			{
				EntityRenderingFragments[i].showPedestrianStats = 0; // Show the stats for this entity
				// If no collisions were detected on this handle, we need to remove the tag from this context
				if (Context.DoesArchetypeHaveTag<FDisplayEntityDetailsTag>())
				{
					Context.Defer().RemoveTag<FDisplayEntityDetailsTag>(Entity);
				}
			}
		}

		Context.FlushDeferred();
	}));
}
