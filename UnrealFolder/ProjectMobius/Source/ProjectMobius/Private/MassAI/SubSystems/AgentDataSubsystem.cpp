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

#include "MassAI/SubSystems/AgentDataSubsystem.h"
// File Parser for JSON
#include <iomanip>
#include <iostream>

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonTypes.h"
#include <MassAI/Fragments/SharedFragments/SimulationFragment.h>

#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Interfaces/ProjectMobiusInterface.h"
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
#include "Subsystems/LoadingSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Util/MemoryTraceHelper.h"
#include "HAL/PlatformTime.h"

namespace
{
	void ReportAgentDataError(const UObject* ContextObject, const FString& Title, const FString& Message, const FString& Location)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(ContextObject))
		{
			Feedback->ReportError(
				FText::FromString("Agent Data Error"),
				FText::FromString(Title),
				FText::FromString(Message),
				FText::FromString(Location));
		}
	}

	void ReportAgentDataErrorAnyThread(const UObject* ContextObject, const FString& Title, const FString& Message, const FString& Location)
	{
		FMobiusErrorMessage Payload;
		Payload.TitleBarText = FText::FromString("Agent Data Error");
		Payload.ErrorTitle = FText::FromString(Title);
		Payload.ErrorMessage = FText::FromString(Message);
		Payload.ErrorLocation = FText::FromString(Location);
		UMobiusUserFeedbackSubsystem::ReportErrorFromAnyThread(TWeakObjectPtr<UObject>(const_cast<UObject*>(ContextObject)), Payload);
	}
}


void UAgentDataSubsystem::ParseEntityInfo(const FMobiusAgentEntityData& Entity, FEntityInfoFragment& OutInfo)
{
	OutInfo.EntityID = Entity.Id;
	OutInfo.EntityName = Entity.Name;
	OutInfo.EntitySimTimeS = FString::SanitizeFloat(Entity.SimTimeS);
	OutInfo.EntityMaxSpeed = Entity.MaxSpeed;
	OutInfo.EntityM_Plane = Entity.MPlane;
	OutInfo.EntityMap = Entity.Map;
}

UAgentDataSubsystem::UAgentDataSubsystem() :
	JSONDataFile(TEXT("")),
	JSONDataString(TEXT("")),
	MaxAgents(0),
	QuadTreeDataActor()
{
	//AgentMovementInfoData = FSimulationFragment();
}

UAgentDataSubsystem::~UAgentDataSubsystem()
{
}

void UAgentDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Get the Game Instance
	if(UProjectMobiusGameInstance* GameInst = GetMobiusGameInstance(GetWorld()))
	{
		// Bind the required Game Instance Delegates
		//GameInst->OnDataFileChanged.AddDynamic(this, &UAgentDataSubsystem::GetUpdatedJSONDataFile);
		// log that it has binded
		UE_LOG(LogTemp, Warning, TEXT("Data file Changed Delegate Binded"));

		// Get the Current Data File set on the instance
		// JSONDataFile = GameInst->GetPedestrianDataFilePath();
		// GetJSONDataFile(JSONDataFile);

	}
	else
	{
		//TODO: optimize this so that the file path is not hardcoded and we only perform this operation once

		//GetJSONDataFile("D:\\MastersAndMobius\\ProjectMobius\\TestData\\TechnicalSchool1000People\\TechnicalSchool_1000.json");
		//GetJSONDataFile("C:\\Users\\User_VR4\\Desktop\\WORK\\ProjectMobius\\ProjectMobius\\TestData\\TechnicalSchool1000People\\TechnicalSchool_1000.json");
		//GetJSONDataFile("C:\\Users\\User_VR4\\Desktop\\WORK\\ProjectMobius\\ProjectMobius\\TestData\\iso-test-json-1.json");
		//"D:\\1_Work\\Mobius\\ProjectMobius\\TestData\\iso-test-json-1.json"
		//"D:\\MastersAndMobius\\ProjectMobius\\TestData\\iso-test-json-1.json");

		// Check that json object is not still nullptr
		// if (imported data == nullptr)
		// {
		// 	UE_LOG(LogTemp, Warning, TEXT("JSON Object is nullptr"));
		// }
		// else
		// {
		// 	CalculateMaxEntitiesPermitted();
		// }
	}

}

void UAgentDataSubsystem::Deinitialize()
{
	if (AgentDataRunnable)
	{
		AgentDataRunnable->Stop();
		// Do not call Exit() manually — the destructor calls WaitForCompletion() then
		// UE calls Exit() once cleanly after Run() returns.
		AgentDataRunnable.Reset();
	}
	Super::Deinitialize();
}

void UAgentDataSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	while (ProgressQueue.Dequeue(LoadProgress))
	{
		OnLoadSimulationDataProgress.Broadcast(LoadProgress);
	}

	while (MaxAgentsQueue.Dequeue(MaxAgents))
	{
		OnMaxAgentCount.Broadcast(MaxAgents);
	}

	// This temp fix but as we only should be changing loading text at a few key points this should be ok for now
	while (LoadingTaskQueue.Dequeue(CurrentLoadingTask))
	{
		// Get loading ui
		if (UWorld* World = GetWorld())
		{
			//TODO: this is a temp fix but we need to find a better way to update loading text from this subsystems
			// Look into interface or use a event dispatcher or something better
			if (ULoadingSubsystem* LoadingSubsystem = World->GetSubsystem<ULoadingSubsystem>())
			{
				LoadingSubsystem->SetLoadingText(true, CurrentLoadingTask);
			}
		}
	}

	if (bIsDataLoaded)
	{
		// log this called
		UE_LOG(LogTemp, Warning, TEXT("OnLoadSimulationDataComplete Broadcasted"));
		OnLoadSimulationDataComplete.Broadcast();
		bIsDataLoaded = false; // Reset the flag after broadcasting
	}
}

