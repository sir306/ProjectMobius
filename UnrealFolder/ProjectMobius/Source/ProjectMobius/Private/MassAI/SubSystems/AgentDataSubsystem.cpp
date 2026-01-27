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

#include "HeatmapVisualization.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Interfaces/ProjectMobiusInterface.h"
#include "SubSystems/TimeDilationSubSystem.h"
#include "HeatmapVisualization/Public/QuadTree.h"
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
#include "Subsystems/LoadingSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"

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


void UAgentDataSubsystem::ParseEntityInfo(const TSharedPtr<FJsonObject>& InJsonObject, FEntityInfoFragment& OutInfo)
{
	if (!InJsonObject.IsValid())
	{
		return;
	}

	OutInfo.EntityID = InJsonObject->GetIntegerField(StringCast<TCHAR>("id"));
	OutInfo.EntityName = InJsonObject->GetStringField(StringCast<TCHAR>("name"));
	OutInfo.EntitySimTimeS = InJsonObject->GetStringField(StringCast<TCHAR>("simTimeS"));
	OutInfo.EntityMaxSpeed = InJsonObject->GetNumberField(StringCast<TCHAR>("max_speed"));
	OutInfo.EntityM_Plane = InJsonObject->GetStringField(StringCast<TCHAR>("m_plane"));
	OutInfo.EntityMap = InJsonObject->GetIntegerField(StringCast<TCHAR>("map"));
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
		// if (JSONObject == nullptr)
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
	if (JsonDataRunnable)
	{
		JsonDataRunnable->Stop();
		JsonDataRunnable->Exit();
		JsonDataRunnable.Reset();
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

void UAgentDataSubsystem::GetJSONDataFile(FString InJsonDataFile)
{
	if (!CheckFilePathExists(InJsonDataFile))
	{
		UE_LOG(LogTemp, Warning, TEXT("File Path does not exist"));
		ReportAgentDataError(this,
		                     TEXT("Simulation file not found"),
		                     FString::Printf(TEXT("JSON file does not exist: %s"), *InJsonDataFile),
		                     TEXT("AgentDataSubsystem"));
		return;
	}
	TSharedRef<TJsonReader<TCHAR>> JSONReader = TJsonReaderFactory<TCHAR>::Create(JSONDataString);

	// Create JSON Reader and load String
	CreateJsonReaderAndString(JSONDataString, JSONReader, InJsonDataFile);

	// Deserialize JSON Data
	bool bDeserializeSuccess = FJsonSerializer::Deserialize(JSONReader, JSONObject);

	if (!bDeserializeSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to Deserialize JSON Data"));
		ReportAgentDataError(this,
		                     TEXT("Failed to parse simulation file"),
		                     FString::Printf(TEXT("Failed to deserialize JSON data from: %s"), *InJsonDataFile),
		                     TEXT("AgentDataSubsystem"));
		return;
	}
}

void UAgentDataSubsystem::GetUpdatedJSONDataFile()
{
	// log the file has changed
	UE_LOG(LogTemp, Warning, TEXT("Data File Changed"));

	// Get the Game Instance 
	if(UProjectMobiusGameInstance* GameInst = GetMobiusGameInstance(GetWorld()))
	{
		// update the data file
		JSONDataFile = GameInst->GetPedestrianDataFilePath();

		// Get the JSON Data File
		GetJSONDataFile(JSONDataFile);
		
		// Check that json object is not still nullptr
		if (JSONObject == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("JSON Object is nullptr"));
			ReportAgentDataError(this,
			                     TEXT("Simulation file invalid"),
			                     TEXT("JSON object is null after loading the simulation file."),
			                     TEXT("AgentDataSubsystem"));
		}
		else
		{
			//CalculateMaxEntitiesPermitted();
		}
	}
	else
	{
		ReportAgentDataError(this,
		                     TEXT("Game instance missing"),
		                     TEXT("Unable to access the game instance while loading simulation data."),
		                     TEXT("AgentDataSubsystem"));
	}
	
}

void UAgentDataSubsystem::BuildPedestrianAgentInfo()
{
	if (!JSONObject.IsValid())
	{
		ReportAgentDataError(this,
		                     TEXT("Simulation data missing"),
		                     TEXT("No JSON data is loaded for pedestrian info."),
		                     TEXT("AgentDataSubsystem"));
		return;
	}
	TArray<TSharedPtr<FJsonValue>> JsonEntityDataArray = JSONObject->GetArrayField(StringCast<TCHAR>("entities"));

	// loop through the JSON array
	for (int32 entityIndex = 0; entityIndex < JsonEntityDataArray.Num(); entityIndex++)
	{
		if (!JsonEntityDataArray[entityIndex]->AsObject().IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Invalid JSON Object"));
			ReportAgentDataError(this,
			                     TEXT("Invalid entity data"),
			                     TEXT("Encountered an invalid entity object while parsing."),
			                     TEXT("AgentDataSubsystem"));
			break;
		}

		// Get the JSON object for this 
		TSharedPtr<FJsonObject> JSONEntityDataObject = JsonEntityDataArray[entityIndex]->AsObject();

		FEntityInfoFragment EntityInfo;
		ParseEntityInfo(JSONEntityDataObject, EntityInfo);
		
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

	// Check if we have HDF5 data loaded via the runnable
	if (JsonDataRunnable && JsonDataRunnable->SimulationFileType == ESimulationFileType::ESFT_HDF5)
	{
		const TArray<FHdf5EntityData>& Entities = JsonDataRunnable->Hdf5Data.Entities;
		if (!Entities.IsValidIndex(Index))
		{
			ReportAgentDataError(this,
			                     TEXT("Entity data missing"),
			                     TEXT("Entity index is not present in the HDF5 data."),
			                     TEXT("AgentDataSubsystem"));
			return;
		}

		const FHdf5EntityData& Entity = Entities[Index];
		EntityInfoFragToUpdate.EntityID = Entity.Id;
		EntityInfoFragToUpdate.EntityName = Entity.Name;
		EntityInfoFragToUpdate.EntitySimTimeS = FString::SanitizeFloat(Entity.SimTimeS);
		EntityInfoFragToUpdate.EntityMaxSpeed = Entity.MaxSpeed;
		EntityInfoFragToUpdate.EntityM_Plane = Entity.MPlane;
		EntityInfoFragToUpdate.EntityMap = Entity.Map;
		return;
	}

	// Fall back to JSON data
	if (!JSONObject.IsValid())
	{
		ReportAgentDataError(this,
		                     TEXT("Simulation data missing"),
		                     TEXT("No simulation data is loaded for entity info."),
		                     TEXT("AgentDataSubsystem"));
		return;
	}

	TArray<TSharedPtr<FJsonValue>> JsonEntityDataArray = JSONObject->GetArrayField(StringCast<TCHAR>("entities", 8));
	if (!JsonEntityDataArray.IsValidIndex(Index))
	{
		ReportAgentDataError(this,
		                     TEXT("Entity data missing"),
		                     TEXT("Entity index is not present in the JSON data."),
		                     TEXT("AgentDataSubsystem"));
		return;
	}

	// Get the JSON object for this
	TSharedPtr<FJsonObject> JSONEntityDataObject = JsonEntityDataArray[Index]->AsObject();

	ParseEntityInfo(JSONEntityDataObject, EntityInfoFragToUpdate);
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

	// Check if we have HDF5 data loaded via the runnable
	if (JsonDataRunnable && JsonDataRunnable->SimulationFileType == ESimulationFileType::ESFT_HDF5)
	{
		const TArray<FHdf5EntityData>& Entities = JsonDataRunnable->Hdf5Data.Entities;
		if (!Entities.IsValidIndex(Index))
		{
			ReportAgentDataError(this,
			                     TEXT("Entity data missing"),
			                     TEXT("Entity index is not present in the HDF5 data."),
			                     TEXT("AgentDataSubsystem"));
			return;
		}

		AgentName = Entities[Index].Name;
	}
	else
	{
		// Fall back to JSON data
		if (!JSONObject.IsValid())
		{
			ReportAgentDataError(this,
			                     TEXT("Simulation data missing"),
			                     TEXT("No simulation data is loaded for entity rendering."),
			                     TEXT("AgentDataSubsystem"));
			return;
		}

		TArray<TSharedPtr<FJsonValue>> JsonEntityDataArray = JSONObject->GetArrayField(StringCast<TCHAR>("entities", 8));
		if (!JsonEntityDataArray.IsValidIndex(Index))
		{
			ReportAgentDataError(this,
			                     TEXT("Entity data missing"),
			                     TEXT("Entity index is not present in the JSON data."),
			                     TEXT("AgentDataSubsystem"));
			return;
		}

		// Get the JSON object for this
		TSharedPtr<FJsonObject> JSONEntityDataObject = JsonEntityDataArray[Index]->AsObject();

		if (!JSONEntityDataObject.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Invalid JSON Object"));
			ReportAgentDataError(this,
			                     TEXT("Invalid entity data"),
			                     TEXT("Entity object is invalid while parsing rendering data."),
			                     TEXT("AgentDataSubsystem"));
			return;
		}
		AgentName = JSONEntityDataObject->GetStringField(StringCast<TCHAR>("name", 4));
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

FProcessSimulationDataRunnable::FProcessSimulationDataRunnable(FString InJsonDataFile, TWeakObjectPtr<UAgentDataSubsystem> Owner)
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
	SimulationDataFilePath = InJsonDataFile;
	// check file actually exists before creating the thread
	if (SimulationDataFilePath.IsEmpty() || !FPaths::FileExists(SimulationDataFilePath))
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
		                              TEXT("Simulation file not found"),
		                              FString::Printf(TEXT("JSON file does not exist: %s"), *SimulationDataFilePath),
		                              TEXT("AgentDataSubsystem"));
		return;
	}

	
	
	// Create the thread -- The thread priority is set to TPri_Normal this may need to be adjusted based on the application
	Thread = FRunnableThread::Create(this, TEXT("FProcessSimulationDataRunnable"), 0, TPri_Normal);
}

FProcessSimulationDataRunnable::~FProcessSimulationDataRunnable()
{
	// ensure you’ve called Stop() first
	if (Thread)
	{
		Thread->WaitForCompletion();  // safe join off-thread
		delete Thread;
		Thread = nullptr;
	}
	AgentMovementInfoData = FSimulationFragment();
	JSONObject = nullptr;

	// DEBUG: Prove the runnable has been deleted
	// AsyncTask(ENamedThreads::GameThread, []()
	// 	{
	//
	// 		UE_LOG(LogTemp, Warning, TEXT("Runnable Deleted"));
	// 	});
}

bool FProcessSimulationDataRunnable::LoadFileAndDeserialize()
{
	// check file actually exists before creating the thread
	if (!FPaths::FileExists(SimulationDataFilePath))
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
		                              TEXT("Simulation file path not found"),
		                              FString::Printf(TEXT("File path does not exist: %s"), *SimulationDataFilePath),
		                              TEXT("AgentDataSubsystem"));
		bShouldStop = true;
		return false;
	}
	
	// Ensure the simulation file type is set to unknown before loading - successful loading and deserializing will set this to the corresponding file type
	SimulationFileType = ESimulationFileType::ESFT_Unknown;
	//TODO: should really be doing equal not compare 
	// check what the extension of the file is: JSON ? HDF5
	if (FPaths::GetExtension(SimulationDataFilePath).Compare(FString("json"), ESearchCase::Type::IgnoreCase) == 0) // FPaths::GetExtension().Compare() returns 0 if equal!
	{
		return LoadAndDeserializeJSONFile();
	}
	if (FPaths::GetExtension(SimulationDataFilePath).Compare(FString("h5"), ESearchCase::Type::IgnoreCase) == 0) // FPaths::GetExtension().Compare() returns 0 if equal!
	{
		return LoadAndDeserializeHDF5File();
	}
	else
	{
		// invalid format
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
		                              TEXT("Unsupported simulation file format"),
		                              FString::Printf(TEXT("File format not supported: %s"), *SimulationDataFilePath),
		                              TEXT("AgentDataSubsystem"));
		bShouldStop = true;
		return false;
	}
}

