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


UAgentDataSubsystem::UAgentDataSubsystem() :
	JSONDataFile(TEXT("")),
	JSONDataString(TEXT("")),
	MaxAgents(0),
	QuadTreeDataActor()
{
	//AgentMovementInfoData = FSimulationFragment();
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
	Super::Deinitialize();
}

void UAgentDataSubsystem::GetJSONDataFile(FString InJsonDataFile)
{
	if (!CheckFilePathExists(InJsonDataFile))
	{
		UE_LOG(LogTemp, Warning, TEXT("File Path does not exist"));
		return;// TODO: Add error handling
	}
	TSharedRef<TJsonReader<TCHAR>> JSONReader = TJsonReaderFactory<TCHAR>::Create(JSONDataString);

	// Create JSON Reader and load String
	CreateJsonReaderAndString(JSONDataString, JSONReader, InJsonDataFile);

	// Deserialize JSON Data
	bool bDeserializeSuccess = FJsonSerializer::Deserialize(JSONReader, JSONObject);

	if (!bDeserializeSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to Deserialize JSON Data"));
		return;// TODO: Add error handling
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
			//TODO: throw our error popup to display that the json object is null meaning bad file(most likely)
		}
		else
		{
			//CalculateMaxEntitiesPermitted();
		}
	}
	else
	{
		// TODO throw our error popup to display that the game instance is null
	}
	
}

void UAgentDataSubsystem::BuildPedestrianMovementData()
{
	//TArray<TSharedPtr<FJsonValue>> JsonSimulationDataArray = JSONObject->GetArrayField("simulation");
	//{

	//	// loop through the JSON array
	//	for (int32 i_AllSimData = 0; i_AllSimData < JsonSimulationDataArray.Num(); i_AllSimData++)
	//	{

	//		if (!JsonSimulationDataArray[i_AllSimData]->AsObject().IsValid())
	//		{
	//			UE_LOG(LogTemp, Warning, TEXT("Invalid JSON Object"));
	//			break;
	//		}

	//		// Get the JSON object for this 
	//		TSharedPtr<FJsonObject> JSONSimDataObject = JsonSimulationDataArray[i_AllSimData]->AsObject();

	//		// get time field
	//		float TimeVal = JSONSimDataObject->GetNumberField("time");

	//		// get the sample array for this
	//		TArray<TSharedPtr<FJsonValue>> JSONSampleArray = JSONSimDataObject->GetArrayField("samples");

	//		// create a movement sample array
	//		TArray<FSimMovementSample> MovementSamples;

	//		// loop through the sample array and build the movement sample values
	//		for (int32 JsimSample = 0; JsimSample < JSONSampleArray.Num(); JsimSample++)
	//		{
	//			if (!JSONSampleArray[JsimSample]->AsObject().IsValid())
	//			{
	//				UE_LOG(LogTemp, Warning, TEXT("Invalid JSON Object"));
	//				continue;
	//			}

	//			// Get the JSON object
	//			TSharedPtr<FJsonObject> JSONSampleDataObject = JSONSampleArray[JsimSample]->AsObject();

	//			// Get the entity ID
	//			int32 EntityID = JSONSampleDataObject->GetIntegerField("entity");

	//			// Initilize the position variable
	//			FVector Position = FVector::ZeroVector;

	//			// Get the Position field
	//			TSharedPtr<FJsonObject> PositionValue = JSONSampleDataObject->GetObjectField("position");
	//			if (PositionValue.IsValid())
	//			{
	//				// Map the values to the position
	//				Position.X = PositionValue->GetNumberField("x");
	//				Position.Y = -PositionValue->GetNumberField("y");
	//				Position.Z = PositionValue->GetNumberField("z");
	//			}
	//			else
	//			{
	//				// Handle missing or invalid "position" field.
	//				UE_LOG(LogTemp, Warning, TEXT("Missing or invalid 'position' field. Position will be set to 0,0,0"));

	//			}

	//			// Initialize the rotation variable
	//			FRotator Rotation = FRotator::ZeroRotator;

	//			// Get the Rotation field
	//			float RotationValue = JSONSampleDataObject->GetNumberField("rotation");
	//			if (PositionValue.IsValid())
	//			{
	//				// convert the degree rotation value to x,y,z
	//				Rotation = FRotator(0.0f, -RotationValue, 0.0f);//TODO: this is correct(for test data) and add method for different modeling studios
	//			}
	//			else
	//			{
	//				// Handle missing or invalid "rotation" field.
	//				UE_LOG(LogTemp, Warning, TEXT("Missing or invalid 'rotation' field. Rotation will be set to 0,0,0"));

	//			}

	//			// Get the speed
	//			float Speed = JSONSampleDataObject->GetNumberField("speed");

	//			// Get the mode
	//			FString Mode = JSONSampleDataObject->GetStringField("mode");

	//			// Create the movement sample
	//			FSimMovementSample MovementSample(EntityID, Position, Rotation, Speed, Mode);

	//			// Add the movement sample
	//			MovementSamples.Add(MovementSample);

	//		}

	//		// add the movement sample values to the simulation data
	//		AddMovementSample(i_AllSimData, MovementSamples);

	//	}
	//}
}

