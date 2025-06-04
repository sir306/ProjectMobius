// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/MassProcessor/PedestrianMovementProcessor.h"
// Required headers for processing entities, Subsystems and there fragments
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassExternalSubsystemTraits.h" // This is needed so we can use subsystems and have no compile errors
// Fragments to include with this processor
#include "MassAI/Fragments/EntityInfoFragment.h"
// Shared Fragments to include with the processor
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
// Subsystems to include with the processor
#include "MassAI/Subsystems/TimeDilationSubSystem.h"
#include <MassAI/Tags/MassAITags.h>
// multithreading and async
#include "HeatmapSubsystem.h"
#include "MassEntityView.h"
#include "Async/ParallelFor.h"
#include "HAL/CriticalSection.h"
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "MassAI/SubSystems/MassEntitySpawnSubsystem.h"

UPedestrianMovementProcessor::UPedestrianMovementProcessor():
	TimeDilationSubSystem(nullptr)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Avoidance);
	
}

void UPedestrianMovementProcessor::ConfigureQueries()
{
	// The required fragments for this processor
	EntityQuery.AddSharedRequirement<FSimulationFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::All);

	// The Entity Query Required fragments for this processor
	EntityQuery.AddRequirement<FEntityInfoFragment>(EMassFragmentAccess::ReadWrite);
	// Required Query Tags
	EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::Optional);

	// Register the entity query with the processor
	EntityQuery.RegisterWithProcessor(*this);

	// Register requirements for the processor
	ProcessorRequirements.AddSubsystemRequirement<UTimeDilationSubSystem>(EMassFragmentAccess::ReadOnly);

	//
}

void UPedestrianMovementProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	// Check if the subsystems are setup
	if(!bAreSubSystemsSetup)
	{
		SetupSubSystems(ExecutionContext);

		// to stop the function from executing if the subsystem is not valid we return here to prevent crashes
		if(TimeDilationSubSystem == nullptr)
		{
			return;
		}
	}
	else // if the subsystems are setup then we update the current time step
	{
		UpdateCurrentTimeStepAndStepPercentage();
	}

	//EntityManager.BatchCreateEntities()

	// Store the number of entities
	//OffsetIndex = 0;
	{
		// int32 MovementSize = 0;
		//
		// EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this, &MovementSize](FMassExecutionContext& Context) {
		// 	{
		// 		// Check if there is data to process and stop if there is none
		// 		if(!IsThereDataToProcess(Context)) {return;}
		// 		
		// 		// This has to plus current value as data size changes per chunk
		// 		OffsetIndex += Context.GetNumEntities();
		//
		// 		// This is equal to the size of the data as it is shared across all entities
		// 		MovementSize = Context.GetSharedFragment<FSimulationFragment>().SimulationData[CurrentTimeStep].Num();
		// 		
		// 		if(MovementSize == 998)
		// 		{
		// 			UE_LOG(LogTemp, Warning, TEXT("MovementSize: %d"), MovementSize);
		// 			UE_LOG(LogTemp, Warning, TEXT("OffsetIndex: %d"), OffsetIndex);
		// 			UE_LOG(LogTemp, Warning, TEXT("GetNum: %d"), Context.GetNumEntities());
		// 			
		// 		}
		// 	}
		// }));
		//
		//
		//
		// if (OffsetIndex < MovementSize)
		// {
		// 	// log they are equal
		// 	UE_LOG(LogTemp, Warning, TEXT("NumEntities < MovementSize need to spawn more entities"));
		//
		// 	// log number of entities and movement size
		// 	UE_LOG(LogTemp, Warning, TEXT("NumEntities: %d MovementSize: %d"), OffsetIndex, MovementSize);
		//
		// 	// Number to spawn
		// 	int32 NumToSpawn = MovementSize - OffsetIndex;
		//
		// 	auto SpawnSystem = GetWorld()->GetSubsystem<UMassEntitySpawnSubsystem>();
		//
		// 	// log SpawnSystem->SpawnedEntityPedestrianHandles.Num()
		// 	UE_LOG(LogTemp, Warning, TEXT("SpawnSystem->SpawnedEntityPedestrianHandles.Num(): %d"), SpawnSystem->SpawnedEntityPedestrianHandles.Num());
		//
		// 	// Spawning can only happen on the game thread so a quick workaround is do async task
		// 	AsyncTask(ENamedThreads::GameThread, [this, SpawnSystem, NumToSpawn]()
		// 	{
		// 		SpawnSystem->SpawnMassEntityPedestrians(NumToSpawn);
		// 	});
		//
		// 	// return as we don't want to execute the rest of the function
		// 	return;
		// }
	}
	
	EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context) {
		{
			//TODO: Move data to subsystem and get it from there so not constantly getting large data sets
	
			// Get the shared fragment
			//auto SharedAgentMovement = Context.GetSharedFragment<FSimulationFragment>(); // this is killing the fps
			
			// Check if there is data to process and stop if there is none
			if(!IsThereDataToProcess(Context))
			{
				// Entity Info Fragment
				const TArrayView<FEntityInfoFragment> EntityInfoFragment = Context.GetMutableFragmentView<FEntityInfoFragment>();
				
				ParallelFor(EntityInfoFragment.Num(), [&](int32 i)
				{
					EntityInfoFragment[i].bRenderAgent = false;
				});
				
				return;
			}
	
			// Get the required fragments
			// Entity Info Fragment
			const TArrayView<FEntityInfoFragment> EntityInfoFragment = Context.GetMutableFragmentView<FEntityInfoFragment>();
	
			// get the mapped value by specified key
			const TArray<FSimMovementSample> CurrentMovementSamples = Context.GetSharedFragment<FSimulationFragment>().SimulationData[CurrentTimeStep];
			// TODO: ^^PROBLEM HERE movement bracket is not being updated correctly
			// the future movement samples if there is none it will be the same as the current movement samples
			TArray<FSimMovementSample> NextMovementSamples;
			bool bSamplesTheSame = false;

			if (Context.GetSharedFragment<FSimulationFragment>().SimulationData.Find(CurrentTimeStep + 1) != nullptr)
			{
				NextMovementSamples = Context.GetSharedFragment<FSimulationFragment>().SimulationData[CurrentTimeStep + 1];
				bSamplesTheSame = false;
			}
			else
			{
				bSamplesTheSame = true;
			}

			// clear old data
			EntityIDToCurrentMovementSampleIndexMap.Reset();
			EntityIDToNextMovementSampleIndexMap.Reset();
			
			// Reserve the size of the array to avoid reallocating memory
			// TODO: this is horrible and needs to be fixed for the data reserve it needs to be the max num of agents and we need to store this for better handling
			EntityIDToCurrentMovementSampleIndexMap.Reserve(CurrentMovementSamples.Num());
			
			// Create the lookup table for the current movement samples
			for (int32 Index = 0; Index < CurrentMovementSamples.Num(); ++Index)
			{
				EntityIDToCurrentMovementSampleIndexMap.Add(CurrentMovementSamples[Index].EntityID, Index);
			}

			if (!bSamplesTheSame)
			{
				// Reserve the size of the array to avoid reallocating memory
				EntityIDToNextMovementSampleIndexMap.Reserve(NextMovementSamples.Num());
				
				for (int32 iter = 0; iter < NextMovementSamples.Num(); ++iter)
				{
					const FSimMovementSample& NextSample = NextMovementSamples[iter];
					EntityIDToNextMovementSampleIndexMap.Add(NextSample.EntityID, iter);
				}
			}

			// loop through the entity info fragment and update the location and rotation
			ParallelFor(EntityInfoFragment.Num(), [&](int32 i)
			{
				EntityInfoFragment[i].bRenderAgent = false;
				int32 LookupIndex = EntityInfoFragment[i].EntityID;
				// if current and next movement samples are the same we dont need to interpolate or look at both
				if (bSamplesTheSame)
				{
					// find if this entity is in current lookup table
					if (EntityIDToCurrentMovementSampleIndexMap.Contains(LookupIndex))
					{
						// Get the value from the lookup table
						int32 LookupVal = EntityIDToCurrentMovementSampleIndexMap[LookupIndex];
						
						// if it is then we can update the location and rotation
						EntityInfoFragment[i].CurrentLocation = CurrentMovementSamples[LookupVal].Position;
						EntityInfoFragment[i].CurrentRotation = CurrentMovementSamples[LookupVal].Rotation;
						EntityInfoFragment[i].bRenderAgent = true;
						EntityInfoFragment[i].bReadyToDestroy = false;
						EntityInfoFragment[i].CurrentSpeed = CurrentMovementSamples[LookupVal].Speed;
						// did the animation bracket change
						EntityInfoFragment[i].bAnimationChanged = static_cast<EPedestrianMovementBracket>(EntityInfoFragment[i].CurrentMovementBracket) != static_cast<EPedestrianMovementBracket>(CurrentMovementSamples[LookupVal].MovementBracket);
							
						EntityInfoFragment[i].CurrentMovementBracket = CurrentMovementSamples[LookupVal].MovementBracket;
					}
					else
					{
						EntityInfoFragment[i].bRenderAgent = false;
						EntityInfoFragment[i].bReadyToDestroy = true;
					}
				}
				else
				{
					// find if this entity is in current lookup table
					if (EntityIDToCurrentMovementSampleIndexMap.Contains(EntityInfoFragment[i].EntityID))
					{
						// Found a match in current see if it is in next
						if (EntityIDToNextMovementSampleIndexMap.Contains(EntityInfoFragment[i].EntityID))
						{
							// found a match in next so interpolate
							int32 CurrentLookupVal = EntityIDToCurrentMovementSampleIndexMap[EntityInfoFragment[i].EntityID];
							int32 NextLookupVal = EntityIDToNextMovementSampleIndexMap[EntityInfoFragment[i].EntityID];
							// Get the samples // TODO: Better to not store index and use once just while debugging
							const FSimMovementSample& CurrentMovementSample = CurrentMovementSamples[CurrentLookupVal];
							const FSimMovementSample& NextMovementSample = NextMovementSamples[NextLookupVal];

							FVector StartLocation = CurrentMovementSample.Position;
							FVector EndLocation = NextMovementSample.Position;
							FRotator StartRotation = CurrentMovementSample.Rotation;
							FRotator EndRotation = NextMovementSample.Rotation;

							TPair<FVector, FRotator> InterpolatedValues = LinearInterpolate(StartLocation, EndLocation, StartRotation, EndRotation);
							UpdateEntityInfoFragment(EntityInfoFragment[i], InterpolatedValues.Key, InterpolatedValues.Value, true);
							EntityInfoFragment[i].bReadyToDestroy = false;

							// Update the current speed
							EntityInfoFragment[i].CurrentSpeed = FMath::Lerp(CurrentMovementSample.Speed, NextMovementSample.Speed, TimeStepPercentage);

							// did the animation bracket change
							EntityInfoFragment[i].bAnimationChanged = static_cast<EPedestrianMovementBracket>(EntityInfoFragment[i].CurrentMovementBracket) != static_cast<EPedestrianMovementBracket>(NextMovementSample.MovementBracket);

							// Update the anim bracket to be the current one
							EntityInfoFragment[i].CurrentMovementBracket = CurrentMovementSample.MovementBracket;
						}
						else
						{
							// Get the value from the lookup table
							int32 LookupVal = EntityIDToCurrentMovementSampleIndexMap[LookupIndex];
						
							// if it is then we can update the location and rotation
							EntityInfoFragment[i].CurrentLocation = CurrentMovementSamples[LookupVal].Position;
							EntityInfoFragment[i].CurrentRotation = CurrentMovementSamples[LookupVal].Rotation;
							EntityInfoFragment[i].bRenderAgent = true;
							EntityInfoFragment[i].bReadyToDestroy = false;
							EntityInfoFragment[i].CurrentSpeed = CurrentMovementSamples[LookupVal].Speed;
							// did the animation bracket change
							EntityInfoFragment[i].bAnimationChanged = static_cast<EPedestrianMovementBracket>(EntityInfoFragment[i].CurrentMovementBracket) != static_cast<EPedestrianMovementBracket>(CurrentMovementSamples[LookupVal].MovementBracket);
							
							EntityInfoFragment[i].CurrentMovementBracket = CurrentMovementSamples[LookupVal].MovementBracket;
						}
						
						
					}
					else
					{
						EntityInfoFragment[i].bRenderAgent = false;
						EntityInfoFragment[i].bReadyToDestroy = true;
					}
				}
			});
			

			
			{// TArray<int32> EntitiesToDestroy;
				//
				// for(int32 i = 0; i < EntityInfoFragment.Num(); i++)
				// {
				// 	if(EntityInfoFragment[i].bReadyToDestroy)
				// 	{
				// 		EntitiesToDestroy.Add(i);
				// 	}
				// }
				//
				// // if entities to destroy value is greater than 0 log
				// if(EntitiesToDestroy.Num() > 0)
				// {
				// 	UE_LOG(LogTemp, Warning, TEXT("EntitiesToDestroy: %d"), EntitiesToDestroy.Num());
				// }
				//
				// for(int32 i : EntitiesToDestroy)
				// {
				// 	FMassEntityHandle EntityHandle = Context.GetEntity(i);
				// 	Context.Defer().DestroyEntity(EntityHandle);
				// }
			}
			
			// ParallelFor(AllMovementSamples.Num(), [&](int32 i)
			// {
			// 	// Get the movement sample
			// 	FSimMovementSample MovementSample = AllMovementSamples[i];
			// 	
			// 	//CriticalSection.Lock();
			// 	if (EntityInfoFragment.IsValidIndex(MovementSample.EntityID))
			// 	{
			// 		EntityInfoFragment[MovementSample.EntityID].CurrentLocation = MovementSample.Position * 10;
			// 		EntityInfoFragment[MovementSample.EntityID].CurrentRotation = MovementSample.Rotation;
			// 		EntityInfoFragment[MovementSample.EntityID].bRenderAgent = true;
			// 		//UpdateEntityInfoFragment(EntityInfoFragment[MovementSample.EntityID], MovementSample.Position, MovementSample.Rotation, true);
			// 	}
			// 	//CriticalSection.Unlock();
			// });
		}
	}));
	//TODO: ****** when destroying respawning back this should only be called when needed not every execute ******
	//ExecutionContext.FlushDeferred();
}

