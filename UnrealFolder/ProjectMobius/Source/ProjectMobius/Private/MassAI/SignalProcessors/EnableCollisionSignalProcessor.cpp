// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/SignalProcessors/EnableCollisionSignalProcessor.h"
#include "MassSignalSubsystem.h"
#include "MassExecutionContext.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Fragments/EntityTags/PedestrianCollisionTags.h"
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentRepresenatationFragment.h"
#include "MassAI/SubSystems/PedestrianSignalSubsystem.h"
#include "Subsystems/MobiusControllerSubsystem.h"


UEnableCollisionSignalProcessor::UEnableCollisionSignalProcessor()
{
}

void UEnableCollisionSignalProcessor::Initialize(UObject& Owner)
{
	// Subscribe to the signals we want to handle in this processor
	UMassSignalSubsystem* SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());
	SubscribeToSignal(*SignalSubsystem, PedestrianDataSignals::Signals::ActivateCollisions);
	Super::Initialize(Owner);
}

void UEnableCollisionSignalProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FEntityCollisionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FDisplayEntityDetailsTag>(EMassFragmentPresence::Optional);
	
	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);
}
//TODO: will want to break this into two different signal processors, one for enabling collisions and one for disabling collisions -> this way the disable logic can be more complex and handle the disable logic better
void UEnableCollisionSignalProcessor::SignalEntities(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
                                               FMassSignalNameLookup& EntitySignals)
{
	EntityQuery.ParallelForEachEntityChunk(EntityManager, Context, [this, &EntitySignals](FMassExecutionContext& ExecutionContext)
	{
		const TConstArrayView<FMassEntityHandle> Entities = ExecutionContext.GetEntities();

		for (const FMassEntityHandle& Entity : Entities)
		{
			// check if the entity has the tag, if not add it
			if (ExecutionContext.DoesArchetypeHaveTag<FPedestrianCollisionsDisabled>())
			{
				ExecutionContext.Defer().RemoveTag<FPedestrianCollisionsDisabled>(Entity); // remove the disable tag
				ExecutionContext.Defer().AddTag<FPedestrianCollisionsEnabled>(Entity); // add the enable tag
			}
		}
		
	});

	// flush deferred
	Context.FlushDeferred();
}

UDisableCollisionSignalProcessor::UDisableCollisionSignalProcessor()
{
}

void UDisableCollisionSignalProcessor::Initialize(UObject& Owner)
{
	// Subscribe to the signals we want to handle in this processor
	UMassSignalSubsystem* SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());
	SubscribeToSignal(*SignalSubsystem, PedestrianDataSignals::Signals::ActivateCollisions);
	SubscribeToSignal(*SignalSubsystem, PedestrianDataSignals::Signals::DeactivateCollisions);
	Super::Initialize(Owner);
}

void UDisableCollisionSignalProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FEntityCollisionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FEntityRenderingFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FDisplayEntityDetailsTag>(EMassFragmentPresence::Optional);
	
	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);
}

void UDisableCollisionSignalProcessor::SignalEntities(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
	FMassSignalNameLookup& EntitySignals)
{
	// Get the user selected capsule component from the controller subsystem
	UMobiusControllerSubsystem* MobiusControllerSubsystem = Context.GetWorld()->GetSubsystem<UMobiusControllerSubsystem>();
	UCapsuleComponent* UserSelectedCapsuleComponent = MobiusControllerSubsystem->GetCapsuleComponent();
	
	EntityQuery.ParallelForEachEntityChunk(EntityManager, Context, [this, UserSelectedCapsuleComponent](FMassExecutionContext& ExecutionContext)
	{
		const TConstArrayView<FMassEntityHandle> Entities = ExecutionContext.GetEntities();

		for (int i = 0; i < Entities.Num(); i++)
		{
			auto Entity = Entities[i];
			auto & EntityCollision = ExecutionContext.GetMutableFragmentView<FEntityCollisionFragment>()[i];
			// check if the entity has the tag, if not add it
			if (ExecutionContext.DoesArchetypeHaveTag<FPedestrianCollisionsEnabled>())
			{
				ExecutionContext.Defer().RemoveTag<FPedestrianCollisionsEnabled>(Entity); // remove the enable tag
				ExecutionContext.Defer().AddTag<FPedestrianCollisionsDisabled>(Entity); // add the disable tag
				
			}
			
			if (UserSelectedCapsuleComponent && EntityCollision.Capsule.Get() == UserSelectedCapsuleComponent)
			{
				// We may want to keep this capsule active and updating throughout the simulation ?? TBD
			}
			else
			{
				// Ensure the show stats is set to 0 and remove the details tag if it exists
				auto& EntityRendering = ExecutionContext.GetMutableFragmentView<FEntityRenderingFragment>()[i];
				EntityRendering.showPedestrianStats = 0; // Hide the stats for this entity
				if (ExecutionContext.DoesArchetypeHaveTag<FDisplayEntityDetailsTag>())
				{
					ExecutionContext.Defer().RemoveTag<FDisplayEntityDetailsTag>(Entity); // Remove the details tag
				}
			}
	
		}
		
	});

	// flush deferred
	Context.FlushDeferred();
}