void UAgentDataSubsystem::BuildPedestrianAgentInfo()
{
	TArray<TSharedPtr<FJsonValue>> JsonEntityDataArray = JSONObject->GetArrayField(StringCast<TCHAR>("entities"));

	// loop through the JSON array
	for (int32 entityIndex = 0; entityIndex < JsonEntityDataArray.Num(); entityIndex++)
	{
		if (!JsonEntityDataArray[entityIndex]->AsObject().IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Invalid JSON Object"));
			break;
		}

		// Get the JSON object for this 
		TSharedPtr<FJsonObject> JSONEntityDataObject = JsonEntityDataArray[entityIndex]->AsObject();

		// Get the entity ID 
		int32 EntityID = JSONEntityDataObject->GetIntegerField(StringCast<TCHAR>("id"));
		
		// Get the entity name
		FString EntityName = JSONEntityDataObject->GetStringField(StringCast<TCHAR>("name"));

		// Get the entity sim time
		FString EntitySimTime = JSONEntityDataObject->GetStringField(StringCast<TCHAR>("simTimeS"));

		// Get the entity max speed
		float EntityMaxSpeed = JSONEntityDataObject->GetNumberField(StringCast<TCHAR>("max_speed"));

		// Get the entity M_Plane
		FString EntityM_Plane = JSONEntityDataObject->GetStringField(StringCast<TCHAR>("m_plane"));

		// Get the entity map
		int32 EntityMap = JSONEntityDataObject->GetIntegerField(StringCast<TCHAR>("map"));

		// Create the entity info fragment
		FEntityInfoFragment EntityInfo;(EntityID, EntityName, EntitySimTime, EntityMaxSpeed, EntityM_Plane, EntityMap);
		// Assign Vals
		EntityInfo.EntityID = EntityID;
		EntityInfo.EntityName = EntityName;
		EntityInfo.EntitySimTimeS = EntitySimTime;
		EntityInfo.EntityMaxSpeed = EntityMaxSpeed;
		EntityInfo.EntityM_Plane = EntityM_Plane;
		EntityInfo.EntityMap = EntityMap;
		
	}
}

