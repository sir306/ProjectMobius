// Fill out your copyright notice in the Description page of Project Settings.


#include "FlowCounterProcessor.h"

#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "EnumsAndStructs/FlowCounterStructs.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Tags/MassAITags.h"
#include "Subsystems/StatisticSubsystem.h"
#include "Subsystems/TimeDilationSubSystem.h"

UFlowCounterProcessor::UFlowCounterProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Avoidance);
	bRequiresGameThreadExecution = true;
}

void UFlowCounterProcessor::ConfigureQueries()
{
	// This processor should only run on entities that have the Flow Counter Tag on the query
	EntityQuery.AddTagRequirement<FMassFlowCounterTag>(EMassFragmentPresence::Any);
	// for the flow counter we need the EntityMovementFragment to be able to check if an agent has crossed the flow counter line
	EntityQuery.AddRequirement<FEntityMovementFragment>(EMassFragmentAccess::ReadWrite);

	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);

	// The required subsystem for this processor -> set to read-write access as we will need to send data
	ProcessorRequirements.AddSubsystemRequirement<UStatisticSubsystem>(EMassFragmentAccess::ReadWrite);

	// time dialation subsystem needed for passing current sim time to flow counter
	ProcessorRequirements.AddSubsystemRequirement<UTimeDilationSubSystem>(EMassFragmentAccess::ReadOnly);
}

void UFlowCounterProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (StatisticSubsystem == nullptr)
	{
		// Get the StatisticSubsystem from the world
		StatisticSubsystem = ToObjectPtr(Context.GetWorld()->GetSubsystem<UStatisticSubsystem>());
		if (StatisticSubsystem == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("StatisticSubsystem is null! Cannot execute FlowCounterProcessor."));
			return;
		}
	}
	if (TimeDilationSubSystem == nullptr)
	{
		// Get the TimeDilationSubSystem from the world
		TimeDilationSubSystem = ToObjectPtr(Context.GetWorld()->GetSubsystem<UTimeDilationSubSystem>());
		if (TimeDilationSubSystem == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("TimeDilationSubSystem is null! Cannot execute FlowCounterProcessor."));
			return;
		}
	}

	//TODO: check if paused -> don't want to keep executing if not needed
	
	CurrentSimTime = TimeDilationSubSystem->GetCurrentSimTime();

	EntityQuery.ForEachEntityChunk(EntityManager, Context, ([this](FMassExecutionContext& QueryContext)
	{
		// Get the required fragments and entities from the query context
		const TConstArrayView<FEntityMovementFragment> EntityMovementFragment = QueryContext.GetFragmentView<FEntityMovementFragment>();
		const int32 NumEntities = QueryContext.GetEntities().Num();
		

		//auto FlowCounters = StatisticSubsystem->GetFlowCounters().Num();

		for (int32 i = 0; i < NumEntities; ++i)
		{
			// Get the current entity movement fragment
			const FEntityMovementFragment& MoveFrag = EntityMovementFragment[i];
			
			for (int32 j = 0; j < StatisticSubsystem->GetFlowCounters().Num(); ++j)
			{
				int32 AgentID = MoveFrag.EntityID;
				// check to see if in band and not already been processed
				if (!StatisticSubsystem->HasAgentBeenCountedInFlowCounter(AgentID, j) && StatisticSubsystem->IsAgentLocationInAFlowCounterBand(MoveFrag.CurrentLocation, j))
				{
					const FFlowCounterData MoveData(AgentID, MoveFrag.CurrentLocation, MoveFrag.LastUpdatedSimTime);
					StatisticSubsystem->SendDataToFlowCounter(MoveData, j);
				}
			}
		}
		
		// ParallelFor(Entities.Num(), [&](int32 j)
		// {
		// 	// Get the current entity movement fragment
		// 	const FEntityMovementFragment& MoveFrag = EntityMovementFragment[j];
		//
		// 	bool bInBand = StatisticSubsystem->IsAgentLocationInAFlowCounterBand(MoveFrag.CurrentLocation, FlowCounterID);
		// 	if (bInBand)
		// 	{
		// 		FlowDataQueues[i].ProduceItem(FFlowCounterData(MoveFrag.EntityID, MoveFrag.CurrentLocation));
		// 		
		// 	}
		// });
			
		
		
		
	}));
}
