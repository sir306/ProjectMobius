// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/SignalProcessors/FlowCounterSignalProcessor.h"
#include "MassSignalSubsystem.h"
#include "MassExecutionContext.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/SubSystems/PedestrianSignalSubsystem.h"
#include "MassAI/Tags/MassAITags.h"

UEnableFlowCounterSignalProcessor::UEnableFlowCounterSignalProcessor()
{
}

void UEnableFlowCounterSignalProcessor::Initialize(UObject& Owner)
{
	// Subscribe to the signals we want to handle in this processor
	UMassSignalSubsystem* SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());
	SubscribeToSignal(*SignalSubsystem, PedestrianDataSignals::Signals::ActivateFlowCounter);
	Super::Initialize(Owner);
}

void UEnableFlowCounterSignalProcessor::ConfigureQueries()
{
	// This Signal needs the entity info fragment, so we only check for entities that have this fragment
	EntityQuery.AddRequirement<FEntityInfoFragment>(EMassFragmentAccess::ReadOnly);

	EntityQuery.RegisterWithProcessor(*this);
}

void UEnableFlowCounterSignalProcessor::SignalEntities(FMassEntityManager& EntityManager,
	FMassExecutionContext& Context, FMassSignalNameLookup& EntitySignals)
{
	EntityQuery.ParallelForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& QueryContext)
	{
		const TConstArrayView<FMassEntityHandle> Entities = QueryContext.GetEntities();

		for (const FMassEntityHandle& Entity : Entities)
		{
			// Check that the the entity has the tag, if not add it
			if (!QueryContext.DoesArchetypeHaveTag<FMassFlowCounterTag>())
			{
				QueryContext.Defer().AddTag<FMassFlowCounterTag>(Entity);
			}
		};
	});

	// flush deferred
	Context.FlushDeferred();
}

UDisableFlowCounterSignalProcessor::UDisableFlowCounterSignalProcessor()
{
}

void UDisableFlowCounterSignalProcessor::Initialize(UObject& Owner)
{
	// Subscribe to the signals we want to handle in this processor
	UMassSignalSubsystem* SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());
	SubscribeToSignal(*SignalSubsystem, PedestrianDataSignals::Signals::DeactivateFlowCounter);
	Super::Initialize(Owner);
}

void UDisableFlowCounterSignalProcessor::ConfigureQueries()
{
	// As we are only removing a tag from the entities, we don't need to check for any specific fragments
	// only entities that have the Flow Counter Tag
	EntityQuery.AddTagRequirement<FMassFlowCounterTag>(EMassFragmentPresence::Any);
	
	EntityQuery.RegisterWithProcessor(*this);
}

void UDisableFlowCounterSignalProcessor::SignalEntities(FMassEntityManager& EntityManager,
	FMassExecutionContext& Context, FMassSignalNameLookup& EntitySignals)
{
	EntityQuery.ParallelForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& QueryContext)
	{
		const TConstArrayView<FMassEntityHandle> Entities = QueryContext.GetEntities();

		for (const FMassEntityHandle& Entity : Entities)
		{
			// Check that the the entity has the tag, if so remove it
			if (QueryContext.DoesArchetypeHaveTag<FMassFlowCounterTag>())
			{
				QueryContext.Defer().RemoveTag<FMassFlowCounterTag>(Entity);
			}
		};
	});

	// flush deferred
	Context.FlushDeferred();
}
