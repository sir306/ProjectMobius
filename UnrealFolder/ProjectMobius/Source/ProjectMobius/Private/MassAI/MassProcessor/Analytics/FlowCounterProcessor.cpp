// Fill out your copyright notice in the Description page of Project Settings.


#include "FlowCounterProcessor.h"

#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "EnumsAndStructs/FlowCounterStructs.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/Tags/MassAITags.h"
#include "Subsystems/StatisticSubsystem.h"

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

	EntityQuery.ParallelForEachEntityChunk(EntityManager, Context, ([this](FMassExecutionContext& QueryContext)
	{
		// Get the required fragments and entities from the query context
		const TConstArrayView<FEntityMovementFragment> EntityMovementFragment = QueryContext.GetFragmentView<FEntityMovementFragment>();
		const TConstArrayView<FMassEntityHandle> Entities = QueryContext.GetEntities();

		// The potential data to send to the StatisticSubsystem
		UE::TConsumeAllMpmcQueue<FFlowCounterData> FlowDataQueue;

		ParallelFor(Entities.Num(), [&](int32 i)
		{
			// Get the current entity movement fragment
			const FEntityMovementFragment& MoveFrag = EntityMovementFragment[i];
			
			bool bInBand = StatisticSubsystem->IsAgentLocationInAFlowCounterBand(MoveFrag.CurrentLocation);
			if (bInBand)
			{
				FlowDataQueue.ProduceItem(FFlowCounterData(MoveFrag.EntityID, MoveFrag.CurrentLocation));
					
			}
		});
		StatisticSubsystem->SendDataToFlowCounter(FlowDataQueue, 0);
	}));
}
