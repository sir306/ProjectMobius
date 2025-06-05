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

#include "MassAI/MassProcessor/Representation/OLD_AgentRepProcessor.h"
// Required headers for processing entities and there fragments
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassExternalSubsystemTraits.h" // This is needed so we can use subsystems and have no compile errors
// Fragments to include with this processor
#include "MassAI/Fragments/EntityInfoFragment.h"
// Shared Fragments to include with the processor
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentRepresenatationFragment.h"
// Tags
#include "MassAI/Tags/MassAITags.h"
// Subsystems to include with the processor
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
// actors
#include "MassAI/Actors/AgentRepresentationActorISM.h"
#include "Components/InstancedStaticMeshComponent.h"
// multithreading and async
#include "HeatmapSubsystem.h"
#include "Async/ParallelFor.h"
#include "MassAI/Subsystems/TimeDilationSubSystem.h"

UAgentRepProcessor::UAgentRepProcessor():
	HeatmapSubsystem(nullptr), bRegisteredProperties(false)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);

	bRequiresGameThreadExecution = true;

	// set the variable ptrs to null
	AgentRepresentationActor = nullptr;
	TimeDilationSubSystem = nullptr;
}

void UAgentRepProcessor::ConfigureQueries()
{
	// // The required fragments for this processor
	// EntityQuery.AddSharedRequirement<FAgentRepresentationFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	//
	// // The Entity Query Required fragments for this processor;
	// EntityQuery.AddRequirement<FEntityInfoFragment>(EMassFragmentAccess::ReadWrite);
	//
	// /* Add subsystem requirements */
	// // Representation subsystem
	// EntityQuery.AddSubsystemRequirement<UMRS_RepresentationSubsystem>(EMassFragmentAccess::ReadOnly);
	//
	// // Heatmap module subsystem
	// EntityQuery.AddSubsystemRequirement<UHeatmapSubsystem>(EMassFragmentAccess::ReadWrite);
	//
	// // Required Query Tags
	// EntityQuery.AddTagRequirement<FMassEntityDeleteTag>(EMassFragmentPresence::None);
	//
	// // Register the entity query with the processor
	// EntityQuery.RegisterWithProcessor(*this);
	//
	// // Register requirements for the processor
	// ProcessorRequirements.AddSubsystemRequirement<UMRS_RepresentationSubsystem>(EMassFragmentAccess::ReadOnly);
	//
	// // Time Dilation Subsystem
	// ProcessorRequirements.AddSubsystemRequirement<UTimeDilationSubSystem>(EMassFragmentAccess::ReadOnly);
}

void UAgentRepProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	// // if the time dilation subsystem is not valid attempt to get it
	// if(TimeDilationSubSystem == nullptr)
	// {
	// 	TimeDilationSubSystem = ExecutionContext.GetWorld()->GetSubsystem<UTimeDilationSubSystem>();
	//
	// 	// in case the subsystem is not valid return and try again next tick
	// 	if(TimeDilationSubSystem == nullptr)
	// 	{
	// 		return;
	// 	}
	// }
	//
	// // Get the representation subsystem if it is not valid
	// if (RepresentationSubsystem == nullptr)
	// {
	// 	RepresentationSubsystem = ExecutionContext.GetWorld()->GetSubsystem<UMRS_RepresentationSubsystem>();
	// 	return;
	// }
	//
	//
	//
	// //TODO: too many checks needs to be optimized
	// // Get the time dilation subsystem and check current time step
	// if(CurrentTimeStep != TimeDilationSubSystem->CurrentTimeStep) // this works by adding this to a processor requirement instead of the entity query
	// {
	// 	CurrentTimeStep = TimeDilationSubSystem->CurrentTimeStep;
	// 	bIsPaused = TimeDilationSubSystem->bIsPaused;
	// 	AgentRenderHeight = RepresentationSubsystem->MaxRenderHeight;
	// }
	// // check that the pause state not changed this way if the time step hasn't changed but is about to we need to get animations moving again
	// else if(bIsPaused != TimeDilationSubSystem->bIsPaused)
	// {
	// 	bIsPaused = TimeDilationSubSystem->bIsPaused;
	// 	AgentRenderHeight = RepresentationSubsystem->MaxRenderHeight;
	// } // render height check to see if we need to update the render height
	// else if (AgentRenderHeight != RepresentationSubsystem->GetMaxRenderHeight())
	// {
	// 	AgentRenderHeight = RepresentationSubsystem->MaxRenderHeight;
	// }
	// else
	// {
	// 	return;
	// }
	//
	// // clear the heatmap locations array but maintain memory allocation size
	// HeatmapLocations.Reset();
	//
	// EntityQuery.ForEachEntityChunk(EntityManager, ExecutionContext, ([this](FMassExecutionContext& Context) {
	//
	// 	
	// 	
	// 	// Check if we are in world and registered the properties -- we use a bool so we only check once and not every variable each time
	// 	if(!bRegisteredProperties)
	// 	{
	// 		RegisterProperties(Context);
	//
	// 		// we check again because we need to make sure we have the properties before we continue and in world 
	// 		if(!bRegisteredProperties)
	// 		{
	// 			return;
	// 		}
	// 	}
	//
	// 	// if we swap data the last time needs to be updated
	// 	if (TimeDilationSubSystem->GetCurrentSimTime() == 0.0f)
	// 	{
	// 		LastUpdatedCurrentTime = 0.0f;
	// 	}
	// 	else if (TimeDilationSubSystem->GetCurrentSimTime() < LastUpdatedCurrentTime)
	// 	{
	// 		LastUpdatedCurrentTime = TimeDilationSubSystem->GetCurrentSimTime();
	// 		bUpdateHeatmap = true;
	// 	}
	//
	// 	// if time is the same then no heatmap update is needed
	// 	// TODO: this is a TEMP fix by limiting the heatmap update to no more than 0.1 seconds between updates
	// 	else if (LastUpdatedCurrentTime != TimeDilationSubSystem->GetCurrentSimTime() || LastUpdatedCurrentTime == 0.0f)// if 0 then this is first run
	// 	{
	// 		float TimeDifference = TimeDilationSubSystem->GetCurrentSimTime() - LastUpdatedCurrentTime;
	// 		if(TimeDifference < 0.1f && LastUpdatedCurrentTime != 0.0f)
	// 		{
	// 			bUpdateHeatmap = false;
	// 		}
	// 		else
	// 		{
	// 			bUpdateHeatmap = true;
	// 			LastUpdatedCurrentTime = TimeDilationSubSystem->GetCurrentSimTime();
	// 		}
	// 	
	// 	}
	// 	// Get the agent representation fragment
	// 	//auto& AgentRepresentationFragment = Context.GetMutableSharedFragment<FAgentRepresentationFragment>();
	//
	//
	// 	// Get the entity info fragment
	// 	const TArrayView<FEntityInfoFragment>& EntityInfoFragment = Context.GetMutableFragmentView<FEntityInfoFragment>();
	//
	// 	// Get the representation subsystem
	// 	//const UMRS_RepresentationSubsystem* RepresentationSubsystem = Context.GetSubsystem<UMRS_RepresentationSubsystem>();
	//
	// 	// array to store instances to destroy will need to be improved when agent info is fixed
	// 	//TArray<int32> InstancesToDestroy;
	//
	// 	// TODO: need to not empty the locations for every loop as this is causing things to reset before they are sent to the heatmap
	// 	// TODO: need to sort a better way to memory manage the heatmap locations -> we know max agents so implement memory management for this
	// 	
	// 	// // Clear the heatmap locations array
	// 	// HeatmapLocations.Empty();
	// 	//
	// 	// // Size the heatmap locations array to the number of entities
	// 	// HeatmapLocations.Reserve(EntityInfoFragment.Num());
	//
	// 	/* TODO
	// 	 * this for loop is not great on performance it accounts for 15 -20ms  and is causing hitches in the framerate
	// 	 * 
	// 	 * main problem is the logic on this loop needs adjusting to not be so heavy on the cpu and be thread safe
	// 	 * current variables and methods are not thread safe 
	// 	 *
	// 	 * another big problem is the ism components -> the issue is the update instance transform can only happen on main thread
	// 	 * 
	// 	*/
	// 	
	// 	for(FEntityInfoFragment& EntityInfoFrag : EntityInfoFragment)
	// 	{
	// 		//HeatmapSubsystem->UpdateHeatmaps(EntityInfo.CurrentLocatiodn);
	// 	
	// 		// if the agent render is false and already ready to destroy we dont need to update the instance
	// 		if(!EntityInfoFrag.bRenderAgent && EntityInfoFrag.bReadyToDestroy) 
	// 		{
	// 			continue;
	// 		}
	// 	
	// 		
	// 		// as the above checks the render state we don't need to check if the instance is valid
	// 		HeatmapLocations.Add(EntityInfoFrag.CurrentLocation); // this adds 3ms to this loop
	// 		
	// 		UpdateEntityRepresentation(EntityInfoFrag); // this accounts for 10 - 14ms of the loop
	// 		
	// 		//EntityInfoFrag.bRenderAgent = EntityInfoFrag.bRenderAgent;
	// 	}
	// 	
	// 	
	// }));
	//
	//
	// // performing the render state dirty outside of loop so not to call for every chunk
	// //AgentRepresentationActor->MarkComponentsRenderStateDirty();
	// AgentRepresentationActor->GetMaleISMComponent()->MarkRenderInstancesDirty();
	// AgentRepresentationActor->GetFemaleISMComponent()->MarkRenderInstancesDirty();
	//
	// // TODO: this causes 4-5ms overhead likely due to parts being non async
	//
	// // are there any locations to update the heatmap with
	// if(!HeatmapLocations.IsEmpty())
	// {
	// 	// update if in valid time interval
	// 	if (bUpdateHeatmap)
	// 	{
	// 		HeatmapSubsystem->UpdateHeatmapsWithLocations(HeatmapLocations);
	// 	}
	// }
	// else
	// {
	// 	HeatmapSubsystem->ClearEmptyHeatmaps();//clear live tracking heatmaps
	// }
	//
	// // update the pause state
	// bLastPauseLoop = bIsPaused;
}