void UAgentDataSubsystem::SetEntityInfoByIndex(int32 Index, FEntityInfoFragment& EntityInfoFragToUpdate) const
{
	if (Index < 0 || Index >= MaxAgents)
	{
		UE_LOG(LogTemp, Warning, TEXT("Index out of range"));
		return;
	}
	
	TArray<TSharedPtr<FJsonValue>> JsonEntityDataArray = JSONObject->GetArrayField(StringCast<TCHAR>("entities", 8));

	// Get the JSON object for this 
	TSharedPtr<FJsonObject> JSONEntityDataObject = JsonEntityDataArray[Index]->AsObject();

	
	// Get the entity ID
	EntityInfoFragToUpdate.EntityID = JSONEntityDataObject->GetIntegerField(StringCast<TCHAR>("id", 2));

	// Get the entity name
	EntityInfoFragToUpdate.EntityName = JSONEntityDataObject->GetStringField(StringCast<TCHAR>("name", 4));

	// Get the entity sim time
	EntityInfoFragToUpdate.EntitySimTimeS = JSONEntityDataObject->GetStringField(StringCast<TCHAR>("simTimeS", 8));

	// Get the entity max speed
	EntityInfoFragToUpdate.EntityMaxSpeed = JSONEntityDataObject->GetNumberField(StringCast<TCHAR>("max_speed", 9));

	// Get the entity M_Plane
	EntityInfoFragToUpdate.EntityM_Plane = JSONEntityDataObject->GetStringField(StringCast<TCHAR>("m_plane", 7));

	// Get the entity map
	EntityInfoFragToUpdate.EntityMap = JSONEntityDataObject->GetIntegerField(StringCast<TCHAR>("map", 3));

}

void UAgentDataSubsystem::SetEntityRenderingByIndex(int32 Index,
	FEntityRenderingFragment& EntityRenderingFragToUpdate) const
{
	if (Index < 0 || Index >= MaxAgents)
	{
		UE_LOG(LogTemp, Warning, TEXT("Index out of range"));
		return;
	}

	EntityRenderingFragToUpdate.EntityID = Index;
	
	TArray<TSharedPtr<FJsonValue>> JsonEntityDataArray = JSONObject->GetArrayField(StringCast<TCHAR>("entities", 8));

	// Get the JSON object for this 
	TSharedPtr<FJsonObject> JSONEntityDataObject = JsonEntityDataArray[Index]->AsObject();

	if (!JSONEntityDataObject.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid JSON Object"));
		return;
	}
	FString AgentName = JSONEntityDataObject->GetStringField(StringCast<TCHAR>("name", 4));

	
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
	FFileHelper::LoadFileToString(OutJsonString, *JsonFile);

	// Create JSON Reader
	OutJsonReader = TJsonReaderFactory<TCHAR>::Create(OutJsonString);
}

FJsonDataRunnable::FJsonDataRunnable(FString InJsonDataFile)
{

	JsonFilePath = InJsonDataFile;
	// check file actually exists before creating the thread
	if (!FPaths::FileExists(JsonFilePath))
	{
		// TODO: this needs to broadcast error message to the UI
		return;
	}

	
	
	// Create the thread -- The thread priority is set to TPri_Normal this may need to be adjusted based on the application
	Thread = FRunnableThread::Create(this, TEXT("FJsonDataRunnable"), 0, TPri_Normal);
}

FJsonDataRunnable::~FJsonDataRunnable()
{
        // if the thread is still running, stop it
        if (Thread != nullptr)
        {
                Thread->Kill(true);
                delete Thread;
        }
}

bool FJsonDataRunnable::LoadFileAndDeserialize()
{
        // check file actually exists before creating the thread
        if (!FPaths::FileExists(JsonFilePath))
        {
                bShouldStop = true;
                return false;
        }

        FFileHelper::LoadFileToString(JsonDataFile, *JsonFilePath);
        TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonDataFile);

        bool bDeserializeSuccess = FJsonSerializer::Deserialize(JsonReader, JSONObject);
        if (!bDeserializeSuccess)
        {
                bShouldStop = true;
                return false;
        }

        return true;
}

