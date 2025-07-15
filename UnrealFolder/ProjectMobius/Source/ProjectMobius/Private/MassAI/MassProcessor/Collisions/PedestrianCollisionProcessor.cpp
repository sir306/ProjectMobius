// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/MassProcessor/Collisions/PedestrianCollisionProcessor.h"

#include "MassExecutionContext.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Fragments/EntityTags/PedestrianCollisionTags.h"
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
	EntityQuery.AddTagRequirement<FPedestrianCollisionsEnabled>(EMassFragmentPresence::Any); // If any entities have tag, do process
	EntityQuery.AddTagRequirement<FPedestrianCollisionsDisabled>(EMassFragmentPresence::None);// When disabled we don't want to process collisions
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

	// log the processor execution
	UE_LOG(LogTemp, Warning, TEXT("PedestrianCollisionProcessor::Execute"));
	
	
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
			CurrentLocation.Z += EntityCollision.Capsule.Get()->GetScaledCapsuleHalfHeight(); // Adjust the Z offset for the capsule collision

			// update the collision fragment with the current location and rotation
			EntityCollision.Capsule.Get()->SetWorldLocation(CurrentLocation);
			EntityCollision.Capsule.Get()->SetWorldRotation(CurrentEntityMovementFragment.CurrentRotation);


			
		}
	}));

	bool bLineTraceHit = false;
	FHitResult HitResult;
	UCapsuleComponent* HoveredCapsuleComponent = nullptr;
	UCapsuleComponent* UserSelectedCapsuleComponent = nullptr;

	if (MobiusControllerSubsystem)
	{
		bLineTraceHit = MobiusControllerSubsystem->LineTraceFromMousePosition(HitResult, HoveredCapsuleComponent);
		UserSelectedCapsuleComponent = MobiusControllerSubsystem->GetCapsuleComponent();
	}

	// TODO: need to clear if not hovering over anyone, so we don't lock the last hovered capsule component but make sure to maintain the
	// // last selected capsule component if the user has selected one, we also want to set a flag to only update capsules
	// while trigger is active as the current implementation of collisions, is not scalable and won't perform well with large numbers
	// of entities and updates, need to refine this at a later date once other systems that are in demand are implemented,
	// the other item is to convert our ui for selected agent to be a screen widget so we can display stats for the selected agent,
	// in a manner that is more UX friendly and less intrusive to the user experience, this will also allow us to drop widget space updates and scaling for this particular widget
	
	// We do another EntityQuery for each chunk to check for collisions with updated capsule components locations, provided that our line trace is successful
	if (!bLineTraceHit && !HoveredCapsuleComponent && !UserSelectedCapsuleComponent) // all need to be false to return early -> TBD we should store selection changes instead of checking every time
	{
		// If no capsule components were hit, we can return early
		// This will prevent unnecessary processing and ensure we only process entities that are relevant to the current mouse position
		//UE_LOG(LogTemp, Warning, TEXT("No capsule components hit or not valid"));
		return;
	}
	
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this, HitResult, HoveredCapsuleComponent, UserSelectedCapsuleComponent](FMassExecutionContext& Context)
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

			//
			if (HoveredCapsuleComponent != nullptr && UserSelectedCapsuleComponent != nullptr)
			{
				if (EntityCollision.Capsule.Get() == HoveredCapsuleComponent ||
					EntityCollision.Capsule.Get() == UserSelectedCapsuleComponent)
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
			else if (UserSelectedCapsuleComponent != nullptr && HoveredCapsuleComponent == nullptr)
			{
				if (EntityCollision.Capsule.Get() == UserSelectedCapsuleComponent)
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
			else if (HoveredCapsuleComponent && UserSelectedCapsuleComponent == nullptr)
			{
				if (EntityCollision.Capsule.Get() == HoveredCapsuleComponent)
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
			else
			{
				// this method should never fire here as should always have a capsule component to check against
			}

			
		}

		Context.FlushDeferred();
	}));
}