void UAgentRepProcessor::RegisterProperties(FMassExecutionContext& Context)
{
	// Check if we are in world
	if (!Context.GetWorld())
	{
		return;
	}
	
	if(AgentRepresentationActor == nullptr)
	{
		// Get the agent representation fragment
		auto& AgentRepresentationFragment = Context.GetMutableSharedFragment<FAgentRepresentationFragment>();
		
		// observor has not spawned it yet so return check
		if (!AgentRepresentationFragment.ActorRepresentationClass->IsValidLowLevel() || !AgentRepresentationFragment.ActorRepresentationClass->CheckStillInWorld())
		{
			return;
		}
		
		AgentRepresentationActor = AgentRepresentationFragment.ActorRepresentationClass;
	}
	
	if(HeatmapSubsystem == nullptr)
	{
		HeatmapSubsystem = Context.GetWorld()->GetSubsystem<UHeatmapSubsystem>();
	}

	// check if the we registered the properties
	if(HeatmapSubsystem == nullptr || AgentRepresentationActor == nullptr)
	{
		return;
	}

	// Get the representation subsystem if it is not valid
	if (RepresentationSubsystem == nullptr)
	{
		RepresentationSubsystem = Context.GetWorld()->GetSubsystem<UMRS_RepresentationSubsystem>();
	}

	// if the representation subsystem is not valid return and try again next tick
	if (RepresentationSubsystem == nullptr)
	{
		return;
	}

	// get the number of ism instances on the two components
	auto MaleISMComp = AgentRepresentationActor->GetMaleISMComponent();
	auto FemaleISMComp = AgentRepresentationActor->GetFemaleISMComponent();

	HeatmapLocationsSize = MaleISMComp->GetNumInstances() + FemaleISMComp->GetNumInstances();

	// we need to size the heatmap locations array to the number of entities - this memory allocation helps with performance
	HeatmapLocations.Reserve(HeatmapLocationsSize); // we dont want to shrink due to replay capabilities
	
	// set the properties to registered
	bRegisteredProperties = true;
}

void UAgentRepProcessor::UpdateEntityRepresentation(FEntityInfoFragment& EntityInfo) const // TODO: this can be refactored to be more efficient and cleaner with an additional method
{
	float ThesisAgentScale = 1.0f;
	
	// for my thesis when experiment is running I only need to see agents between 0 and 1.5 meters
	if (EntityInfo.CurrentLocation.Z > AgentRenderHeight)
	{
		ThesisAgentScale = 0.0f;
	}

	
	bool bValidRequest = false;
	auto AgentIsmComp = GetISMComponent(EntityInfo.bIsMale, AgentRepresentationActor, bValidRequest);

	// did we successfully get the ISM component
	if (!bValidRequest)
	{
		return;
	}

	// change the pause state of the animation if it has changed
	if(bLastPauseLoop != bIsPaused)
	{
		// Update the animation pause
		SetAnimationPause(AgentIsmComp, EntityInfo.InstanceID, bIsPaused);
	}
	
	if (!EntityInfo.bRenderAgent)
	{
		// zero out the scale, location and rotation - this is so if there is delay in destroying it will not be visible to the user
		UpdateInstanceTransformForISMComp(AgentIsmComp, EntityInfo.InstanceID);

		// because no location matching this entity it is ready to be destroyed
		EntityInfo.bReadyToDestroy = true;
	}
	else
	{
			
		// update the instance transform with the new location and rotation
		//UpdateInstanceTransformForISMComp(AgentIsmComp, EntityInfo.InstanceID, EntityInfo.CurrentRotation, EntityInfo.CurrentLocation, FVector(ThesisAgentScale));
		UpdateInstanceTransformForISMComp(AgentIsmComp, EntityInfo.InstanceID, EntityInfo.CurrentRotation, EntityInfo.CurrentLocation, FVector(0));

		// TODO: this StepsPerSecond is not used yet - multiples animation speed
		float StepsPerSecond = 0.0f;
			
		// did the animation bracket change
		if (EntityInfo.bAnimationChanged)
		{
			// update the animation bracket custom data values
			UpdateCustomDataForISMComp(AgentIsmComp, EntityInfo.InstanceID, EntityInfo.CurrentMovementBracket);
		}

		// this makes it ready to have its location calculated again
		//EntityInfo.bRenderAgent = false;
	}
	
	
	// AgentRepresentationActor->MarkComponentsRenderStateDirty();
}