void FJsonDataRunnable::ProcessMetadata(bool& bCalculateTimeBetweenSteps, bool& bCalculateMaxTime)
{
        bCalculateTimeBetweenSteps = true;
        bCalculateMaxTime = true;

        if(JSONObject->HasField(StringCast<TCHAR>("metadata")))
        {
                TSharedPtr<FJsonObject> JSONMetaDataObject = JSONObject->GetObjectField(StringCast<TCHAR>("metadata"));

                if(!JSONMetaDataObject->TryGetNumberField(StringCast<TCHAR>("max_num_entities"), MaxAgents))
                {
                        MaxAgents = JSONObject->GetArrayField(StringCast<TCHAR>("entities")).Num();
                }

                if(JSONMetaDataObject->HasField(StringCast<TCHAR>("duration")) && JSONMetaDataObject->HasField(StringCast<TCHAR>("sampling_rate")) &&
                   JSONMetaDataObject->GetNumberField(StringCast<TCHAR>("duration")) > 0 && JSONMetaDataObject->GetNumberField(StringCast<TCHAR>("sampling_rate")) > 0)
                {
                        AgentMovementInfoData.MaxTime = JSONMetaDataObject->GetNumberField(StringCast<TCHAR>("duration"));
                        TimeBetweenSteps = JSONMetaDataObject->GetNumberField(StringCast<TCHAR>("sampling_rate"));
                        TargetDataCount = AgentMovementInfoData.MaxTime / TimeBetweenSteps;

                        bCalculateTimeBetweenSteps = false;
                        bCalculateMaxTime = false;
                }
                else
                {
                        TargetDataCount = JSONObject->GetArrayField(StringCast<TCHAR>("simulation")).Num();
                }
        }
        else
        {
                MaxAgents = JSONObject->GetArrayField(StringCast<TCHAR>("entities")).Num();
        }
}