void UAgentDataSubsystem::SetEntityInfoByIndex(int32 Index, FEntityInfoFragment& EntityInfoFragToUpdate) const
{
	if (Index < 0 || Index >= MaxAgents)
	{
		UE_LOG(LogTemp, Warning, TEXT("Index out of range"));
		ReportAgentDataError(this,
		                     TEXT("Invalid entity index"),
		                     TEXT("Requested entity index is out of range."),
		                     TEXT("AgentDataSubsystem"));
		return;
	}

	// Check if we have entity data cached (moved out of runnable before it was torn down)
	if (CachedEntityData.Num() > 0)
	{
		if (!CachedEntityData.IsValidIndex(Index))
		{
			ReportAgentDataError(this,
			                     TEXT("Entity data missing"),
			                     TEXT("Entity index is not present in the simulation data."),
			                     TEXT("AgentDataSubsystem"));
			return;
		}

		ParseEntityInfo(CachedEntityData[Index], EntityInfoFragToUpdate);
		return;
	}

	ReportAgentDataError(this,
						 TEXT("Simulation data missing"),
						 TEXT("No simulation data is loaded for entity info."),
						 TEXT("AgentDataSubsystem"));
}
void UAgentDataSubsystem::SetEntityRenderingByIndex(int32 Index,
                                                    FEntityRenderingFragment& EntityRenderingFragToUpdate) const
{
	if (Index < 0 || Index >= MaxAgents)
	{
		UE_LOG(LogTemp, Warning, TEXT("Index out of range"));
		ReportAgentDataError(this,
		                     TEXT("Invalid entity index"),
		                     TEXT("Requested entity index is out of range."),
		                     TEXT("AgentDataSubsystem"));
		return;
	}

	EntityRenderingFragToUpdate.EntityID = Index;

	FString AgentName;

	// Check if we have entity data cached (moved out of runnable before it was torn down)
	if (CachedEntityData.Num() > 0)
	{
		if (!CachedEntityData.IsValidIndex(Index))
		{
			ReportAgentDataError(this,
			                     TEXT("Entity data missing"),
			                     TEXT("Entity index is not present in the simulation data."),
			                     TEXT("AgentDataSubsystem"));
			return;
		}

		AgentName = CachedEntityData[Index].Name;
	}
	else
	{
		ReportAgentDataError(this,
							 TEXT("Simulation data missing"),
							 TEXT("No simulation data is loaded for entity rendering."),
							 TEXT("AgentDataSubsystem"));
		return;
	}

	// update gender
	EntityRenderingFragToUpdate.bIsMale = !(AgentName.Contains("Female"));

	// update age demographic
	if (AgentName.Contains("Child"))
	{
		EntityRenderingFragToUpdate.AgeDemographic = EAgeDemographic::Ead_Child;
	}
	else if (AgentName.Contains("Elderly"))
	{
		EntityRenderingFragToUpdate.AgeDemographic = EAgeDemographic::Ead_Elderly;
	}
	else if (AgentName.Contains("Adult"))
	{
		EntityRenderingFragToUpdate.AgeDemographic = EAgeDemographic::Ead_Adult;

		//DEBUG: Sample data has adults with gender not elderly
		// so create rand bool to set to elderly to see a mix of adults and elderly in test sim
		// if (FMath::FRandRange(0.0f, 1.0f) > 0.5f)
		// {
		// 	EntityInfoFragToUpdate.AgeDemographic = EAgeDemographic::Ead_Elderly;
		// }
	}
	else // no valid age demographic found -> TODO: for now just set it to adult but need to think on how we want to handle this
	{
		EntityRenderingFragToUpdate.AgeDemographic = EAgeDemographic::Ead_Adult;
	}

	// These are defaults but respawning agents that have this set will still be set to false
	// Ensure they are set to be rendered
	EntityRenderingFragToUpdate.bRenderAgent = true;

	// Set the entity to not be ready to destroy
	EntityRenderingFragToUpdate.bReadyToDestroy = false;
}

void UAgentDataSubsystem::UpdateMaxAgentCount(int32 NewMaxAgentCount)
{
	MaxAgents = NewMaxAgentCount;

	// log the new max agent count
	UE_LOG(LogTemp, Warning, TEXT("New Max Agent Count: %d"), MaxAgents);
}

void UAgentDataSubsystem::ClearPerFileState()
{
	// Prevent a stale completion flag from the previous run firing BuildPedestrianMovementFragmentData
	// with the new (not-yet-loaded) runnable's empty data on the next Tick.
	bIsDataLoaded = false;

	// CachedEntityData holds the previous file's FMobiusAgentEntityData array. It was
	// only ever overwritten when the next file's BuildFrag ran, so between
	// switches the prior payload stayed live. Drop it now.
	CachedEntityData.Empty();
	CachedEntityData.Shrink();

	// Drain the Tick-fed queues so they don't retain slot capacity from the
	// prior file. TQueue has no Shrink; dequeue loop is cheapest.
	{
		float  Tmp = 0.f;   while (ProgressQueue.Dequeue(Tmp))     { }
	}
	{
		int32  Tmp = 0;     while (MaxAgentsQueue.Dequeue(Tmp))    { }
	}
	{
		FString Tmp;        while (LoadingTaskQueue.Dequeue(Tmp))  { }
	}
}

bool UAgentDataSubsystem::CheckFilePathExists(FString FilePath)
{
	if (FPaths::FileExists(FilePath))
	{
		return true;
	}
	return false;
}

void UAgentDataSubsystem::CreateJsonReaderAndString(FString& OutJsonString, TSharedRef<TJsonReader<TCHAR>>& OutJsonReader, FString JsonFile)
{
	// Load File to String
	if (!FFileHelper::LoadFileToString(OutJsonString, *JsonFile))
	{
		ReportAgentDataError(this,
		                     TEXT("Failed to read simulation file"),
		                     FString::Printf(TEXT("Unable to read JSON data from: %s"), *JsonFile),
		                     TEXT("AgentDataSubsystem"));
		OutJsonString.Empty();
	}

	// Create JSON Reader
	OutJsonReader = TJsonReaderFactory<TCHAR>::Create(OutJsonString);
}