void UAgentRepProcessor::SetAnimationPause(UInstancedStaticMeshComponent* IsmComp, int32 InstanceIndex, bool bPause)
{
	if (bPause)
	{
		IsmComp->SetCustomDataValue(InstanceIndex, 1, 0.0f);
	}
	else
	{
		IsmComp->SetCustomDataValue(InstanceIndex, 1, 1.0f);
	}
}

UInstancedStaticMeshComponent* UAgentRepProcessor::GetISMComponent(bool bIsMale,
                                                                              AAgentRepresentationActorISM* AgentRepresenterActor, bool& bValidReq)
{
	// null ptr check
	if (AgentRepresenterActor == nullptr || !AgentRepresenterActor->IsValidLowLevel() ||
		!AgentRepresenterActor->CheckStillInWorld() || !AgentRepresenterActor->GetFemaleISMComponent() ||
		!AgentRepresenterActor->GetMaleISMComponent())
	{
		bValidReq = false;
		return nullptr;
	}
	// the checks that component is valid and not null so request is valid
	bValidReq = true;

	// checks were valid so this return should be valid to do
	return bIsMale ? AgentRepresenterActor->GetMaleISMComponent() : AgentRepresenterActor->GetFemaleISMComponent();
}

void UAgentRepProcessor::UpdateInstanceTransformForISMComp(UInstancedStaticMeshComponent* IsmComp,
                                                                      int32 InstanceIndex, const FRotator& Rotation, const FVector& Location, const FVector& Scale)
{
	// null ptr check and valid 
	if (IsmComp == nullptr || !IsmComp->IsValidLowLevel())
	{
		return;
	}
	
	IsmComp->UpdateInstanceTransform(
		InstanceIndex,
		FTransform(Rotation, Location, Scale),
		false, false, true);
}

void UAgentRepProcessor::UpdateCustomDataForISMComp(UInstancedStaticMeshComponent* IsmComp,
                                                               int32 InstanceIndex, EPedestrianMovementBracket MovementBracket) const
{
	// Update the current agent movement animation based on speed
	FVatMovementFrames MovementFrames = RepresentationSubsystem->GetMovementFrames(MovementBracket);
								
	// update the custom data values
	IsmComp->SetCustomDataValue(InstanceIndex, 2, MovementFrames.FirstFrame);
	IsmComp->SetCustomDataValue(InstanceIndex, 3, MovementFrames.LastFrame);
	//AgentIsmComp->SetCustomDataValue(EntityInfo.InstanceID, 1, EntityInfo.);
	// AgentIsmComp->SetCustomDataValue(EntityInfo.InstanceID, 2, 50.0f);
}

FRunnableRepresentationProcessorOLD::FRunnableRepresentationProcessorOLD(FEntityInfoFragment* InEntityInfo,
                                                                   AAgentRepresentationActorISM* InAgentRepresentationActor)
{
	Thread = FRunnableThread::Create(this, TEXT("UpdateRepresentationISM"));
}

FRunnableRepresentationProcessorOLD::~FRunnableRepresentationProcessorOLD()
{
	if (Thread)
	{
		Thread->Kill();
		delete Thread;
	}
}

bool FRunnableRepresentationProcessorOLD::Init()
{
	return FRunnable::Init();
}

uint32 FRunnableRepresentationProcessorOLD::Run()
{
	UE_LOG(LogTemp, Display, TEXT("FRunnableRepresentationProcessor::Running")); // check not running
	// test log
	// get the entities location and rotation
	FVector Location = EntityInfo->CurrentLocation;
	FRotator Rotation = EntityInfo->CurrentRotation;
			
	// Create transform
	FTransform NewTransform = FTransform(Rotation, Location, FVector(0.1f));
			
	// Get the name this way we can see what gender the person is
	FString AgentVariation = EntityInfo->EntityName;
            
	// is the name Male[n] or Female[n]
	if (AgentVariation.Contains("Female"))// We use female as the check so male doesn't return true on females
	{
		if (AgentRepresentationActor->GetFemaleISMComponent()->UpdateInstanceTransform(EntityInfo->InstanceID, NewTransform, true, false, false))
		{
					
		}
	}
	else
	{
		if (AgentRepresentationActor->GetMaleISMComponent()->UpdateInstanceTransform(EntityInfo->InstanceID, NewTransform, true, false, false))
		{
					
		}
	}
	return 1;
}

void FRunnableRepresentationProcessorOLD::Stop()
{
	FRunnable::Stop();
}
