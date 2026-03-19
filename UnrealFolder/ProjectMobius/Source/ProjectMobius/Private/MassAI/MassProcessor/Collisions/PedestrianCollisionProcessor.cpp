// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/MassProcessor/Collisions/PedestrianCollisionProcessor.h"

#include "MassExecutionContext.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Fragments/EntityTags/PedestrianCollisionTags.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "MassAI/Fragments/SharedFragments/RepresentationFragments/AgentRepresentationFragment.h"
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
	EntityQuery.AddTagRequirement<FPedestrianCollisionsEnabled>(EMassFragmentPresence::Any); // If any entities have tag, do process
	EntityQuery.AddTagRequirement<FPedestrianCollisionsDisabled>(EMassFragmentPresence::None);// When disabled we don't want to process collisions
	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);
}

void UPedestrianCollisionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	

	// log the processor execution
	//UE_LOG(LogTemp, Warning, TEXT("PedestrianCollisionProcessor::Execute"));
	
	
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context)
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

			// null check capsule and log
			if (EntityCollision.Capsule.Get() == nullptr)
			{
				UE_LOG(LogTemp, Warning, TEXT("PedestrianCollisionProcessor::Execute Entity %d Capsule is null"), Entity.Index);
				continue; // Skip this entity if the capsule is null
			}
			
			
			CurrentLocation.Z += EntityCollision.Capsule.Get()->GetScaledCapsuleHalfHeight(); // Adjust the Z offset for the capsule collision
			
			// update the collision fragment with the current location and rotation
			EntityCollision.Capsule.Get()->SetWorldLocation(CurrentLocation);

			//EntityCollision.Capsule.Get()->SetWorldRotation(CurrentEntityMovementFragment.CurrentRotation);
		}
	}));

	//TODO: below the query needs to be optimized so its scalable - current implementation is not scalable and is a hack together prototype
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context)
	{
		FHitResult HitResult;
		UCapsuleComponent* HoveredCapsuleComponent = nullptr;
		UCapsuleComponent* UserSelectedCapsuleComponent = nullptr;
	
		auto MobiusControllerSubsystem = Context.GetWorld()->GetSubsystem<UMobiusControllerSubsystem>();
	
		if (MobiusControllerSubsystem)
		{
			MobiusControllerSubsystem->LineTraceFromMousePosition(HitResult, HoveredCapsuleComponent);
			UserSelectedCapsuleComponent = MobiusControllerSubsystem->GetCapsuleComponent();
		}
		
		auto Entities = Context.GetEntities();
		const TArrayView<FEntityCollisionFragment>& EntityCollisions = Context.GetMutableFragmentView<FEntityCollisionFragment>();
		const TArrayView<FEntityRenderingFragment>& EntityRenderingFragments = Context.GetMutableFragmentView<FEntityRenderingFragment>();
		
		//TODO: We have two methods that rely on the agent subsystem we should assign it to a variable and use it
		for (int i = 0; i < Entities.Num(); i++)
		{
			auto Entity = Entities[i];

			//auto CurrentEntityMovementFragment = EntityMovementFragment[i];
			auto& EntityCollision = EntityCollisions[i];

			// if (HoveredCapsuleComponent != nullptr)
			// {
			// 	if (EntityCollision.Capsule.Get() == HoveredCapsuleComponent && UserSelectedCapsuleComponent == nullptr)
			// 	{
			// 		EntityRenderingFragments[i].showPedestrianStats = 1; // Show the stats for this entity
			//
			// 		// check if the entity has the tag, if not add it
			// 		if (!Context.DoesArchetypeHaveTag<FDisplayEntityDetailsTag>())
			// 		{
			// 			Context.Defer().AddTag<FDisplayEntityDetailsTag>(Entity); // Mark the collection with stats tag
			// 		}
			// 	}
			// 	else if (EntityCollision.Capsule.Get() == HoveredCapsuleComponent && UserSelectedCapsuleComponent != nullptr && UserSelectedCapsuleComponent != HoveredCapsuleComponent)
			// 	{
			// 		EntityRenderingFragments[i].showPedestrianStats = 1; // Show the stats for this entity
			//
			// 		// check if the entity has the tag, if not add it
			// 		if (!Context.DoesArchetypeHaveTag<FDisplayEntityDetailsTag>())
			// 		{
			// 			Context.Defer().AddTag<FDisplayEntityDetailsTag>(Entity); // Mark the collection with stats tag
			// 		}
			// 	}
			// 	else
			// 	{
			// 		EntityRenderingFragments[i].showPedestrianStats = 0; // Show the stats for this entity
			// 		// If no collisions were detected on this handle, we need to remove the tag from this context
			// 		if (Context.DoesArchetypeHaveTag<FDisplayEntityDetailsTag>())
			// 		{
			// 			Context.Defer().RemoveTag<FDisplayEntityDetailsTag>(Entity);
			// 		}
			// 	}
			// }
			// else if (UserSelectedCapsuleComponent != nullptr)
			// {
			// 	if (EntityCollision.Capsule.Get() == UserSelectedCapsuleComponent)
			// 	{
			// 		EntityRenderingFragments[i].showPedestrianStats = 2; // Show the stats for this entity
			//
			// 		// check if the entity has the tag, if not add it
			// 		if (!Context.DoesArchetypeHaveTag<FDisplayEntityDetailsTag>())
			// 		{
			// 			Context.Defer().AddTag<FDisplayEntityDetailsTag>(Entity); // Mark the collection with stats tag
			// 		}
			// 	}
			// 	else
			// 	{
			// 		EntityRenderingFragments[i].showPedestrianStats = 0; // Show the stats for this entity
			// 		// If no collisions were detected on this handle, we need to remove the tag from this context
			// 		if (Context.DoesArchetypeHaveTag<FDisplayEntityDetailsTag>())
			// 		{
			// 			Context.Defer().RemoveTag<FDisplayEntityDetailsTag>(Entity);
			// 		}
			// 	}
			// }
			// else
			// {
			//
			// 		EntityRenderingFragments[i].showPedestrianStats = 0; // Show the stats for this entity
			// 		// If no collisions were detected on this handle, we need to remove the tag from this context
			// 		if (Context.DoesArchetypeHaveTag<FDisplayEntityDetailsTag>())
			// 		{
			// 			Context.Defer().RemoveTag<FDisplayEntityDetailsTag>(Entity);
			// 		}
			// 	
			// }

			
			if (HoveredCapsuleComponent != nullptr && UserSelectedCapsuleComponent != nullptr)
			{
				if (EntityCollision.Capsule.Get() == HoveredCapsuleComponent &&
					EntityCollision.Capsule.Get() != UserSelectedCapsuleComponent)
				{
					EntityRenderingFragments[i].showPedestrianStats = 1; // Show the stats for this entity
			
					// check if the entity has the tag, if not add it
					if (!Context.DoesArchetypeHaveTag<FDisplayEntityDetailsTag>())
					{
						Context.Defer().AddTag<FDisplayEntityDetailsTag>(Entity); // Mark the collection with stats tag
					}
				}
				else if (EntityCollision.Capsule.Get() == UserSelectedCapsuleComponent)
				{
					EntityRenderingFragments[i].showPedestrianStats = 2; // Show the stats for this entity
			
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
			else if (UserSelectedCapsuleComponent != nullptr && HoveredCapsuleComponent == nullptr)
			{
				if (EntityCollision.Capsule.Get() == UserSelectedCapsuleComponent)
				{
					EntityRenderingFragments[i].showPedestrianStats = 2; // Show the stats for this entity
			
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
			else if (HoveredCapsuleComponent && UserSelectedCapsuleComponent == nullptr)
			{
				if (EntityCollision.Capsule.Get() == HoveredCapsuleComponent)//TODO: DOUBLE CLICK 
				{
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
			else // no hover or selected -> clear stats
			{
				EntityRenderingFragments[i].showPedestrianStats = 0; // Hide the stats for this entity
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