void FJsonDataRunnable::RunSimulationLoop(bool bCalculateTimeBetweenSteps, bool bCalculateMaxTime)
{
        TArray<TSharedPtr<FJsonValue>> JsonSimDataArray = JSONObject->GetArrayField(StringCast<TCHAR>("simulation"));

        while (!bShouldStop && CurrentDataCount <= TargetDataCount)
        {
                if (!JsonSimDataArray.IsValidIndex(CurrentDataCount) || !JsonSimDataArray[CurrentDataCount]->AsObject().IsValid())
                {
                        bShouldStop = true;
                        break;
                }

                TSharedPtr<FJsonObject> JSONSimDataObject = JsonSimDataArray[CurrentDataCount]->AsObject();

                if(bCalculateMaxTime)
                {
                        AgentMovementInfoData.MaxTime = JSONSimDataObject->GetNumberField(StringCast<TCHAR>("time"));
                }

                if(bCalculateTimeBetweenSteps)
                {
                        float TimeVal = JSONSimDataObject->GetNumberField(StringCast<TCHAR>("time"));
                        TimeBetweenSteps = TimeVal - AgentMovementInfoData.MaxTime;
                }

                minimumStepDuration = 0.6;
                maximumStepDuration = 1.0;
                minTimedSrcRecordsForStep = (int)std::round(minimumStepDuration*(int)std::round(((double)1.0 / (double)TimeBetweenSteps)));
                maxTimedSrcRecordsForStep = (int)std::round(maximumStepDuration * (double)TimeBetweenSteps);
                timeDurationPerRecord = 1.0 / (double)(int)std::round(((double)1.0 / (double)TimeBetweenSteps));

                TArray<TSharedPtr<FJsonValue>> JSONSampleArray = JSONSimDataObject->GetArrayField(StringCast<TCHAR>("samples"));

                if (JSONSampleArray.Num() == 0)
                {
                        UE_LOG(LogTemp, Warning, TEXT("No samples found for time step %d"), CurrentDataCount);
                }

                TArray<FSimMovementSample> MovementSamples;

                for (int32 JsimSample = 0; JsimSample < JSONSampleArray.Num(); JsimSample++)
                {
                        if (!JSONSampleArray[JsimSample]->AsObject().IsValid())
                        {
                                continue;
                        }

                        TSharedPtr<FJsonObject> JSONSampleDataObject = JSONSampleArray[JsimSample]->AsObject();

                        int32 EntityID;
                        if(!JSONSampleDataObject->TryGetNumberField(StringCast<TCHAR>("entity"), EntityID))
                        {
                                continue;
                        }

                        FVector Position = FVector::ZeroVector;
                        FRotator Rotation = FRotator::ZeroRotator;

                        const TSharedPtr<FJsonObject>* PositionValue;
                        if (JSONSampleDataObject->TryGetObjectField(StringCast<TCHAR>("position"), PositionValue))
                        {
                                if(PositionValue->ToSharedRef()->HasField(StringCast<TCHAR>("x")) && PositionValue->ToSharedRef()->HasField(StringCast<TCHAR>("y")) && PositionValue->ToSharedRef()->HasField(StringCast<TCHAR>("z")))
                                {
                                        Position.X = PositionValue->ToSharedRef()->GetNumberField(StringCast<TCHAR>("x"));
                                        Position.Y = -PositionValue->ToSharedRef()->GetNumberField(StringCast<TCHAR>("y"));
                                        Position.Z = PositionValue->ToSharedRef()->GetNumberField(StringCast<TCHAR>("z"));
                                }
                                if(JSONObject->GetObjectField(StringCast<TCHAR>("metadata"))->GetBoolField(StringCast<TCHAR>("isSI")))
                                {
                                        Position *= 100.0f;
                                }
                                else
                                {
                                        Position *= 10.0f;
                                }
                        }

                        float RotationValue;
                        if (JSONSampleDataObject->TryGetNumberField(StringCast<TCHAR>("rotation"), RotationValue))
                        {
                                if(JSONObject->HasField(StringCast<TCHAR>("metadata")) && JSONObject->GetObjectField(StringCast<TCHAR>("metadata"))->HasField(StringCast<TCHAR>("isDeg")))
                                {
                                        if(JSONObject->GetObjectField(StringCast<TCHAR>("metadata"))->GetBoolField(StringCast<TCHAR>("isDeg")))
                                        {
                                                Rotation = FRotator(0.0f, (-RotationValue -  90), 0.0f);
                                        }
                                        else
                                        {
                                                Rotation = FRotator(0.0f, FMath::RadiansToDegrees(-RotationValue) - 90, 0.0f);
                                        }
                                }
                                else
                                {
                                        Rotation = FRotator(0.0f, (-RotationValue -  90), 0.0f);
                                }
                        }

                        float Speed(0);
                        JSONSampleDataObject->TryGetNumberField(StringCast<TCHAR>("speed"), Speed);

                        FString Mode("");
                        JSONSampleDataObject->TryGetStringField(StringCast<TCHAR>("mode"), Mode);

                        FSimMovementSample MovementSample;
                        MovementSample.EntityID = EntityID;
                        MovementSample.Position = Position;
                        MovementSample.Rotation = Rotation;
                        MovementSample.Speed = Speed;

                        MovementSamples.Add(MovementSample);

                        AgentDataArray[EntityID].MovementData.Push(FMovementPreProcessData(Position));
                }

                AgentMovementInfoData.SimulationData.Add(CurrentDataCount, MovementSamples);

                float CurrentPercentage = (float)CurrentDataCount / (float)TargetDataCount;

                AsyncTask(ENamedThreads::GameThread, [this, CurrentPercentage]()
                {
                        OnLoadSimulationDataProgress.Broadcast(CurrentPercentage);
                });

                CurrentDataCount++;
        }
}

void FJsonDataRunnable::FinalizeProgress()
{
        AsyncTask(ENamedThreads::GameThread, [this]()
        {
                OnLoadSimulationDataProgress.Broadcast(1.0f);
                OnLoadSimulationDataProgress.Broadcast(0.0f);
        });

        CalcSmoothedStepMovementBrackets(AgentDataArray);

        FPlatformProcess::Sleep(0.5f);

        AsyncTask(ENamedThreads::GameThread, [this]()
        {
                OnLoadSimulationDataProgress.Broadcast(1.0f);
                OnLoadSimulationDataComplete.Broadcast();
        });

        FPlatformProcess::Sleep(0.5f);
}