FProcessAgentSimulationDataRunnable::FProcessAgentSimulationDataRunnable(FString InAgentDataFile, TWeakObjectPtr<UAgentDataSubsystem> Owner)
{
	// Set the owner subsystem if valid
	if (Owner.IsValid())
	{
		OwnerSubsystem = Owner;
	}
	else
	{
		// Log a warning if the owner subsystem is not valid and implement propper error handling
		ReportAgentDataErrorAnyThread(nullptr,
		                              TEXT("Agent data subsystem missing"),
		                              TEXT("Background loader could not access the agent data subsystem."),
		                              TEXT("AgentDataSubsystem"));
		return;
	}
	SimulationDataFilePath = InAgentDataFile;
	// check file actually exists before creating the thread
	if (SimulationDataFilePath.IsEmpty() || !FPaths::FileExists(SimulationDataFilePath))
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
		                              TEXT("Simulation file not found"),
		                              FString::Printf(TEXT("Agent data file does not exist: %s"), *SimulationDataFilePath),
		                              TEXT("AgentDataSubsystem"));
		return;
	}



	// Create the thread -- The thread priority is set to TPri_Normal this may need to be adjusted based on the application
#if !UE_BUILD_SHIPPING
	const double ThreadCreateStart = FPlatformTime::Seconds();
#endif
	Thread = FRunnableThread::Create(this, TEXT("FProcessAgentSimulationDataRunnable"), 0, TPri_Normal);
#if !UE_BUILD_SHIPPING
	UE_LOG(LogTemp, Display, TEXT("FProcessAgentSimulationDataRunnable thread create took %.3f ms"), (FPlatformTime::Seconds() - ThreadCreateStart) * 1000.0);
#endif
}

FProcessAgentSimulationDataRunnable::~FProcessAgentSimulationDataRunnable()
{
	// ensure you’ve called Stop() first
	if (Thread)
	{
		Thread->WaitForCompletion();  // safe join off-thread
		delete Thread;
		Thread = nullptr;
	}
	AgentMovementInfoData = FSimulationFragment();

	// DEBUG: Prove the runnable has been deleted
	// AsyncTask(ENamedThreads::GameThread, []()
	// 	{
	//
	// 		UE_LOG(LogTemp, Warning, TEXT("Runnable Deleted"));
	// 	});
}

bool FProcessAgentSimulationDataRunnable::LoadFileAndDeserialize()
{
	AgentFileFormat = EMobiusAgentFileFormat::Unknown;
	AgentSimulationData = FMobiusAgentSimulationData();

#if !UE_BUILD_SHIPPING
	const double DeserializeStart = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("Agent data import start"));
#endif

	FString ImportError;
	if (!FMobiusAgentDataImporter::ImportAgentFile(SimulationDataFilePath, AgentSimulationData, &ImportError))
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
		                              TEXT("Failed to import simulation file"),
		                              ImportError.IsEmpty() ? FString::Printf(TEXT("Unable to import data from: %s"), *SimulationDataFilePath) : ImportError,
		                              TEXT("AgentDataSubsystem"));
		bShouldStop = true;
		return false;
	}

	AgentFileFormat = AgentSimulationData.SourceFormat;

#if !UE_BUILD_SHIPPING
	UE_LOG(LogTemp, Display, TEXT("Agent data import finish: format=%d entities=%d samples=%d %.3f ms"),
		static_cast<int32>(AgentFileFormat),
		AgentSimulationData.Entities.Num(),
		AgentSimulationData.Samples.Num(),
		(FPlatformTime::Seconds() - DeserializeStart) * 1000.0);
#endif

	return true;
}
void FProcessAgentSimulationDataRunnable::ProcessMetadata(bool& bCalculateTimeBetweenSteps, bool& bCalculateMaxTime)
{
	bCalculateTimeBetweenSteps = true;
	bCalculateMaxTime = true;

	if (AgentSimulationData.Samples.Num() == 0)
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
		                              TEXT("Simulation data missing"),
		                              TEXT("Imported agent data has no simulation samples."),
		                              TEXT("AgentDataSubsystem - FProcessAgentSimulationDataRunnable::ProcessMetadata"));
		return;
	}

	if (AgentSimulationData.Metadata.MaxNumEntities > 0)
	{
		MaxAgents = AgentSimulationData.Metadata.MaxNumEntities;
	}
	else
	{
		MaxAgents = AgentSimulationData.Entities.Num();
	}

	if (AgentSimulationData.Metadata.Duration > 0.0f)
	{
		AgentMovementInfoData.MaxTime = AgentSimulationData.Metadata.Duration;
		bCalculateMaxTime = false;
	}

	if (AgentSimulationData.Metadata.SamplingRate > 0.0f)
	{
		TimeBetweenSteps = AgentSimulationData.Metadata.SamplingRate;
		bCalculateTimeBetweenSteps = false;
	}

	if (AgentSimulationData.Metadata.Duration > 0.0f && AgentSimulationData.Metadata.SamplingRate > 0.0f)
	{
		TargetDataCount = FMath::CeilToInt(AgentSimulationData.Metadata.Duration / AgentSimulationData.Metadata.SamplingRate);
	}
	else
	{
		int32 MaxTimestepIndex = 0;
		for (const FMobiusAgentSampleData& Sample : AgentSimulationData.Samples)
		{
			MaxTimestepIndex = FMath::Max(MaxTimestepIndex, Sample.TimestepIndex);
		}
		TargetDataCount = MaxTimestepIndex + 1;
	}
}

