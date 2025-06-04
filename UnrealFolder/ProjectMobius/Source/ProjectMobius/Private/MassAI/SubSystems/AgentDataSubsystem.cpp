// Fill out your copyright notice in the Description page of Project Settings.


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
#include "MassAI/SubSystems/TimeDilationSubSystem.h"
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
		FEntityInfoFragment EntityInfo(EntityID, EntityName, EntitySimTime, EntityMaxSpeed, EntityM_Plane, EntityMap);

		// Add the entity info fragment to the entity info array
		
	}
}

void UAgentDataSubsystem::SetEntityInfoByIndex(int32 Index, FEntityInfoFragment& EntityInfoFragToUpdate) const
{
	if (Index < 0 || Index >= MaxAgents)
	{
		UE_LOG(LogTemp, Warning, TEXT("Index out of range"));
		return;
	}
	
	// // Check that the JSON Object is valid
	// if (!JSONObject->GetArrayField("entities").IsEmpty())
	//    {
	//        UE_LOG(LogTemp, Warning, TEXT("Invalid JSON Object"));
	//        return;
	//    }
	//
	// // Get the JSON object for this index is valid
	// if (!JSONObject->GetArrayField("entities")[Index]->AsObject().IsValid())
	//    {
	//        UE_LOG(LogTemp, Warning, TEXT("Invalid JSON Object"));
	//        return;
	//    }
	
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

	// update gender
	EntityInfoFragToUpdate.bIsMale = !(EntityInfoFragToUpdate.EntityName.Contains("Female"));

	// update age demographic
	if (EntityInfoFragToUpdate.EntityName.Contains("Child"))
	{
		EntityInfoFragToUpdate.AgeDemographic = EAgeDemographic::Ead_Child;
	}
	else if (EntityInfoFragToUpdate.EntityName.Contains("Elderly"))
	{
		EntityInfoFragToUpdate.AgeDemographic = EAgeDemographic::Ead_Elderly;
	}
	else if (EntityInfoFragToUpdate.EntityName.Contains("Adult"))
	{
		EntityInfoFragToUpdate.AgeDemographic = EAgeDemographic::Ead_Adult;

		//DEBUG: Sample data has adults with gender not elderly
		// so create rand bool to set to elderly to see a mix of adults and elderly in test sim
		// if (FMath::FRandRange(0.0f, 1.0f) > 0.5f)
		// {
		// 	EntityInfoFragToUpdate.AgeDemographic = EAgeDemographic::Ead_Elderly;
		// }
	}
	else // no valid age demographic found -> TODO: for now just set it to adult but need to think on how we want to handle this
	{
		EntityInfoFragToUpdate.AgeDemographic = EAgeDemographic::Ead_Adult;
	}

	// These are defaults but respawning agents that have this set will still be set to false
	// Ensure they are set to be rendered
	EntityInfoFragToUpdate.bRenderAgent = true;

	// Set the entity to not be ready to destroy
	EntityInfoFragToUpdate.bReadyToDestroy = false;
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

uint32 FJsonDataRunnable:: Run()
{
	bIsRunning = true;
	// Broadcast the current percentage of the data loaded
	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		// Broadcast the current percentage of the data loaded as 0 this way the ui will show
		OnLoadSimulationDataProgress.Broadcast(0.0f);
	});
	
	// ensure the tread is not stopped
	bShouldStop = false;

	// check file actually exists before creating the thread
	if (!FPaths::FileExists(JsonFilePath))
	{
		// TODO: this needs to broadcast error message to the UI
		// We cant log here as this is a separate thread
		bShouldStop = true;
	}
	
	// Load File to String
	FFileHelper::LoadFileToString(JsonDataFile, *JsonFilePath);

	// Create JSON Reader
	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonDataFile);

	// Deserialize JSON Data
	bool bDeserializeSuccess = FJsonSerializer::Deserialize(JsonReader, JSONObject);

	// if the deserialization was not successful, log it
	if (!bDeserializeSuccess)
	{
		// TODO: this needs to broadcast error message to the UI
		// We cant log here as this is a separate thread
		bShouldStop = true;
	}
	

	bool bCalculateTimeBetweenSteps = true;
	bool bCalculateMaxTime = true;

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
		if(JSONMetaDataObject->HasField(StringCast<TCHAR>("duration")) && JSONMetaDataObject->HasField(StringCast<TCHAR>("sampling_rate")) && JSONMetaDataObject->GetNumberField(StringCast<TCHAR>("duration")) > 0 && JSONMetaDataObject->GetNumberField(StringCast<TCHAR>("sampling_rate")) > 0)
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
	
	

	// Broadcast the max agent count -- this is done on the game thread
	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		// Broadcast the max agent count
		OnMaxAgentCount.Broadcast(MaxAgents);
	});

	// Size AgentDataArray to the max agents
	AgentDataArray.SetNum(MaxAgents);

	// get the simulation data array
	TArray<TSharedPtr<FJsonValue>> JsonSimDataArray = JSONObject->GetArrayField(StringCast<TCHAR>("simulation"));
	
	// keep looping until the thread is stopped or the current data count is equal to the target data count
	while (!bShouldStop && CurrentDataCount <= TargetDataCount)
	{
		// Check that the JSON Object is valid
		if (!JsonSimDataArray.IsValidIndex(CurrentDataCount) || !JsonSimDataArray[CurrentDataCount]->AsObject().IsValid())
		{
			// TODO: this needs to broadcast error message to the UI
			// We cant log here as this is a separate thread
			bShouldStop = true;
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
		minTimedSrcRecordsForStep = (int)std::round(minimumStepDuration *(int)std::round(((double)1.0 / (double)TimeBetweenSteps))); // Min. num. time steps to forward-assess
		maxTimedSrcRecordsForStep = (int)std::round(maximumStepDuration * (double)TimeBetweenSteps); // Max. num. time steps to forward-assess
		timeDurationPerRecord = 1.0 / (double)(int)std::round(((double)1.0 / (double)TimeBetweenSteps));
		
		// get the sample array for this
		TArray<TSharedPtr<FJsonValue>> JSONSampleArray = JSONSimDataObject->GetArrayField(StringCast<TCHAR>("samples"));

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
			if (!JSONSampleArray[JsimSample]->AsObject().IsValid())
			{
				// TODO: this needs to broadcast error message to the UI
				// We cant log here as this is a separate thread
				continue;
			}
		
			// Get the JSON object
			TSharedPtr<FJsonObject> JSONSampleDataObject = JSONSampleArray[JsimSample]->AsObject();
		
			// Get the entity ID
			int32 EntityID;

			if(!JSONSampleDataObject->TryGetNumberField(StringCast<TCHAR>("entity"), EntityID))
			{
				// TODO: this needs to broadcast error message to the UI
				// We cant log here as this is a separate thread
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
				if(JSONObject->GetObjectField(StringCast<TCHAR>("metadata"))->GetBoolField(StringCast<TCHAR>("isSI")))
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
				// TODO: this needs to broadcast error message to the UI
				// We cant log here as this is a separate thread
			}
			
			// Get the Rotation field which is in degrees -- TODO: if meta present for rotation unit need to see if it is in degrees or radians
			float RotationValue;
			
			// try get the rotation value
			if (JSONSampleDataObject->TryGetNumberField(StringCast<TCHAR>("rotation"), RotationValue))
			{
				// if the metadata contains isDeg then we know the rotation is in degrees otherwise it is in radians
				if(JSONObject->HasField(StringCast<TCHAR>("metadata")) && JSONObject->GetObjectField(StringCast<TCHAR>("metadata"))->HasField(StringCast<TCHAR>("isDeg")))
				{
					// is it degrees
					if(JSONObject->GetObjectField(StringCast<TCHAR>("metadata"))->GetBoolField(StringCast<TCHAR>("isDeg")))
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
				// TODO: this needs to broadcast error message to the UI
				// We cant log here as this is a separate thread
			}
		
			// Get the speed
			float Speed(0);

			// try get the speed value
			if (!JSONSampleDataObject->TryGetNumberField(StringCast<TCHAR>("speed"), Speed))
			{
				// throw error message
			}
		
			// Get the mode
			FString Mode("");

			// try get the mode string value
			if (!JSONSampleDataObject->TryGetStringField(StringCast<TCHAR>("mode"), Mode))
			{
				// throw error message
			}
		
			// Create the movement sample
			FSimMovementSample MovementSample(EntityID, Position, Rotation, Speed, Mode);
			
			// Add the movement sample
			MovementSamples.Add(MovementSample);
			
			AgentDataArray[EntityID].MovementData.Push(FMovementPreProcessData(Position));
		}

		// // if movement sample is empty then log
		// if (MovementSamples.Num() == 0)
		// {
		// 	AsyncTask(ENamedThreads::GameThread, [this]()
		// 	{
		// 		// log that movement sample is empty
		// 		UE_LOG(LogTemp, Display, TEXT("Movement sample empty for time step %d"), CurrentDataCount);
		// 	});
		// }
		// // else log the number of movement samples
		// else
		// {
		// 	AsyncTask(ENamedThreads::GameThread, [this, MovementSamples]()
		// 	{
		// 		// log the number of movement samples
		// 		UE_LOG(LogTemp, Display, TEXT("Movement sample count: %d"), MovementSamples.Num());
		// 	});
		// }

		AgentMovementInfoData.AddMovementSample(CurrentDataCount, MovementSamples);
		
	
		// Calculate the current percentage of the data loaded
		float CurrentPercentage = (float)CurrentDataCount / (float)TargetDataCount;

		// Broadcast the current percentage of the data loaded -- this is done on the game thread
		AsyncTask(ENamedThreads::GameThread, [this, CurrentPercentage]()
		{
			// Broadcast the current percentage of the data loaded
			OnLoadSimulationDataProgress.Broadcast(CurrentPercentage);
		});
		
		// Increment the current data count
		CurrentDataCount++;
	}

	// Perform Animation Preprocessing data here
	// Broadcast the current percentage of the data loaded
	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		// Broadcast the current percentage of the data loaded as 0 this way the ui will show
		OnLoadSimulationDataProgress.Broadcast(1.0f);
		OnLoadSimulationDataProgress.Broadcast(0.0f);// NEED to broadcast new load text here
	});
	
	
	CalcSmoothedStepMovementBrackets(AgentDataArray);
	// for (int32 i = 0; i < MaxAgents; i++)
	// {
	// 	TArray<FSimMovementSample> CurrentAgentSample = GetMovementSamples(i);
	// 	CurrentAgentAnimSmoothing=i;
	// 	CalcSmoothedStepMovementBrackets(AgentDataArray);
	// 	// Calculate the current percentage of the data loaded
	// 	float CurrentPercentage = (float)i / (float)MaxAgents;
	// 	// Broadcast the current percentage of the data loaded
	// 	AsyncTask(ENamedThreads::GameThread, [this, CurrentPercentage]()
	// 	{
	// 		// Broadcast the current percentage of the data loaded as 0 this way the ui will show
	// 		OnLoadSimulationDataProgress.Broadcast(CurrentPercentage);
	// 	});
	// }
	// ParallelFor(MaxAgents, [&](int32 i)
	// {
	// 	TArray<FSimMovementSample> CurrentAgentSample = GetMovementSamples(i);
	// 	CalcSmoothedStepMovementBrackets(CurrentAgentSample);
	//
	// 	// Calculate the current percentage of the data loaded
	// 	float CurrentPercentage = (float)i / (float)MaxAgents;
	//
	// 	// Broadcast the progress on the Game Thread
	// 	AsyncTask(ENamedThreads::GameThread, [this, CurrentPercentage]()
	// 	{
	// 		OnLoadSimulationDataProgress.Broadcast(CurrentPercentage);
	// 	});
	// });

	// let the thread sleep for 0.5 second
	FPlatformProcess::Sleep(0.5f);
	
	// Broadcast that the simulation data has been loaded -- this is done on the game thread
	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		// Broadcast the current percentage of the data loaded
		OnLoadSimulationDataProgress.Broadcast(1.0f); 
		// Broadcast that the simulation data has been loaded
		OnLoadSimulationDataComplete.Broadcast();
	});
	
	// let the thread sleep for 0.5 second
	FPlatformProcess::Sleep(0.5f);
	
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