bool FProcessSimulationDataRunnable::LoadAndDeserializeJSONFile()
{
	// Load File to String
	if (!FFileHelper::LoadFileToString(SimulationDataFile, *SimulationDataFilePath))
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
									  TEXT("Failed to read simulation file"),
									  FString::Printf(TEXT("Unable to read JSON data from: %s"), *SimulationDataFilePath),
									  TEXT("AgentDataSubsystem"));
		bShouldStop = true;
		return false;
	}

	// Create JSON Reader
	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(SimulationDataFile);

	JSONObject.Reset();
	
	// Deserialize JSON Data
	bool bDeserializeSuccess = FJsonSerializer::Deserialize(JsonReader, JSONObject);

	// if the deserialization was not successful, log it
	if (!bDeserializeSuccess)
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
									  TEXT("Failed to parse simulation file"),
									  FString::Printf(TEXT("Failed to deserialize JSON data from: %s"), *SimulationDataFilePath),
									  TEXT("AgentDataSubsystem"));
		bShouldStop = true;
		return false;
	}
	
	// if successful we can set the simulation file type
	SimulationFileType = ESimulationFileType::ESFT_JSON;

	return true;
}

bool FProcessSimulationDataRunnable::LoadAndDeserializeHDF5File()
{
	if (!HDF5SimulationReader.OpenFile(*SimulationDataFilePath))
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
		                              TEXT("Failed to read simulation file"),
		                              FString::Printf(TEXT("Unable to read HDF5 data from: %s"), *SimulationDataFilePath),
		                              TEXT("AgentDataSubsystem"));
		bShouldStop = true;
		return false;
	}
	// clear old data
	Hdf5Data = FHdf5SimulationData();
	
	// Read the HDF5 data into our Hdf5Data structure
	// MetaData read
	HDF5SimulationReader.ReadMetadata(Hdf5Data.Meta);
	// Entities read
	HDF5SimulationReader.ReadEntities(Hdf5Data.Entities);
	// Samples read
	HDF5SimulationReader.ReadAllSamples(Hdf5Data.Samples);
	
	// finished reading
	HDF5SimulationReader.CloseFile();
	
	// if successful we can set the simulation file type
	SimulationFileType = ESimulationFileType::ESFT_HDF5;
	
	return true;
}