void UPedestrianMovementProcessor::RenderEntityInfoFragment(FEntityInfoFragment& EntityInfoFragToUpdate, bool bNewRenderStatus)
{
	EntityInfoFragToUpdate.bRenderAgent = bNewRenderStatus;
}

void UPedestrianMovementProcessor::UpdateEntityInfoFragment(FEntityInfoFragment& EntityInfoFragToUpdate, const FVector& NewLocation, const FRotator& NewRotation, const bool bNewRenderStatus)
{
	EntityInfoFragToUpdate.CurrentLocation = NewLocation; // this value needs to be come a variable for scaling and converting feet to cm, meters, etc.
	EntityInfoFragToUpdate.CurrentRotation = NewRotation;
	RenderEntityInfoFragment(EntityInfoFragToUpdate, bNewRenderStatus);
}

bool UPedestrianMovementProcessor::DoesMovementAndEntityIDMatch(const int32 MovementDataID, const int32 EntityInfoID)
{
	if(MovementDataID == EntityInfoID)
	{
		return true;
	}
	else
	{
		return false;
	}
}

void UPedestrianMovementProcessor::GetEntityLocationAndRotation(const FEntityInfoFragment& Entity,
                                                                FVector& OutStartLocation, FVector& OutEndLocation, FRotator& OutStartRotation, FRotator& OutEndRotation) const
{
	
}

