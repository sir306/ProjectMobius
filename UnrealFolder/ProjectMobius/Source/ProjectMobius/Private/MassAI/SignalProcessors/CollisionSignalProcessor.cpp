// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/SignalProcessors/CollisionSignalProcessor.h"
#include "MassSignalSubsystem.h"
#include "MassExecutionContext.h"
#include "MassAI/Fragments/EntityTags/PedestrianCollisionTags.h"
#include "MassAI/SubSystems/PedestrianSignalSubsystem.h"

UCollisionSignalProcessor::UCollisionSignalProcessor()
{
}

void UCollisionSignalProcessor::Initialize(UObject& Owner)
{
	// Subscribe to the signals we want to handle in this processor
	UMassSignalSubsystem* SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());
	SubscribeToSignal(*SignalSubsystem, PedestrianDataSignals::Signals::ActivateCollisions);
	SubscribeToSignal(*SignalSubsystem, PedestrianDataSignals::Signals::DeactivateCollisions);
	Super::Initialize(Owner);
}

void UCollisionSignalProcessor::ConfigureQueries()
{
	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);
}
//TODO: will want to break this into two different signal processors, one for enabling collisions and one for disabling collisions -> this way the disable logic can be more complex and handle the disable logic better
void UCollisionSignalProcessor::SignalEntities(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
                                               FMassSignalNameLookup& EntitySignals)
{
	EntityQuery.ParallelForEachEntityChunk(EntityManager, Context, [this, &EntitySignals](FMassExecutionContext& ExecutionContext)
	{
		const TConstArrayView<FMassEntityHandle> Entities = ExecutionContext.GetEntities();

		for (const FMassEntityHandle& Entity : Entities)
		{
			// check if the entity has the tag, if not add it
			if (!ExecutionContext.DoesArchetypeHaveTag<FPedestrianCollisionsDisabled>())
			{
				ExecutionContext.Defer().RemoveTag<FPedestrianCollisionsDisabled>(Entity); // remove the disable tag
				ExecutionContext.Defer().AddTag<FPedestrianCollisionsEnabled>(Entity); // add the enable tag
			}
			else
			{
				ExecutionContext.Defer().RemoveTag<FPedestrianCollisionsEnabled>(Entity); // remove the enable tag
				ExecutionContext.Defer().AddTag<FPedestrianCollisionsDisabled>(Entity); // add the disable tag
			}
		}
		
	});

	// flush deferred
	Context.FlushDeferred();
}
