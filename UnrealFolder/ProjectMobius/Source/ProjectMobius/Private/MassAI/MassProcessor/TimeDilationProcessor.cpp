// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.  
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,  
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL  
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR  
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING  
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS  
 * IN THE SOFTWARE.
 */

#include "MassAI/MassProcessor/TimeDilationProcessor.h"
// must includes
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassExternalSubsystemTraits.h" // This is needed so we can use subsystems and have no compile errors
// fragments we need
#include "MassAI/Fragments/SimulationTimestepFragment.h" // Shared Fragment holding curent time step
// extras needed for logic
#include "Kismet/GameplayStatics.h" // to get realtime seconds
// tags we need
#include "MassAI/Tags/MassAITags.h"
#include <SubSystems/TimeDilationSubSystem.h>

UTimeDilationProcessor::UTimeDilationProcessor()
{
	// Set the processor to auto register with the processing phases
	bAutoRegisterWithProcessingPhases = true;
	// Set the processor to execute on all (client and server) processors
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	// What phase to execute this processor in
	ProcessingPhase = EMassProcessingPhase::PrePhysics;

	// Set the order of execution for this processor
	// Before Avoidance
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Avoidance);
	
	// This processor should not be multithreaded as it is required to run on the game thread for
	// subsystem requirements on the processor
	bRequiresGameThreadExecution = true;
}

void UTimeDilationProcessor::ConfigureQueries()
{
	// Add the shared FSimulationTimeStepFragment requirement and set access to read write
	EntityQuery.AddSharedRequirement<FSimulationTimeStepFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	
	// Add Tag requirements
	EntityQuery.AddTagRequirement<FMassAITimeStepTag>(EMassFragmentPresence::All);

	// Add subsystem requirements
	EntityQuery.AddSubsystemRequirement<UTimeDilationSubSystem>(EMassFragmentAccess::ReadOnly);

	// register the query with the processor
	EntityQuery.RegisterWithProcessor(*this);

	// Register requirements for the processor
	ProcessorRequirements.AddSubsystemRequirement<UTimeDilationSubSystem>(EMassFragmentAccess::ReadOnly);
}

void UTimeDilationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context) {

		// // Get the TimeDilationSubSystem
		// const UTimeDilationSubSystem* TimeDilationSubSystem = Context.GetMutableSubsystem<UTimeDilationSubSystem>();
		//
		// // Get the shared fragment
		// auto& SimTimeStepFragment = Context.GetMutableSharedFragment<FSimulationTimeStepFragment>();
		//
		// // Update the SimTimeStepFragment current time step
		// SimTimeStepFragment.CurrentTimeStep = TimeDilationSubSystem->GetCurrentTimeStep();
		//
		// // log the current time step
		// //UE_LOG(LogTemp, Warning, TEXT("Current Time Step: %d"), SimTimeStepFragment.CurrentTimeStep);
		//
		// // Get the current simulation time 
		// float CurrentSimulationTime = TimeDilationSubSystem->GetCurrentSimTime();

		//UE_LOG(LogTemp, Warning, TEXT("Current Time Step: %d"), TimeStepFragment.CurrentTimeStep);

		

		//DEBUG
		// float Hours = FMath::FloorToInt(CurrentSimulationTime / 3600);
		// float Minutes = FMath::FloorToInt(fmod(CurrentSimulationTime, 3600) / 60);
		// float Seconds = fmod(CurrentSimulationTime, 60);
		//float Milliseconds = fmod(CurrentSimulationTime, 1) * 1000;

		// UE LOG Sim Time
		//UE_LOG(LogTemp, Warning, TEXT("Simulation Time: %02d:%02d:%02d"), (int32)Hours, (int32)Minutes, (int32)Seconds);
	}));
	
}