void FProcessSimulationDataRunnable::ProcessMetadata(bool& bCalculateTimeBetweenSteps, bool& bCalculateMaxTime)
{
	bCalculateTimeBetweenSteps = true;
	bCalculateMaxTime = true;
	
	// Check what file we are working with
	switch (SimulationFileType)
	{
	case ESFT_Unknown:
		// shouldn't happen but will add error stuff later
		break;
	case ESFT_JSON:
		ReadJSONMetadataValues(bCalculateTimeBetweenSteps, bCalculateMaxTime);
		break;
	case ESFT_HDF5:
		ReadHDF5MetadataValues(bCalculateTimeBetweenSteps, bCalculateMaxTime);
		break;
	case ESFT_MAX:
		// shouldn't happen but will add error stuff later
		break;
	}
}

void FProcessSimulationDataRunnable::ReadJSONMetadataValues(bool& bCalculateTimeBetweenSteps, bool& bCalculateMaxTime)
{
	if (!JSONObject.IsValid())
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
		                              TEXT("Simulation data missing"),
		                              TEXT("JSON data was not loaded before processing metadata."),
		                              TEXT("AgentDataSubsystem - FProcessSimulationDataRunnable::ReadJSONMetadataValues"));
		bShouldStop = true;
		return;
	}

	// See if the metadata object is present and valid in this file
	if(JSONObject->HasField(StringCast<TCHAR>("metadata")))
	{
		// Get the metadata object
		TSharedPtr<FJsonObject> JSONMetaDataObject = JSONObject->GetObjectField(StringCast<TCHAR>("metadata"));

		/**
		 * The way mass entity spawns we need to use the actual number and not the index value,
		 * the assignment of index values is done in the observor processors
		 */

		// check if the metadata field for max num entities is present and not blank
		if(!JSONMetaDataObject->TryGetNumberField(StringCast<TCHAR>("max_num_entities"), MaxAgents))
		{
			// Set the entity count from count of entities in the JSON object array if the metadata fields are not present or blank
			MaxAgents = JSONObject->GetArrayField(StringCast<TCHAR>("entities")).Num();
		}

		/** we see if the metadata has the sampling rate field and the duration field,
		 * it is also important to check that they are not blank or 0
		 * so we can calculate the number of samples and get the time between steps */
		if(JSONMetaDataObject->HasField(StringCast<TCHAR>("duration")) && JSONMetaDataObject->HasField(StringCast<TCHAR>("sampling_rate")) &&
			JSONMetaDataObject->GetNumberField(StringCast<TCHAR>("duration")) > 0 && JSONMetaDataObject->GetNumberField(StringCast<TCHAR>("sampling_rate")) > 0)
		{
			// Get the duration of the simulation
			AgentMovementInfoData.MaxTime = JSONMetaDataObject->GetNumberField(StringCast<TCHAR>("duration"));

			// Get the sampling rate of the simulation
			TimeBetweenSteps = JSONMetaDataObject->GetNumberField(StringCast<TCHAR>("sampling_rate"));

			// Calculate the number of samples
			TargetDataCount = AgentMovementInfoData.MaxTime / TimeBetweenSteps;

			// don't calculate the time between steps
			bCalculateTimeBetweenSteps = false;

			// don't calculate the max time
			bCalculateMaxTime = false;
		}
		else
		{
			// Set the target count to the simulation array count
			TargetDataCount = JSONObject->GetArrayField(StringCast<TCHAR>("simulation")).Num();
		}
	}
	else
	{
		// Set the entity count from count of entities in the JSON object array if the metadata fields are not present or blank
		MaxAgents = JSONObject->GetArrayField(StringCast<TCHAR>("entities")).Num();
	}
}