uint32 FJsonDataRunnable:: Run()
{
        bIsRunning = true;
        AsyncTask(ENamedThreads::GameThread, [this]()
        {
                OnLoadSimulationDataProgress.Broadcast(0.0f);
        });

        bShouldStop = false;

        if (!LoadFileAndDeserialize())
        {
                bIsRunning = false;
                return 0;
        }

        bool bCalculateTimeBetweenSteps = true;
        bool bCalculateMaxTime = true;

        ProcessMetadata(bCalculateTimeBetweenSteps, bCalculateMaxTime);

        AsyncTask(ENamedThreads::GameThread, [this]()
        {
                OnMaxAgentCount.Broadcast(MaxAgents);
        });

        AgentDataArray.SetNum(MaxAgents);

        RunSimulationLoop(bCalculateTimeBetweenSteps, bCalculateMaxTime);

        FinalizeProgress();

        bIsRunning = false;
        return 0; // return 0 to indicate that the thread has ended
}
void FJsonDataRunnable::Stop()
{
	bShouldStop = true;
	bIsRunning = false;
}

void FJsonDataRunnable::Exit()
{
	FRunnable::Exit();
}

TArray<FSimMovementSample> FJsonDataRunnable::GetMovementSamples(int32 AgentID)
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
			// loop through the movement samples for this time step
			for (FSimMovementSample MovementSample : AgentMovementInfoData.SimulationData[i])
			{
				if (MovementSample.EntityID == AgentID)
				{
					MovementSamples.Add(MovementSample);
				}
			}
		}
	}

	return MovementSamples;
}

void FJsonDataRunnable::CalcSmoothedStepMovementBrackets(TArray<FAgentData> AgentSamples)
{
	// Loop through the agentsData, calculating the vectors for each agent
	for (int a = 0; a < AgentSamples.Num(); a++)
	{		
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
			SetAnimPt(t, EPedestrianMovementBracket::Emb_NotMoving, 1.0f);
			// Debug info for tracing a single person consecutive output to assess the benefits of movement bracket smoothing
			// if ((DebugAgent > -1) && (DebugAgent== a)){
			// 	double recordSpeed = RecordVectors[t].Length()/(TimeBetweenSteps);
			// 	std::cout << std::fixed << std::setprecision(2) << std::setw(4) << std::setfill('0');
			// 	std::cout << "Motion[" << std::setw(3) << t << "] = " << RecordVectors[t].Length() * 100.0 << "cm, "
			// 		<< recordSpeed << "m/s V(" << tSpan << ")step-pts " << std::endl;
			// }
		}
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

		//Calculate the current percentage of the data loaded
		float CurrentPercentage = static_cast<float>(a) / static_cast<float>(MaxAgents);
		// Broadcast the current percentage of the data loaded
		AsyncTask(ENamedThreads::GameThread, [this, CurrentPercentage]()
		{
			// Broadcast the current percentage of the data loaded as 0 this way the ui will show
			OnLoadSimulationDataProgress.Broadcast(CurrentPercentage);
		});
		
		// Now: the animation movement brackets are stored in IKVectorSteps::agentsData[nPeople].embAvatarAnims[nTimeSteps]
	}
	
}

int FJsonDataRunnable::CalculateSrcVectors(TArray<FVector>& Vec3D, FAgentData Sample)
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
void FJsonDataRunnable::SetAnimPt(int t, EPedestrianMovementBracket emb, float StepDuration)
{
	EmbAvatarAnims[t].MovementBracket = emb;
	EmbAvatarAnims[t].StepDurationMS = static_cast<unsigned long>(StepDuration * 1000.0f);

	// Set the agent animation smoothing data
	for (FSimMovementSample& MovementSample : AgentMovementInfoData.SimulationData[t])
	{
		if (MovementSample.EntityID == CurrentAgentAnimSmoothing)
		{
			MovementSample.MovementBracket = static_cast<EPedestrianMovementBracket>(emb);
			MovementSample.StepDurationMS = EmbAvatarAnims[t].StepDurationMS;
		}
	}
}

void FJsonDataRunnable::AddManyVectors(TArray<FVector>& Vec3D, int TStartStep, int TSpanStepPts, FVector& SumVec)
{
	for (int i = TStartStep; i < TStartStep + TSpanStepPts; i++){
		SumVec = SumVec + Vec3D[i];
	}
}

EPedestrianMovementBracket FJsonDataRunnable::CalculateStepAnimationParams(float CurrentSpeed, float& StepsPerSecond)
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