void FProcessAgentSimulationDataRunnable::RunSimulationDataGatheringLoop(bool bCalculateTimeBetweenSteps, bool bCalculateMaxTime)
{
	if (AgentSimulationData.Samples.Num() == 0)
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
		                              TEXT("Simulation data missing"),
		                              TEXT("Agent data was not loaded before processing simulation steps."),
		                              TEXT("AgentDataSubsystem - FProcessAgentSimulationDataRunnable::RunSimulationDataGatheringLoop"));
		return;
	}

	int32 MaxTimestepIndex = 0;
	for (const FMobiusAgentSampleData& Sample : AgentSimulationData.Samples)
	{
		MaxTimestepIndex = FMath::Max(MaxTimestepIndex, Sample.TimestepIndex);
	}

	if (TargetDataCount <= 0)
	{
		TargetDataCount = MaxTimestepIndex + 1;
	}

	NumOfAgentsPerTimeStep.Reserve(TargetDataCount);

	TMap<int32, TArray<const FMobiusAgentSampleData*>> SamplesByTimestep;
	for (const FMobiusAgentSampleData& Sample : AgentSimulationData.Samples)
	{
		SamplesByTimestep.FindOrAdd(Sample.TimestepIndex).Add(&Sample);
	}

	const bool bIsSI = AgentSimulationData.Metadata.bIsSI;
	const bool bIsDeg = AgentSimulationData.Metadata.bIsDeg;
	const bool bInvertYAxis = AgentFileFormat == EMobiusAgentFileFormat::Json || AgentFileFormat == EMobiusAgentFileFormat::MobiusHdf5;

	for (int32 TimestepIdx = 0; TimestepIdx <= MaxTimestepIndex && !bShouldStop; ++TimestepIdx)
	{
		CurrentDataCount = TimestepIdx;
		TArray<const FMobiusAgentSampleData*>* TimestepSamples = SamplesByTimestep.Find(TimestepIdx);

		if (!TimestepSamples || TimestepSamples->Num() == 0)
		{
			NumOfAgentsPerTimeStep.Add(0);
			AgentMovementInfoData.SimulationData->Add(TimestepIdx, TArray<FSimMovementSample>());
			continue;
		}

		if (bCalculateTimeBetweenSteps && AgentSimulationData.Metadata.SamplingRate > 0.0f)
		{
			TimeBetweenSteps = AgentSimulationData.Metadata.SamplingRate;
			bCalculateTimeBetweenSteps = false;
		}

		if (bCalculateMaxTime && AgentSimulationData.Metadata.Duration > 0.0f)
		{
			AgentMovementInfoData.MaxTime = AgentSimulationData.Metadata.Duration;
		}
		else if (bCalculateMaxTime && TimeBetweenSteps > 0.0f)
		{
			AgentMovementInfoData.MaxTime = TimestepIdx * TimeBetweenSteps;
		}

		minimumStepDuration = 0.6;
		maximumStepDuration = 1.0;
		if (TimeBetweenSteps > 0.0f)
		{
			minTimedSrcRecordsForStep = (int)std::round(minimumStepDuration * (int)std::round(((double)1.0 / (double)TimeBetweenSteps)));
			maxTimedSrcRecordsForStep = (int)std::round(maximumStepDuration * (double)TimeBetweenSteps);
			timeDurationPerRecord = 1.0 / (double)(int)std::round(((double)1.0 / (double)TimeBetweenSteps));
		}

		NumOfAgentsPerTimeStep.Add(TimestepSamples->Num());
		TArray<FSimMovementSample> MovementSamples;
		MovementSamples.Reserve(TimestepSamples->Num());

		for (const FMobiusAgentSampleData* SamplePtr : *TimestepSamples)
		{
			if (bShouldStop) break;

			const FMobiusAgentSampleData& Sample = *SamplePtr;
			if (Sample.EntityId < 0)
			{
				ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
				                              TEXT("Invalid entity ID"),
				                              FString::Printf(TEXT("Agent sample at timestep %d has negative EntityId (%d). Skipping sample."), TimestepIdx, Sample.EntityId),
				                              TEXT("AgentDataSubsystem - RunSimulationDataGatheringLoop"));
				continue;
			}

			FVector Position = FVector::ZeroVector;
			if (FMath::IsNaN(Sample.PositionX) || FMath::IsNaN(Sample.PositionY) || FMath::IsNaN(Sample.PositionZ) ||
			    !FMath::IsFinite(Sample.PositionX) || !FMath::IsFinite(Sample.PositionY) || !FMath::IsFinite(Sample.PositionZ))
			{
				ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
				                              TEXT("Invalid position data"),
				                              FString::Printf(TEXT("Agent sample at timestep %d, entity %d has NaN/Inf position. Using ZeroVector."), TimestepIdx, Sample.EntityId),
				                              TEXT("AgentDataSubsystem - RunSimulationDataGatheringLoop"));
			}
			else
			{
				Position.X = Sample.PositionX;
				Position.Y = bInvertYAxis ? -Sample.PositionY : Sample.PositionY;
				Position.Z = Sample.PositionZ;
				Position *= bIsSI ? 100.0f : 10.0f;
			}

			FRotator Rotation = FRotator::ZeroRotator;
			if (FMath::IsNaN(Sample.Rotation) || !FMath::IsFinite(Sample.Rotation))
			{
				ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
				                              TEXT("Invalid rotation data"),
				                              FString::Printf(TEXT("Agent sample at timestep %d, entity %d has NaN/Inf rotation. Using ZeroRotator."), TimestepIdx, Sample.EntityId),
				                              TEXT("AgentDataSubsystem - RunSimulationDataGatheringLoop"));
			}
			else
			{
				const float YawDeg = bIsDeg ? (-Sample.Rotation - 90.0f) : (FMath::RadiansToDegrees(-Sample.Rotation) - 90.0f);
				Rotation = FRotator(0.0f, YawDeg, 0.0f);
			}

			float ValidatedSpeed = Sample.Speed;
			if (FMath::IsNaN(Sample.Speed) || !FMath::IsFinite(Sample.Speed) || Sample.Speed < 0.0f)
			{
				ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
				                              TEXT("Invalid speed data"),
				                              FString::Printf(TEXT("Agent sample at timestep %d, entity %d has invalid speed (%.2f). Using 0."), TimestepIdx, Sample.EntityId, Sample.Speed),
				                              TEXT("AgentDataSubsystem - RunSimulationDataGatheringLoop"));
				ValidatedSpeed = 0.0f;
			}

			FSimMovementSample& MovementSample = MovementSamples.AddDefaulted_GetRef();
			MovementSample.EntityID = Sample.EntityId;
			MovementSample.Position = Position;
			MovementSample.Rotation = Rotation;
			MovementSample.Speed = ValidatedSpeed;

			if (Sample.EntityId < AgentDataArray.Num())
			{
				AgentDataArray[Sample.EntityId].MovementData.Push(FMovementPreProcessData(Position));
			}
		}

		AgentMovementInfoData.SimulationData->Add(TimestepIdx, MovementSamples);

		const float CurrentPercentage = TargetDataCount > 0 ? (float)CurrentDataCount / (float)TargetDataCount : 1.0f;
		if (UAgentDataSubsystem* Subsys = OwnerSubsystem.Get())
		{
			Subsys->ProgressQueue.Enqueue(CurrentPercentage);
		}
	}

	CurrentDataCount = MaxTimestepIndex + 1;

	int32 PeakEntityCount = 0;
	for (const int32 Count : NumOfAgentsPerTimeStep)
	{
		PeakEntityCount = FMath::Max(PeakEntityCount, Count);
	}

	if (AgentSimulationData.Metadata.Duration > 0.0f && AgentSimulationData.Metadata.SamplingRate > 0.0f)
	{
		const int32 ExpectedTimesteps = FMath::CeilToInt(AgentSimulationData.Metadata.Duration / AgentSimulationData.Metadata.SamplingRate);
		if (CurrentDataCount < ExpectedTimesteps)
		{
			ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
			                              TEXT("Incomplete simulation data"),
			                              FString::Printf(TEXT("Agent data: loaded %d of %d expected timesteps. Data may be truncated."), CurrentDataCount, ExpectedTimesteps),
			                              TEXT("AgentDataSubsystem - RunSimulationDataGatheringLoop"));
		}
	}

	if (MaxAgents > 0 && PeakEntityCount > 0 && PeakEntityCount < MaxAgents)
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
		                              TEXT("Incomplete entity data"),
		                              FString::Printf(TEXT("Agent data: peak entity count %d < MaxAgents %d. Some entities may be missing."), PeakEntityCount, MaxAgents),
		                              TEXT("AgentDataSubsystem - RunSimulationDataGatheringLoop"));
	}
}
/**
 * Calculates rotation from movement direction when the HDF5 source data lacks rotation information.
 *
 * Algorithm:
 * 1. For each entity, collect all position samples ordered by timestep
 * 2. For each timestep, calculate direction vector to the NEXT position (look-ahead approach)
 * 3. Convert direction to rotation angle in degrees using FMath::Atan2
 * 4. For the last timestep, carry forward the previous calculated rotation
 * 5. For stationary entities (no position change), maintain last valid rotation
 *
 * @note The -90 degree offset applied to the yaw is to correct for mesh orientation in Unreal Engine,
 *       where the forward direction of the mesh faces +X but Atan2 returns 0 for movement along +X axis.
 */