void FProcessSimulationDataRunnable::ReadHDF5MetadataValues(bool& bCalculateTimeBetweenSteps, bool& bCalculateMaxTime)
{
	// Check the hdf5 data object is valid, there should be samples to be classed valid
	if (Hdf5Data.Samples.Num() == 0)// while sample numbers can be less than expected max time step, it can't be empty
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
									  TEXT("Simulation data missing"),
									  TEXT("HDF5 data was not processed due to missing simulation samples."),
									  TEXT("AgentDataSubsystem - FProcessSimulationDataRunnable::ReadHDF5MetadataValues"));
		bShouldStop = true;
		return;
	}
	
	// Did the HDF5 data contain metadata
	if (Hdf5Data.Meta != FHdf5SimulationMetadata())//TODO: need to improve this equality check logic
	{
		// if not 0 then we have data
		if (Hdf5Data.Meta.MaxNumEntities != 0)
		{
			MaxAgents = Hdf5Data.Meta.MaxNumEntities;
		}
		if (Hdf5Data.Meta.Duration > 0 && Hdf5Data.Meta.SamplingRate > 0)
		{
			// get the simulation metadata
			AgentMovementInfoData.MaxTime = Hdf5Data.Meta.Duration;
			// get the sampling rate of the simulation from the metadata
			TimeBetweenSteps = Hdf5Data.Meta.SamplingRate;
			
			// Calculate the number of samples
			TargetDataCount = AgentMovementInfoData.MaxTime / TimeBetweenSteps;
			
			// don't calculate the time between steps
			bCalculateTimeBetweenSteps = false;

			// don't calculate the max time
			bCalculateMaxTime = false;
		}
		else
		{
			if (Hdf5Data.Meta.SamplingRate > 0)
			{
				// get the sampling rate of the simulation from the metadata
				TimeBetweenSteps = Hdf5Data.Meta.SamplingRate;
			}
			if (Hdf5Data.Meta.Duration > 0)
			{
				// get the simulation metadata
				AgentMovementInfoData.MaxTime = Hdf5Data.Meta.Duration;
			}
			
			// Set the target count to the simulation array count - as we know there is simulation data
			TargetDataCount = Hdf5Data.Samples.Num();
		}
		
	}
	else
	{
		// Set the entity count from count of entities in the hdf5 Entities array if the metadata fields are not present or blank
		MaxAgents = Hdf5Data.Entities.Num();
	}
	
}

void FProcessSimulationDataRunnable::RunSimulationDataGatheringLoop(bool bCalculateTimeBetweenSteps, bool bCalculateMaxTime)
{
	// TODO: refactor this to handle HDF5 data as well
	// Check what file we are working with
	switch (SimulationFileType)
	{
	case ESFT_Unknown:
		// shouldn't happen but will add error stuff later
		break;
	case ESFT_JSON:
		RunJsonSimDataGatheringLoop(bCalculateTimeBetweenSteps, bCalculateMaxTime);
		break;
	case ESFT_HDF5:
		RunHdf5SimDataGatheringLoop(bCalculateTimeBetweenSteps, bCalculateMaxTime);
		break;
	case ESFT_MAX:
		// shouldn't happen but will add error stuff later
		break;
	}
}