TPair<FVector, FRotator> UPedestrianMovementProcessor::LinearInterpolate(const FVector& StartLocation,
                                                                         const FVector& EndLocation, const FRotator& StartRotation, const FRotator& EndRotation) const
{
	// Get the new location
	FVector NewLocation = FMath::Lerp(StartLocation, EndLocation, TimeStepPercentage);
	// Get the new rotation
	FRotator NewRotation = FMath::Lerp(StartRotation, EndRotation, TimeStepPercentage);
	// Return the new location and rotation
	return TPair<FVector, FRotator>(NewLocation, NewRotation);
}

bool UPedestrianMovementProcessor::IsThereDataToProcess(const FMassExecutionContext& ExecutionContext) const
{
	// TODO: This one should be at the start to only check once per call not per loop iteration per call
	// Check if the shared fragment is empty or not need more methodology to handle this and not check every time executed
	if (ExecutionContext.GetSharedFragment<FSimulationFragment>().SimulationData.IsEmpty())
	{
		return false;
	}
	
	if (CurrentTimeStep >= ExecutionContext.GetSharedFragment<FSimulationFragment>().SimulationData.Num())
	{
		return false;
	}
	
	if (ExecutionContext.GetSharedFragment<FSimulationFragment>().SimulationData[CurrentTimeStep].IsEmpty())
	{
		return false;
	}
	if(ExecutionContext.GetNumEntities() == 0)
	{
		return false;
	}
	return true;
}

void UPedestrianMovementProcessor::SetupSubSystems(FMassExecutionContext& ExecutionContext)
{
	// The TimeDilationSubSystem should not be allocated if the world is not valid
	if(!ExecutionContext.GetWorld())
	{
		return;
	}

	// Check if the TimeDilation subsystem is nullptr
	if(TimeDilationSubSystem == nullptr)
	{
		// Get the TimeDilationSubSystem
		TimeDilationSubSystem = ExecutionContext.GetWorld()->GetSubsystem<UTimeDilationSubSystem>();
	}
	
	// check that the TimeDilationSubSystem was allocated and update the current time step value
	if(TimeDilationSubSystem != nullptr)
	{
		// Get the current time step
		SetCurrentTimeStep(TimeDilationSubSystem->GetCurrentTimeStep());

		// Update flag to true so we don't check again
		bAreSubSystemsSetup = true;
	}
}

void UPedestrianMovementProcessor::UpdateCurrentTimeStepAndStepPercentage()
{
	if(TimeDilationSubSystem != nullptr)
	{
		// Get the current time step
		SetCurrentTimeStep(TimeDilationSubSystem->GetCurrentTimeStep());

		// update the time step percentage
		TimeStepPercentage = TimeDilationSubSystem->GetCurrentTimeStepPercentage();
	}
	else
	{
		// if the subsystem is nullptr then set the flag to false, this way it will try to get the subsystem again
		bAreSubSystemsSetup = false;
	}
}