void FProcessAgentSimulationDataRunnable::CalculateRotationFromMovement()
{
	// Early out if the source data already contains rotation information
	if (AgentSimulationData.Metadata.bHasRotationData)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Calculating rotation from movement direction for %d entities"), MaxAgents);

	// Process each entity independently
	for (int32 EntityIdx = 0; EntityIdx < MaxAgents; ++EntityIdx)
	{
		if (bShouldStop) break;

		// Step 1: Collect all position samples for this entity across all timesteps
		// We store (timestep index, position) pairs for later sorting
		TArray<TPair<int32, FVector>> EntityPositions;

		for (auto& Pair : *AgentMovementInfoData.SimulationData)
		{
			for (const FSimMovementSample& Sample : Pair.Value)
			{
				if (Sample.EntityID == EntityIdx)
				{
					EntityPositions.Add(TPair<int32, FVector>(Pair.Key, Sample.Position));
					break; // Only one sample per entity per timestep, so we can exit early
				}
			}
		}

		// Need at least 2 positions to calculate any rotation (need direction to next position)
		if (EntityPositions.Num() < 2) continue;

		// Step 2: Sort positions by timestep to ensure chronological order
		EntityPositions.Sort([](const TPair<int32, FVector>& A, const TPair<int32, FVector>& B)
		{
			return A.Key < B.Key;
		});

		// Step 3: Calculate rotation for each timestep using look-ahead to next position
		// LastValidRotation is used for stationary entities or the last frame
		FRotator LastValidRotation = FRotator::ZeroRotator;

		for (int32 i = 0; i < EntityPositions.Num(); ++i)
		{
			if (bShouldStop) break;

			FRotator NewRotation = LastValidRotation;  // Default to previous rotation

			// Calculate direction from current position to NEXT position (look-ahead approach)
			if (i < EntityPositions.Num() - 1)
			{
				FVector Delta = EntityPositions[i + 1].Value - EntityPositions[i].Value;

				// Only update rotation if there's meaningful movement (avoid jitter from tiny movements)
				// Threshold of 0.1 cm filters out noise while allowing detection of real movement
				if (!Delta.IsNearlyZero(0.1f))
				{
					// Calculate yaw angle from movement direction using atan2(Y, X)
					// This gives us the angle in radians from the +X axis
					float YawRad = FMath::Atan2(Delta.Y, Delta.X);
					float YawDeg = FMath::RadiansToDegrees(YawRad);

					// Apply -90 degree offset for mesh orientation correction
					// Unreal meshes typically face +X when yaw=0, but our coordinate system
					// expects facing direction to align with movement vector
					NewRotation = FRotator(0.0f, YawDeg - 90.0f, 0.0f);
					LastValidRotation = NewRotation;
				}
				// If position hasn't changed significantly, keep LastValidRotation
			}
			// For the last frame, we keep LastValidRotation (carry forward previous rotation)

			// Step 4: Update the rotation value in the actual simulation data
			int32 Timestep = EntityPositions[i].Key;
			for (FSimMovementSample& Sample : (*AgentMovementInfoData.SimulationData)[Timestep])
			{
				if (Sample.EntityID == EntityIdx)
				{
					Sample.Rotation = NewRotation;
					break;
				}
			}
		}

		// Report progress to the subsystem for UI feedback (every 100 entities to avoid flooding)
		if (EntityIdx % 100 == 0)
		{
			float CurrentPercentage = static_cast<float>(EntityIdx) / static_cast<float>(MaxAgents);
			if (UAgentDataSubsystem* Subsys = OwnerSubsystem.Get())
			{
				Subsys->ProgressQueue.Enqueue(CurrentPercentage);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Finished calculating rotation from movement"));
}

/**
 * Calculates speed from position deltas when the HDF5 source data lacks speed information.
 *
 * Algorithm:
 * 1. For each entity, collect all position samples ordered by timestep
 * 2. Calculate speed using the formula: speed = distance / time
 *    - Distance is the Euclidean distance between consecutive positions (in cm after unit conversion)
 *    - Time is TimeBetweenSteps (the sampling interval in seconds)
 * 3. Speed is converted to m/s by dividing by 100 (cm to m conversion)
 * 4. For the last timestep, the previous calculated speed is carried forward
 *
 * @note TODO: Some agents appear to be moving but have 0 speed calculated. Investigate where
 *       this discrepancy occurs - possible causes include:
 *       - TimeBetweenSteps not being set correctly before this function is called
 *       - Position data not being properly converted to cm before storage
 *       - Entities that only appear in a single timestep (skipped due to Num() < 2 check)
 *       - Floating point precision issues with very small movements
 */
void FProcessAgentSimulationDataRunnable::CalculateSpeedFromMovement()
{
	// Early out if the source data already contains speed information
	if (AgentSimulationData.Metadata.bHasSpeedData)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Calculating speed from position deltas for %d entities"), MaxAgents);

	// Process each entity independently
	for (int32 EntityIdx = 0; EntityIdx < MaxAgents; ++EntityIdx)
	{
		if (bShouldStop) break;

		// Step 1: Collect all position samples for this entity across all timesteps
		// We store (timestep index, position) pairs for later sorting
		TArray<TPair<int32, FVector>> EntityPositions;

		for (auto& Pair : *AgentMovementInfoData.SimulationData)
		{
			for (const FSimMovementSample& Sample : Pair.Value)
			{
				if (Sample.EntityID == EntityIdx)
				{
					EntityPositions.Add(TPair<int32, FVector>(Pair.Key, Sample.Position));
					break; // Only one sample per entity per timestep, so we can exit early
				}
			}
		}

		// Need at least 2 positions to calculate any speed
		if (EntityPositions.Num() < 2) continue;

		// Step 2: Sort positions by timestep to ensure chronological order
		EntityPositions.Sort([](const TPair<int32, FVector>& A, const TPair<int32, FVector>& B)
		{
			return A.Key < B.Key;
		});

		// Step 3: Calculate speed for each timestep using look-ahead to next position
		float LastValidSpeed = 0.0f;

		for (int32 i = 0; i < EntityPositions.Num(); ++i)
		{
			if (bShouldStop) break;

			float Speed = LastValidSpeed;  // Default to previous speed (used for last frame)

			// Calculate speed from current position to next position
			if (i < EntityPositions.Num() - 1)
			{
				FVector Delta = EntityPositions[i + 1].Value - EntityPositions[i].Value;
				float Distance = Delta.Size();  // Distance in cm (positions are converted to cm during loading)

				// Speed formula: speed (m/s) = distance (cm) / (time (s) * 100)
				// The * 100 converts cm to m
				Speed = Distance / (TimeBetweenSteps * 100.0f);
				LastValidSpeed = Speed;
			}
			// For the last frame, we keep LastValidSpeed (carry forward previous speed)

			// Step 4: Update the speed value in the actual simulation data
			int32 Timestep = EntityPositions[i].Key;
			for (FSimMovementSample& Sample : (*AgentMovementInfoData.SimulationData)[Timestep])
			{
				if (Sample.EntityID == EntityIdx)
				{
					Sample.Speed = Speed;
					break;
				}
			}
		}

		// Report progress to the subsystem for UI feedback (every 100 entities)
		if (EntityIdx % 100 == 0)
		{
			float CurrentPercentage = static_cast<float>(EntityIdx) / static_cast<float>(MaxAgents);
			if (UAgentDataSubsystem* Subsys = OwnerSubsystem.Get())
			{
				Subsys->ProgressQueue.Enqueue(CurrentPercentage);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Finished calculating speed from movement"));
}

void FProcessAgentSimulationDataRunnable::FinalizeProgress()
{
	// TODO: CHECK IF ANY HDF5 SPECIFIC FINALIZATION IS NEEDED
	if (bShouldStop)
	{
		return;
	}
	UAgentDataSubsystem* Subsys = OwnerSubsystem.Get();

	// Perform Animation Preprocessing data here
	// Broadcast the current percentage of the data loaded
	if (Subsys)
	{
		Subsys->ProgressQueue.Enqueue(1.0f);
		Subsys->ProgressQueue.Enqueue(0.0f);// TODO: NEED to broadcast new load text here
	}

	if (bShouldStop)
	{
		return;
	}
	if (Subsys)
	{
		Subsys->LoadingTaskQueue.Enqueue(TEXT("Calculating Smoothed Step Movement Brackets..."));
	}

	CalcSmoothedStepMovementBrackets(AgentDataArray);

	if (bShouldStop)
	{
		return;
	}

	// Broadcast that the simulation data has been loaded -- this is done on the game thread
	if (Subsys)
	{
		if (bShouldStop)
		{
			return;
		}
		Subsys->ProgressQueue.Enqueue(1.0f);
		Subsys->bIsDataLoaded = true; // Set the flag to indicate that the data has been loaded
	}
}

uint32 FProcessAgentSimulationDataRunnable:: Run()
{
	bIsRunning = true;

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot SnapRunStart = FMobiusMemSnapshot::Take(TEXT("Run_Start"));
	SnapRunStart.LogAbsolute();
#endif

	UAgentDataSubsystem* Subsys = OwnerSubsystem.Get(); //TODO: this may need to be a weak ptr check/and/or variable
	// Broadcast the current percentage of the data loaded
	if (Subsys)
	{
		Subsys->ProgressQueue.Enqueue(0.0f);

		// First loading task
		Subsys->LoadingTaskQueue.Enqueue(TEXT("Loading Simulation Data From File..."));
	}

	// TODO: this has no way to know progress of loading file at the moment so we need to think on how to do this better
	if (!LoadFileAndDeserialize())
	{
		bIsRunning = false;
		return 0;
	}

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot::Take(TEXT("Run_AfterDeserialize")).LogDelta(SnapRunStart);
#endif

	bool bCalculateTimeBetweenSteps = true;
	bool bCalculateMaxTime = true;

	if (Subsys)
	{
		Subsys->LoadingTaskQueue.Enqueue(TEXT("Processing Simulation Metadata..."));
	}

	ProcessMetadata(bCalculateTimeBetweenSteps, bCalculateMaxTime);

	if (Subsys)
	{
		Subsys->MaxAgentsQueue.Enqueue(MaxAgents);
	}

	if (bShouldStop)
	{
		return 0;
	}

	// Size AgentDataArray to the max agents
	AgentDataArray.SetNum(MaxAgents);

	if (bShouldStop)
	{
		return 0;
	}
	if (Subsys)
	{
		Subsys->LoadingTaskQueue.Enqueue(TEXT("Parsing Simulation Data..."));
	}

	// Run the main simulation loop
#if !UE_BUILD_SHIPPING
	const double GatherLoopStart = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("Agent data gather loop start"));
#endif
	RunSimulationDataGatheringLoop(bCalculateTimeBetweenSteps, bCalculateMaxTime);
#if !UE_BUILD_SHIPPING
	UE_LOG(LogTemp, Display, TEXT("Agent data gather loop finish %.3f ms"), (FPlatformTime::Seconds() - GatherLoopStart) * 1000.0);
#endif

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot::Take(TEXT("Run_AfterGatherLoop")).LogDelta(SnapRunStart);
#endif

	if (bShouldStop)
	{
		return 0;
	}

	// Calculate rotation from movement if rotation data is missing
	if (!AgentSimulationData.Metadata.bHasRotationData)
	{
		if (Subsys)
		{
			Subsys->LoadingTaskQueue.Enqueue(TEXT("Calculating Rotation From Movement..."));
		}
		CalculateRotationFromMovement();
	}

	if (bShouldStop)
	{
		return 0;
	}

	// Calculate speed from movement if speed data is missing
	if (!AgentSimulationData.Metadata.bHasSpeedData)
	{
		if (Subsys)
		{
			Subsys->LoadingTaskQueue.Enqueue(TEXT("Calculating Speed From Movement..."));
		}
		CalculateSpeedFromMovement();
	}

	if (bShouldStop)
	{
		return 0;
	}

	// Free raw sample buffer now; RunSimulationDataGatheringLoop has fully consumed it.
	// AgentSimulationData.Entities is moved to CachedEntityData on the game thread.
	AgentSimulationData.Samples.Empty();
	AgentSimulationData.Samples.Shrink();
	SimulationDataFile.Empty();
#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot::Take(TEXT("Run_AfterSamplesFree")).LogDelta(SnapRunStart);
#endif

	// Send the final progress and completion events
	FinalizeProgress();

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot::Take(TEXT("Run_FinalizeComplete")).LogDelta(SnapRunStart);
#endif

	bIsRunning = false;
	return 0; // return 0 to indicate that the thread has ended
}
void FProcessAgentSimulationDataRunnable::Stop()
{
	bShouldStop = true;
}
void FProcessAgentSimulationDataRunnable::Exit()
{
#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot SnapExitStart = FMobiusMemSnapshot::Take(TEXT("Exit_Start"));
	SnapExitStart.LogAbsolute();
#endif

	// as the runnable contains multiple properties that are not handled by garbage collection,
	// we need to ensure that we clean up properly
	// AgentSimulationData.Entities, AgentMovementInfoData.SimulationData and NumOfAgentsPerTimeStep
	// are consumed by the game thread in BuildPedestrianMovementFragmentData after the
	// OnLoadSimulationDataComplete broadcast. UE calls Exit() on the worker thread right
	// after Run() returns, which can race ahead of a GT stall (e.g. FBX mesh build on
	// CreateMeshSection_LinearColor) — freeing them here would null the shared fragment
	// and leave PedestrianInitializeMOP stuck on "CurrentTimeStep not valid". The TUniquePtr
	// destructor in AgentDataRunnableCleanup releases everything naturally on the next load.
	AgentSimulationData.Samples.Empty();
	AgentSimulationData.Samples.Shrink();
	AgentSimulationData.Metadata = FMobiusAgentSimulationMetadata();
	AgentFileFormat = EMobiusAgentFileFormat::Unknown;

	AgentDataArray.Empty();
	AgentDataArray.Shrink();

	EmbAvatarAnims.Empty();
	EmbAvatarAnims.Shrink();

	StepVectors.Empty();
	StepVectors.Shrink();

	bReadyToDelete = true; // Set the flag to true to indicate that the runnable is ready to be deleted

#if !UE_BUILD_SHIPPING
	FMobiusMemSnapshot::Take(TEXT("Exit_Complete")).LogDelta(SnapExitStart);
#endif
}

TArray<FSimMovementSample> FProcessAgentSimulationDataRunnable::GetMovementSamples(int32 AgentID)
{
	TArray<FSimMovementSample> MovementSamples;

	// check if the agent id exceeds the max agents
	if (!AgentMovementInfoData.SimulationData.IsValid() || AgentID >= AgentMovementInfoData.SimulationData->Num())
	{
		// throw error message
	}
	else
	{
		// loop through the simulation data and get the movement samples
		for (int32 i = 0; i < AgentMovementInfoData.SimulationData->Num(); i++)
		{
			if (bShouldStop) break;
			// loop through the movement samples for this time step
			for (FSimMovementSample MovementSample : (*AgentMovementInfoData.SimulationData)[i])
			{
				if (bShouldStop) break;
				if (MovementSample.EntityID == AgentID)
				{
					MovementSamples.Add(MovementSample);
				}
			}
		}
	}

	return MovementSamples;
}

void FProcessAgentSimulationDataRunnable::CalcSmoothedStepMovementBrackets(const TArray<FAgentData>& AgentSamples)
{
	// Build O(1) lookup: SampleIndex[timestep][entityID] -> FSimMovementSample*
	// This replaces the O(S) linear scan per SetAnimPt call with O(1) hash lookup
	TMap<int32, TMap<int32, FSimMovementSample*>> SampleIndex;
	SampleIndex.Reserve(AgentMovementInfoData.SimulationData->Num());
	for (auto& Pair : *AgentMovementInfoData.SimulationData)
	{
		if (bShouldStop) break;
		TMap<int32, FSimMovementSample*>& IndexMap = SampleIndex.Add(Pair.Key);
		IndexMap.Reserve(Pair.Value.Num());
		for (FSimMovementSample& Sample : Pair.Value)
		{
			IndexMap.Add(Sample.EntityID, &Sample);
		}
	}
	if (bShouldStop) return;

	// Reuse allocation across agents to avoid per-agent heap churn
	TArray<FVector> RecordVectors;

	// Loop through the agentsData, calculating the vectors for each agent
	for (int a = 0; a < AgentSamples.Num(); a++)
	{
		if (bShouldStop) break;
		CalculateSrcVectors(RecordVectors, AgentSamples[a]); // Calculate the short-time source vectors for the agent

		// Lambda for O(1) indexed write to SimulationData (replaces SetAnimPt linear scan).
		// A2: only MovementBracket is stored now. The former per-sample StepDurationMS write was removed — that
		// field moved off FSimMovementSample into FSimSampleStepMotion because nothing consumed it. The step
		// duration is still computed locally below (it drives the rolling tSpan gait window via newtSpan), it is
		// just no longer persisted per sample. To re-enable, see FSimSampleStepMotion in SimulationFragment.h.
		auto WriteAnimData = [&SampleIndex, a](int32 TimeStep, EPedestrianMovementBracket emb)
		{
			if (TMap<int32, FSimMovementSample*>* TimeMap = SampleIndex.Find(TimeStep))
			{
				if (FSimMovementSample** SamplePtr = TimeMap->Find(a))
				{
					(*SamplePtr)->MovementBracket = emb;
				}
			}
		};

		FVector StepVector = FVector::ZeroVector;
		int t = 0, tSpan = 1;
		float stepDuration = 0.0f;

		// Iterate t through all recordVectors, rapidly moving through the initial zero-speed records
		for (t = 0; t < RecordVectors.Num() && (RecordVectors[t].Length()/(timeDurationPerRecord) < MinSpeedWalking); t++) {
			if (bShouldStop) break;
			WriteAnimData(t, EPedestrianMovementBracket::Emb_NotMoving);
		}
		if (bShouldStop) break;
		// Calculate the sum-vector speed for the next rolling block of timed records to more accurately estimate gait speed
		// Note: we increase and decrease tSpan (rough timesteps in a step) depending on the required step duration
		//
		// StepVector is the sum of the vectors from index t to t+tSpan (the rolling sum-vector for a following estimated step)
		double stepSpeed = RecordVectors[t].Length()/(timeDurationPerRecord);
		tSpan = minTimedSrcRecordsForStep;
		AddManyVectors(RecordVectors, t, tSpan, StepVector); // Starting to move from zero, so animate the step that we are starting to take

		// Now, we have a meaningful speed, so we can start calculating the proceeding records as part of moving steps
		// Iterate from t to the end of the recordVectors, calculating the sum-vector for the next step duration
		for (; t < RecordVectors.Num(); t++) {
			if (bShouldStop) break;
			stepSpeed = StepVector.Length()/(static_cast<double>(tSpan) * timeDurationPerRecord) / 100; // This 100 value is coming from the isSI conversion
			EPedestrianMovementBracket thisAnimMF = CalculateStepAnimationParams(static_cast<float>(stepSpeed), stepDuration);
			WriteAnimData(t, thisAnimMF);

			// Move the step forward by one record, by subtracting the last vector and adding the new one
			StepVector -= RecordVectors[t]; // subtract this record single vector, ahead of the next step assessment, from t+1

			int newtSpan = static_cast<int>(std::round(stepDuration / timeDurationPerRecord));

			// Reduce tSpan? if the new tSpan is less than current one. No need to adjust the vector sum, as we just removed record[t]
			if ((newtSpan < tSpan) && (tSpan > minTimedSrcRecordsForStep)) {
				tSpan--;
			}
			else // Assess increasing tSpan, if required, and within limits
			{
				if ((newtSpan > tSpan) && (tSpan < maxTimedSrcRecordsForStep) && (t + 1 + tSpan < RecordVectors.Num())) {
					StepVector += RecordVectors[t + tSpan]; // add the next vector to the sum-vector
					tSpan++; // increase the span if the step duration expected is longer than the current span
				}

				if (t + tSpan < RecordVectors.Num()) {
					StepVector += RecordVectors[t + tSpan]; // add the new vector to the sum-vector
				}
				else tSpan--; // reduce the span if we are at the end of the recordVectors
			}
		}

		if (bShouldStop) break;
		//Calculate the current percentage of the data loaded
		float CurrentPercentage = static_cast<float>(a) / static_cast<float>(MaxAgents);
		// Broadcast the current percentage of the data loaded
		if (UAgentDataSubsystem* Subsys = OwnerSubsystem.Get())
		{
			Subsys->ProgressQueue.Enqueue(CurrentPercentage);
		}
	}
}

int FProcessAgentSimulationDataRunnable::CalculateSrcVectors(TArray<FVector>& Vec3D, const FAgentData& Sample)
{
	// Always set logical size so reused array has correct Num() for this agent
	Vec3D.SetNum(Sample.MovementData.Num(), EAllowShrinking::No);
	if (Sample.MovementData.Num() > 2)
	{
		int i = 0;
		for (; i < Sample.MovementData.Num() - 1; i++)
		{
			Vec3D[i] = Sample.MovementData[i].Location - Sample.MovementData[i + 1].Location;
		}
		Vec3D[i] = Vec3D[i - 1]; // set the last vector to the previous vector to avoid out of bounds error
	}
	return (int)Sample.MovementData.Num();
}
void FProcessAgentSimulationDataRunnable::AddManyVectors(TArray<FVector>& Vec3D, int TStartStep, int TSpanStepPts, FVector& SumVec)
{
	for (int i = TStartStep; i < TStartStep + TSpanStepPts; i++){
		SumVec = SumVec + Vec3D[i];
	}
}

EPedestrianMovementBracket FProcessAgentSimulationDataRunnable::CalculateStepAnimationParams(float CurrentSpeed, float& StepsPerSecond)
{
	FVatMovementFrames Band = AvatarGaitSpeedBands[5]; // Default to the last band
	// Fast loop through the GaitSpeedBands, testing CurrentSpeed against the HighVal, in ascending order, to assign the MovementBracket
	// We have assumed that these gait parameters apply to avatars of 1.72m height
	int iBracket = 0;
	for (; iBracket < (sizeof(AvatarGaitSpeedBands) / sizeof(FVatMovementFrames) - 1); iBracket++) {
		if (CurrentSpeed < AvatarGaitSpeedBands[iBracket].HighSpeed) {
			break;
		}
	}

	StepsPerSecond = AvatarGaitSpeedBands[iBracket].AnimatedStepLength / CurrentSpeed;

	calculatedStepAnimationParams = true;

	return AvatarGaitSpeedBands[iBracket].MovementBracket;
}