void FProcessSimulationDataRunnable::RunJsonSimDataGatheringLoop(bool bCalculateTimeBetweenSteps,
	bool bCalculateMaxTime)
{
	if (!JSONObject.IsValid())
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
		                              TEXT("Simulation data missing"),
		                              TEXT("JSON data was not loaded before processing simulation steps."),
		                              TEXT("AgentDataSubsystem - FProcessSimulationDataRunnable::RunSimulationDataGatheringLoop"));
		bShouldStop = true;
		return;
	}
	// get the simulation data array
	TArray<TSharedPtr<FJsonValue>> JsonSimDataArray = JSONObject->GetArrayField(StringCast<TCHAR>("simulation"));
	
	// reserve the num of sim data arrays for the num of agents per time step
	NumOfAgentsPerTimeStep.Reserve(JsonSimDataArray.Num());

	// keep looping until the thread is stopped or the current data count is equal to the target data count
	while (!bShouldStop && CurrentDataCount <= TargetDataCount)
	{
		// Check that the JSON Object is valid
		if (!JsonSimDataArray.IsValidIndex(CurrentDataCount) || !JsonSimDataArray[CurrentDataCount]->AsObject().IsValid())
		{
			ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
			                              TEXT("Invalid simulation step"),
			                              TEXT("Simulation data contains an invalid timestep object. "
				                              "Meaning the simulation data may be corrupted or incomplete. "
				                              "Simulation will still attempt to load available data, "
				                              "but could cause unexpected behavior."),
			                              TEXT("AgentDataSubsystem"));
			break;
		}

		// Get the JSON object for this
		TSharedPtr<FJsonObject> JSONSimDataObject = JsonSimDataArray[CurrentDataCount]->AsObject();

		// if metadata is present for max time then no need to calculate
		if(bCalculateMaxTime)
		{
			AgentMovementInfoData.MaxTime = JSONSimDataObject->GetNumberField(StringCast<TCHAR>("time"));
		}

		// if metadata is present for time steps then no need to calculate
		if(bCalculateTimeBetweenSteps)
		{
			// get time field
			float TimeVal = JSONSimDataObject->GetNumberField(StringCast<TCHAR>("time"));
			TimeBetweenSteps = TimeVal - AgentMovementInfoData.MaxTime;
		}

		// Parameters for step-duration related smoothing, to account for head-tracking  body sway over step duration
		minimumStepDuration = 0.6; // Minimum step duration in seconds, to assess suitable animation
		maximumStepDuration = 1.0; // Maximum step duration in seconds, to assess suitable animation
		minTimedSrcRecordsForStep = (int)std::round(minimumStepDuration*(int)std::round(((double)1.0 / (double)TimeBetweenSteps))); // Min. num. time steps to forward-assess
		maxTimedSrcRecordsForStep = (int)std::round(maximumStepDuration * (double)TimeBetweenSteps); // Max. num. time steps to forward-assess
		timeDurationPerRecord = 1.0 / (double)(int)std::round(((double)1.0 / (double)TimeBetweenSteps));

		// get the sample array for this
		TArray<TSharedPtr<FJsonValue>> JSONSampleArray = JSONSimDataObject->GetArrayField(StringCast<TCHAR>("samples"));
		
		// the number of samples should technically be how many entities there are for this time step
		NumOfAgentsPerTimeStep.Add(JSONSampleArray.Num());

		// log if the sample array is empty
		if (JSONSampleArray.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("No samples found for time step %d"), CurrentDataCount);
		}

		// create a movement sample array
		TArray<FSimMovementSample> MovementSamples;

		// loop through the sample array and build the movement sample values
		for (int32 JsimSample = 0; JsimSample < JSONSampleArray.Num(); JsimSample++)
		{
			if (bShouldStop) break;
			if (!JSONSampleArray[JsimSample]->AsObject().IsValid())
			{
				ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
				                              TEXT("Invalid sample data"),
				                              TEXT("Simulation sample entry is invalid."),
				                              TEXT("AgentDataSubsystem"));
				continue;
			}

			// Get the JSON object
			TSharedPtr<FJsonObject> JSONSampleDataObject = JSONSampleArray[JsimSample]->AsObject();

			// Get the entity ID
			int32 EntityID;
			if(!JSONSampleDataObject->TryGetNumberField(StringCast<TCHAR>("entity"), EntityID))
			{
				ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
				                              TEXT("Missing entity ID"),
				                              TEXT("Simulation sample is missing the entity field."),
				                              TEXT("AgentDataSubsystem"));
				continue; // no ID
			}

			// Initilize the position variable
			FVector Position = FVector::ZeroVector;
			// Initialize the rotation variable
			FRotator Rotation = FRotator::ZeroRotator;

			// Create a pointer to the position value
			const TSharedPtr<FJsonObject>* PositionValue;
			// Get the Position field
			if (JSONSampleDataObject->TryGetObjectField(StringCast<TCHAR>("position"), PositionValue))
			{
				//  Check if position field has x, y and z fields and get the values
				if(PositionValue->ToSharedRef()->HasField(StringCast<TCHAR>("x")) && PositionValue->ToSharedRef()->HasField(StringCast<TCHAR>("y")) && PositionValue->ToSharedRef()->HasField(StringCast<TCHAR>("z")))
				{
					// Map the values to the position
					Position.X = PositionValue->ToSharedRef()->GetNumberField(StringCast<TCHAR>("x")); //TODO need to work out for different modeling studios
					Position.Y = -PositionValue->ToSharedRef()->GetNumberField(StringCast<TCHAR>("y"));
					Position.Z = PositionValue->ToSharedRef()->GetNumberField(StringCast<TCHAR>("z"));
				}
				if(JSONObject != nullptr && JSONObject->GetObjectField(StringCast<TCHAR>("metadata"))->GetBoolField(StringCast<TCHAR>("isSI")))
				{
					// unit is SI so should be in meters - convert to cm
					Position *= 100.0f;
				}
				else
				{
					// unit is not SI
					Position *= 10.0f; // unless we add a field to the metadata that stipulates the unit of measurement we will have to add a user prompt to select the unit of measurement
				}

				// measurement unit conversion
				//Position *= 10.0f; // unless we add a field to the metadata that stipulates the unit of measurement we will have to add a user prompt to select the unit of measurement

			}
			else
			{
				ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
				                              TEXT("Missing position data"),
				                              TEXT("Simulation sample is missing position fields."),
				                              TEXT("AgentDataSubsystem"));
			}

			// Get the Rotation field which is in degrees
			float RotationValue;

			// try get the rotation value
			if (JSONSampleDataObject->TryGetNumberField(StringCast<TCHAR>("rotation"), RotationValue))
			{
				// if the metadata contains isDeg then we know the rotation is in degrees otherwise it is in radians
				if(JSONObject != nullptr && JSONObject->HasField(StringCast<TCHAR>("metadata")) && JSONObject->GetObjectField(StringCast<TCHAR>("metadata"))->HasField(StringCast<TCHAR>("isDeg")))
				{
					// is it degrees
					if(JSONObject != nullptr && JSONObject->GetObjectField(StringCast<TCHAR>("metadata"))->GetBoolField(StringCast<TCHAR>("isDeg")))
					{
						// convert the degree rotation value to x,y,z // the minus 90 is to adjust the rotation to the correct direction for mesh needs better handle on this
						Rotation = FRotator(0.0f, (-RotationValue -  90), 0.0f);//TODO: this is correct(for test data) and add method for different modeling studios
					}
					else
					{
						// convert the radian rotation value to x,y,z // the minus 90 is to adjust the rotation to the correct direction for mesh needs better handle on this
						Rotation = FRotator(0.0f, FMath::RadiansToDegrees(-RotationValue) - 90, 0.0f);//TODO: this is correct(for test data) and add method for different modeling studios
					}


				}
				else
				{
					// the metadata doesn't exist so we assume it is in degrees

					// convert the degree rotation value to x,y,z // the minus 90 is to adjust the rotation to the correct direction for mesh needs better handle on this
					Rotation = FRotator(0.0f, (-RotationValue -  90), 0.0f);//TODO: this is correct(for test data) and add method for different modeling studios
				}

			}
			else
			{
				ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
				                              TEXT("Missing rotation data"),
				                              TEXT("Simulation sample is missing rotation data."),
				                              TEXT("AgentDataSubsystem"));
			}

			// Get the speed
			float Speed(0);

			// try get the speed value
			if (!JSONSampleDataObject->TryGetNumberField(StringCast<TCHAR>("speed"), Speed))
			{
				ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
				                              TEXT("Missing speed data"),
				                              TEXT("Simulation sample is missing speed data."),
				                              TEXT("AgentDataSubsystem"));
			}

			// Get the mode
			FString Mode("");

			// try get the mode string value
			if (!JSONSampleDataObject->TryGetStringField(StringCast<TCHAR>("mode"), Mode))
			{
				ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
				                              TEXT("Missing mode data"),
				                              TEXT("Simulation sample is missing mode data."),
				                              TEXT("AgentDataSubsystem"));
			}

			FSimMovementSample MovementSample;
			MovementSample.EntityID = EntityID;
			MovementSample.Position = Position;
			MovementSample.Rotation = Rotation;
			MovementSample.Speed = Speed;

			MovementSamples.Add(MovementSample);

			AgentDataArray[EntityID].MovementData.Push(FMovementPreProcessData(Position));
		}

		AgentMovementInfoData.SimulationData.Add(CurrentDataCount, MovementSamples);

		// Calculate the current percentage of the data loaded
		float CurrentPercentage = (float)CurrentDataCount / (float)TargetDataCount;

		// Send to progress queue in subsystem so it can Broadcast the current percentage of the data loaded -- this is done on the game thread
		if (UAgentDataSubsystem* Subsys = OwnerSubsystem.Get())
		{
			Subsys->ProgressQueue.Enqueue(CurrentPercentage);
		}

		// Increment the current data count
		CurrentDataCount++;
	}
}

void FProcessSimulationDataRunnable::RunHdf5SimDataGatheringLoop(bool bCalculateTimeBetweenSteps,
	bool bCalculateMaxTime)
{
	if (Hdf5Data.Samples.Num() == 0)
	{
		ReportAgentDataErrorAnyThread(OwnerSubsystem.Get(),
		                              TEXT("Simulation data missing"),
		                              TEXT("HDF5 data was not loaded before processing simulation steps."),
		                              TEXT("AgentDataSubsystem - FProcessSimulationDataRunnable::RunHdf5SimDataGatheringLoop"));
		bShouldStop = true;
		return;
	}

	// Group samples by timestep index
	// First, find the max timestep index to determine how many timesteps we have
	int32 MaxTimestepIndex = 0;
	for (const FHdf5SampleData& Sample : Hdf5Data.Samples)
	{
		if (Sample.TimestepIndex > MaxTimestepIndex)
		{
			MaxTimestepIndex = Sample.TimestepIndex;
		}
	}

	// Update target data count if needed
	if (TargetDataCount == 0)
	{
		TargetDataCount = MaxTimestepIndex + 1;
	}

	// Reserve space for the number of agents per time step
	NumOfAgentsPerTimeStep.Reserve(TargetDataCount);

	// Create a map to group samples by timestep
	TMap<int32, TArray<const FHdf5SampleData*>> SamplesByTimestep;
	for (const FHdf5SampleData& Sample : Hdf5Data.Samples)
	{
		SamplesByTimestep.FindOrAdd(Sample.TimestepIndex).Add(&Sample);
	}

	// Get unit conversion info from metadata
	const bool bIsSI = Hdf5Data.Meta.bIsSI;
	const bool bIsDeg = Hdf5Data.Meta.bIsDeg;

	// Process each timestep
	for (int32 TimestepIdx = 0; TimestepIdx <= MaxTimestepIndex && !bShouldStop; TimestepIdx++)
	{
		CurrentDataCount = TimestepIdx;

		// Get samples for this timestep
		TArray<const FHdf5SampleData*>* TimestepSamples = SamplesByTimestep.Find(TimestepIdx);

		if (!TimestepSamples || TimestepSamples->Num() == 0)
		{
			// Empty timestep - add empty array and continue
			NumOfAgentsPerTimeStep.Add(0);
			AgentMovementInfoData.SimulationData.Add(TimestepIdx, TArray<FSimMovementSample>());
			continue;
		}

		// Calculate time between steps if needed (use first sample with time > 0)
		if (bCalculateTimeBetweenSteps && TimestepIdx == 1 && TimestepSamples->Num() > 0)
		{
			// Since HDF5 doesn't store time per sample directly, use metadata sampling rate
			// or calculate from timestep index difference
			if (Hdf5Data.Meta.SamplingRate > 0)
			{
				TimeBetweenSteps = Hdf5Data.Meta.SamplingRate;
			}
		}

		// Update max time if calculating
		if (bCalculateMaxTime && Hdf5Data.Meta.Duration > 0)
		{
			AgentMovementInfoData.MaxTime = Hdf5Data.Meta.Duration;
		}
		else if (bCalculateMaxTime && TimeBetweenSteps > 0)
		{
			AgentMovementInfoData.MaxTime = TimestepIdx * TimeBetweenSteps;
		}

		// Parameters for step-duration related smoothing
		minimumStepDuration = 0.6;
		maximumStepDuration = 1.0;
		minTimedSrcRecordsForStep = (int)std::round(minimumStepDuration * (int)std::round(((double)1.0 / (double)TimeBetweenSteps)));
		maxTimedSrcRecordsForStep = (int)std::round(maximumStepDuration * (double)TimeBetweenSteps);
		timeDurationPerRecord = 1.0 / (double)(int)std::round(((double)1.0 / (double)TimeBetweenSteps));

		// Track number of agents for this timestep
		NumOfAgentsPerTimeStep.Add(TimestepSamples->Num());

		// Create movement samples array for this timestep
		TArray<FSimMovementSample> MovementSamples;

		// Process each sample
		for (const FHdf5SampleData* SamplePtr : *TimestepSamples)
		{
			if (bShouldStop) break;

			const FHdf5SampleData& Sample = *SamplePtr;

			// Build position vector
			FVector Position;
			Position.X = Sample.PositionX;
			Position.Y = -Sample.PositionY; // Negate Y like JSON does
			Position.Z = Sample.PositionZ;

			// Apply unit conversion
			if (bIsSI)
			{
				// SI units (meters) - convert to cm
				Position *= 100.0f;
			}
			else
			{
				// Non-SI units - apply same conversion as JSON
				Position *= 10.0f;
			}

			// Build rotation
			FRotator Rotation;
			if (bIsDeg)
			{
				// Rotation is in degrees - apply same transformation as JSON
				Rotation = FRotator(0.0f, (-Sample.Rotation - 90), 0.0f);
			}
			else
			{
				// Rotation is in radians - convert to degrees then apply transformation
				Rotation = FRotator(0.0f, FMath::RadiansToDegrees(-Sample.Rotation) - 90, 0.0f);
			}

			// Create movement sample
			FSimMovementSample MovementSample;
			MovementSample.EntityID = Sample.EntityId;
			MovementSample.Position = Position;
			MovementSample.Rotation = Rotation;
			MovementSample.Speed = Sample.Speed;

			MovementSamples.Add(MovementSample);

			// Add to agent data array for preprocessing
			if (Sample.EntityId >= 0 && Sample.EntityId < AgentDataArray.Num())
			{
				AgentDataArray[Sample.EntityId].MovementData.Push(FMovementPreProcessData(Position));
			}
		}

		// Store movement samples for this timestep
		AgentMovementInfoData.SimulationData.Add(TimestepIdx, MovementSamples);

		// Calculate and report progress
		float CurrentPercentage = (float)CurrentDataCount / (float)TargetDataCount;
		if (UAgentDataSubsystem* Subsys = OwnerSubsystem.Get())
		{
			Subsys->ProgressQueue.Enqueue(CurrentPercentage);
		}
	}

	// Update CurrentDataCount to final value
	CurrentDataCount = MaxTimestepIndex + 1;
}

void FProcessSimulationDataRunnable::FinalizeProgress()
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

	// let the thread sleep for 0.5 second
	FPlatformProcess::Sleep(0.5f);

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

	// let the thread sleep for 0.5 second
	FPlatformProcess::Sleep(0.5f);
}

uint32 FProcessSimulationDataRunnable:: Run()
{
	bIsRunning = true;
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
	RunSimulationDataGatheringLoop(bCalculateTimeBetweenSteps, bCalculateMaxTime);

	if (bShouldStop)
	{
		return 0;
	}

	// Send the final progress and completion events
	FinalizeProgress();

	bIsRunning = false;
	return 0; // return 0 to indicate that the thread has ended
}
void FProcessSimulationDataRunnable::Stop()
{
	bShouldStop = true;
}
void FProcessSimulationDataRunnable::Exit()
{
	// as the runnable contains multiple properties that are not handled by garbage collection,
	// we need to ensure that we clean up properly

	// Immediately release any non–Garbage‑collected, thread‑safe pointers
	// (your TSharedPtr will auto‑release, but Reset() here will drop the ref now)
	JSONObject.Reset();

	// Clean up HDF5 data
	if (HDF5SimulationReader.IsOpen())
	{
		HDF5SimulationReader.CloseFile();
	}
	Hdf5Data.Entities.Empty();
	Hdf5Data.Entities.Shrink();
	Hdf5Data.Samples.Empty();
	Hdf5Data.Samples.Shrink();
	Hdf5Data.Meta = FHdf5SimulationMetadata();
	SimulationFileType = ESimulationFileType::ESFT_Unknown;

	for (auto& Pair : AgentMovementInfoData.SimulationData)
	{
		Pair.Value.Empty();   // frees any extra capacity in each TArray
		Pair.Value.Shrink();   // frees any extra capacity in each TArray
	}

	// Optionally clear large TArrays now to free memory immediately
	AgentMovementInfoData.SimulationData.Empty();
	AgentMovementInfoData.SimulationData.Shrink();

	AgentDataArray.Empty();
	AgentDataArray.Shrink();

	EmbAvatarAnims.Empty();
	EmbAvatarAnims.Shrink();

	StepVectors.Empty();
	StepVectors.Shrink();

	NumOfAgentsPerTimeStep.Empty();
	NumOfAgentsPerTimeStep.Shrink();

	bReadyToDelete = true; // Set the flag to true to indicate that the runnable is ready to be deleted
}

TArray<FSimMovementSample> FProcessSimulationDataRunnable::GetMovementSamples(int32 AgentID)
{
	TArray<FSimMovementSample> MovementSamples;

	// check if the agent id exceeds the max agents
	if (AgentID >= AgentMovementInfoData.SimulationData.Num())
	{
		// throw error message
	}
	else
	{
		// loop through the simulation data and get the movement samples
		for (int32 i = 0; i < AgentMovementInfoData.SimulationData.Num(); i++)
		{
			if (bShouldStop) break;
			// loop through the movement samples for this time step
			for (FSimMovementSample MovementSample : AgentMovementInfoData.SimulationData[i])
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

void FProcessSimulationDataRunnable::CalcSmoothedStepMovementBrackets(TArray<FAgentData> AgentSamples)
{
	bool bNotDone = false;
	while (!bShouldStop && !bNotDone)
	{
		// Loop through the agentsData, calculating the vectors for each agent
		for (int a = 0; a < AgentSamples.Num(); a++)
		{
			if (bShouldStop) break;
			const int  DebugAgent = 0;
			TArray<FVector> RecordVectors = TArray<FVector>();
			CalculateSrcVectors(RecordVectors, AgentSamples[a]); // Calculate the short-time source vectors for the agent
			AllocateAnimPts(RecordVectors.Num()); // Pre-allocate an array to receive the animation brackets
			CurrentAgentAnimSmoothing = a;
			FVector StepVector = FVector::ZeroVector;
			int t = 0, tSpan = 1;
			EPedestrianMovementBracket lastEmb = EPedestrianMovementBracket::Emb_NotMoving;
			float stepDuration = 0.0f;

			// Iterate t through all recordVectors, rapidly moving through the initial zero-speed records
			for (t = 0; t < RecordVectors.Num() && (RecordVectors[t].Length()/(timeDurationPerRecord) < MinSpeedWalking); t++) {
				if (bShouldStop) break;
				SetAnimPt(t, EPedestrianMovementBracket::Emb_NotMoving, 1.0f);
				// Debug info for tracing a single person consecutive output to assess the benefits of movement bracket smoothing
				// if ((DebugAgent > -1) && (DebugAgent== a)){
				// 	double recordSpeed = RecordVectors[t].Length()/(TimeBetweenSteps);
				// 	std::cout << std::fixed << std::setprecision(2) << std::setw(4) << std::setfill('0');
				// 	std::cout << "Motion[" << std::setw(3) << t << "] = " << RecordVectors[t].Length() * 100.0 << "cm, "
				// 		<< recordSpeed << "m/s V(" << tSpan << ")step-pts " << std::endl;
				// }
			}
			if (bShouldStop) break;
			// Calculate the sum-vector speed for the next rolling block of timed records to more accurately estimate gait speed
			// Note: we increase and decrease tSpan (rough timesteps in a step) depending on the required step duration
			// 
			// StepVector is the sum of the vectors from index t to t+tSpan (the rolling sum-vector for a following estimated step)
			double lastSpeed = 0.0, stepSpeed = RecordVectors[t].Length()/(timeDurationPerRecord);
			tSpan = minTimedSrcRecordsForStep;
			AddManyVectors(RecordVectors, t, tSpan, StepVector); // Starting to move from zero, so animate the step that we are starting to take

			// Now, we have a meaningful speed, so we can start calculating the proceeding records as part of moving steps
			// Iterate from t to the end of the recordVectors, calculating the sum-vector for the next step duration
			for (; t < RecordVectors.Num(); t++) {
				if (bShouldStop) break;
				stepSpeed = StepVector.Length()/(static_cast<double>(tSpan) * timeDurationPerRecord) / 100; // This 100 value is coming from the isSI conversion
				EPedestrianMovementBracket thisAnimMF = CalculateStepAnimationParams(static_cast<float>(stepSpeed), stepDuration);
				SetAnimPt(t, thisAnimMF, stepDuration);

				// Debug info for tracing a single person consecutive output to assess the benefits of movement bracket smoothing
				// if ((DebugAgent > -1) && (DebugAgent== a)) {
				// 	double recordSpeed = RecordVectors[t].Length()/(TimeBetweenSteps);
				// 	EPedestrianMovementBracket instantAnimF = CalculateStepAnimationParams((float)recordSpeed, stepDuration);
				// 	const double speedDiff = recordSpeed - stepSpeed;
				// 	std::cout << "Motion[" << std::setw(3) << t << "] = " << RecordVectors[t].Length() * 100.0 << "cm, "
				// 		<< recordSpeed << "m/s V(" << tSpan << "pts) = " << stepSpeed << "m/s diff = " << speedDiff << "m/s"
				// 		<< " Step Anim: " << (int)thisAnimMF << " Record: " << (int)instantAnimF
				// 		<< std::endl;
				// }

				// Move the step forward by one record, by subtracting the last vector and adding the new one
				StepVector -= RecordVectors[t]; // subtract this record single vector, ahead of the next step assessment, from t+1

				int newtSpan = static_cast<int>(std::round(stepDuration / timeDurationPerRecord));

				// Reduce tSpan? if the new tSpan is less than current one. No need to adjust the evctor sum, as we just removed record[t]
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
		
			// Now: the animation movement brackets are stored in IKVectorSteps::agentsData[nPeople].embAvatarAnims[nTimeSteps]
		}
		bNotDone = true; // we are done with the processing
	}
}

int FProcessSimulationDataRunnable::CalculateSrcVectors(TArray<FVector>& Vec3D, FAgentData Sample)
{
	if (Sample.MovementData.Num() > 2)
	{
		if (Vec3D.Num() < Sample.MovementData.Num())
			Vec3D.SetNum(Sample.MovementData.Num(), EAllowShrinking::No);
		
		int i = 0;
		for (; i < Sample.MovementData.Num() - 1; i++)
		{
			Vec3D[i] = Sample.MovementData[i].Location - Sample.MovementData[i + 1].Location;
		}
		Vec3D[i] = Vec3D[i - 1]; // set the last vector to the previous vector to avoid out of bounds error
	}
	return (int)Sample.MovementData.Num();
}
// TODO: pass a in so can speed this up 
void FProcessSimulationDataRunnable::SetAnimPt(int t, EPedestrianMovementBracket emb, float StepDuration)
{
	EmbAvatarAnims[t].MovementBracket = emb;
	EmbAvatarAnims[t].StepDurationMS = static_cast<unsigned long>(StepDuration * 1000.0f);

	// Set the agent animation smoothing data
	for (FSimMovementSample& MovementSample : AgentMovementInfoData.SimulationData[t])
	{
		if (bShouldStop) break;
		if (MovementSample.EntityID == CurrentAgentAnimSmoothing)
		{
			MovementSample.MovementBracket = static_cast<EPedestrianMovementBracket>(emb);
			MovementSample.StepDurationMS = EmbAvatarAnims[t].StepDurationMS;
		}
	}
}

void FProcessSimulationDataRunnable::AddManyVectors(TArray<FVector>& Vec3D, int TStartStep, int TSpanStepPts, FVector& SumVec)
{
	for (int i = TStartStep; i < TStartStep + TSpanStepPts; i++){
		SumVec = SumVec + Vec3D[i];
	}
}

EPedestrianMovementBracket FProcessSimulationDataRunnable::CalculateStepAnimationParams(float CurrentSpeed, float& StepsPerSecond)
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
