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

#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
// File Parser for JSON
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonTypes.h"

FSimulationFragment::FSimulationFragment()
{
	// Initialize the simulation data
	SimulationData = TMap<int32, TArray<FSimMovementSample>>(); //TODO: possibly preallocate the size of the map
	MaxTime = 0.0f;
	// HARD CODED FILE PATH FOR TESTING
	FString JSON_FilePath = "C:\\Users\\User_VR4\\Desktop\\WORK\\ProjectMobius\\ProjectMobius\\TestData\\TechnicalSchool1000People\\TechnicalSchool_1000.json";
	//FString JSON_FilePath = "D:\\MastersAndMobius\\ProjectMobius\\TestData\\TechnicalSchool1000People\\TechnicalSchool_1000.json";
	//FString JSON_FilePath = "C:\\Users\\User_VR4\\Desktop\\WORK\\ProjectMobius\\ProjectMobius\\TestData\\iso-test-json-1.json";
		//"D:\\1_Work\\Mobius\\ProjectMobius\\TestData\\iso-test-json-1.json";
		//"D:\\MastersAndMobius\\ProjectMobius\\TestData\\iso-test-json-1.json";

	//GetSimulationData(JSON_FilePath);
	//AsyncGetSimData(JSON_FilePath);
}

FSimulationFragment::FSimulationFragment(const FString& InJsonDataPath)
{
	// Initialize the simulation data
	SimulationData = TMap<int32, TArray<FSimMovementSample>>(); //TODO: possibly preallocate the size of the map
	MaxTime = 0.0f; // Set to 0.0f as the max time is not known yet and could crash if not set
	GetSimulationData(InJsonDataPath);
	//AsyncGetSimData(InJsonDataPath);
}

FSimulationFragment::~FSimulationFragment()
{
}

void FSimulationFragment::AddMovementSample(int32 TimeStep, TArray<FSimMovementSample> MovementSample)
{
	SimulationData.Add(TimeStep, MovementSample);
}

TArray<FSimMovementSample> FSimulationFragment::BuildMovementSample(float TimeVal)
{
	return TArray<FSimMovementSample>();
}

void FSimulationFragment::GetSimulationData(FString JSONData)
{
	// Check file path
	if (FPaths::FileExists(JSONData))
	{
		// Read the file
		FString JSONDataString;
		FFileHelper::LoadFileToString(JSONDataString, *JSONData);

		// Parse the JSON data
		TSharedPtr<FJsonObject> JSONDataObject;
		TSharedRef<TJsonReader<TCHAR>> JSONReader = TJsonReaderFactory<TCHAR>::Create(JSONDataString);

		if (FJsonSerializer::Deserialize(JSONReader, JSONDataObject))
		{
			// Get the JSON array
			TArray<TSharedPtr<FJsonValue>> JsonSimDataArray = JSONDataObject->GetArrayField(TEXT("simulation"));
			
			// loop through the JSON array
			for (int32 i_AllSimData = 0; i_AllSimData < JsonSimDataArray.Num(); i_AllSimData++)
			{
				/*if(i_AllSimData >= 22){
					UE_LOG(LogTemp, Warning, TEXT("Index: %d"), i_AllSimData);
				}*/

				if (!JsonSimDataArray[i_AllSimData]->AsObject().IsValid())
				{
					UE_LOG(LogTemp, Warning, TEXT("Invalid JSON Object"));
					break;
				}

				// Get the JSON object for this 
				TSharedPtr<FJsonObject> JSONSimDataObject = JsonSimDataArray[i_AllSimData]->AsObject();

				// get time field
				float TimeVal = JSONSimDataObject->GetNumberField(TEXT("time"));

				//TODO: This needs to be optimized but will do for now
				MaxTime = TimeVal;

				// Log the time value
				// UE_LOG(LogTemp, Warning, TEXT("Time Value: %f"), TimeVal);

				// get the sample array for this
				TArray<TSharedPtr<FJsonValue>> JSONSampleArray = JSONSimDataObject->GetArrayField(TEXT("samples"));

				// create a movement sample array
				TArray<FSimMovementSample> MovementSamples;

				// loop through the sample array and build the movement sample values
				for (int32 JsimSample = 0; JsimSample < JSONSampleArray.Num(); JsimSample++)
				{
					if (!JSONSampleArray[JsimSample]->AsObject().IsValid())
					{
						UE_LOG(LogTemp, Warning, TEXT("Invalid JSON Object"));
						continue;
					}
				
					// Get the JSON object
					TSharedPtr<FJsonObject> JSONSampleDataObject = JSONSampleArray[JsimSample]->AsObject();
				
					// Get the entity ID
					int32 EntityID = JSONSampleDataObject->GetIntegerField(TEXT("entity"));
				
					// Initilize the position variable
					FVector Position = FVector::ZeroVector;
				
					// Get the Position field
					TSharedPtr<FJsonObject> PositionValue = JSONSampleDataObject->GetObjectField(TEXT("position"));
					if (PositionValue.IsValid())
					{
						// Map the values to the position
						Position.X = PositionValue->GetNumberField(TEXT("x"));
						Position.Y = -PositionValue->GetNumberField(TEXT("y"));
						Position.Z = PositionValue->GetNumberField(TEXT("z"));
					}
					else
					{
						// Handle missing or invalid "position" field.
						UE_LOG(LogTemp, Warning, TEXT("Missing or invalid 'position' field. Position will be set to 0,0,0"));
				
					}
				
					// Initialize the rotation variable
					FRotator Rotation = FRotator::ZeroRotator;
				
					// Get the Rotation field
					float RotationValue = JSONSampleDataObject->GetNumberField(TEXT("rotation"));
					if (PositionValue.IsValid())
					{
						// convert the degree rotation value to x,y,z // the minus 90 is to adjust the rotation to the correct direction for mesh needs better handle on this
						Rotation = FRotator(0.0f, (-RotationValue -  90), 0.0f);//TODO: this is correct(for test data) and add method for different modeling studios
					}
					else
					{
						// Handle missing or invalid "rotation" field.
						UE_LOG(LogTemp, Warning, TEXT("Missing or invalid 'rotation' field. Rotation will be set to 0,0,0"));
				
					}
				
					// Get the speed
					float Speed = JSONSampleDataObject->GetNumberField(TEXT("speed"));
				
					// Get the mode
					FString Mode = JSONSampleDataObject->GetStringField(TEXT("mode"));
				
					// Create the movement sample
					FSimMovementSample MovementSample(EntityID, Position, Rotation, Speed, Mode);
					
					/*if (MovementSamples.Num() >= 22) {
						UE_LOG(LogTemp, Warning, TEXT("Movement Samples Debug"));
					}*/
				
					// Add the movement sample
					MovementSamples.Add(MovementSample);
					
					/*if (MovementSamples.Num() >= 22) {
						UE_LOG(LogTemp, Warning, TEXT("Movement Samples Debug"));
					}*/
				}

				/*if (i_AllSimData >= 22) {
					UE_LOG(LogTemp, Warning, TEXT("Index: %d"), i_AllSimData);
				}
				if (SimulationData.Num() >= 22) {
					
					UE_LOG(LogTemp, Warning, TEXT("SimulationData Debug"));
				}*/
				//auto* test = &MovementSamples[-1];
				///*FSimMovementSample* fSimSample = nullptr;
				//if (MovementSamples.Num() > 0) {
				//	fSimSample = &SimulationData[-1];
				//}	*/
				//TMap<float, TArray<FSimMovementSample>>* test2 = &SimulationData;
				//TArray<FSimMovementSample>* test3 = &SimulationData[-1];
				//if (!SimulationData.IsEmpty()) {
				//	test2[-1];
				//}
				// add the movement sample values to the simulation data
				AddMovementSample(i_AllSimData, MovementSamples);
				/*auto* test4 = &MovementSamples[-1];
				auto* test5 = &SimulationData[-1];*/
				//if (SimulationData.Num() >= 22) {
					
					//UE_LOG(LogTemp, Warning, TEXT("SimulationData Debug"));
				//}
			}
			// log number of movement samples
			//UE_LOG(LogTemp, Warning, TEXT("Number of Movement Samples: %d"), SimulationData.Num());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("File not found!"));
	}
}

void FSimulationFragment::AsyncGetSimData(FString JSONData)
{
	AsyncTask(ENamedThreads::AnyBackgroundHiPriTask, [this, JSONData]()
	{
		//GetSimulationData(JSONData);

		// Check file path
		if (FPaths::FileExists(JSONData))
		{
			// Read the file
			FString JSONDataString;
			FFileHelper::LoadFileToString(JSONDataString, *JSONData);

			// Parse the JSON data
			TSharedPtr<FJsonObject> JSONDataObject;
			TSharedRef<TJsonReader<TCHAR>> JSONReader = TJsonReaderFactory<TCHAR>::Create(JSONDataString);

			if (FJsonSerializer::Deserialize(JSONReader, JSONDataObject))
			{
				// Get the JSON array
				TArray<TSharedPtr<FJsonValue>> JsonSimDataArray = JSONDataObject->GetArrayField(TEXT("simulation"));
				
				// loop through the JSON array
				for (int32 i_AllSimData = 0; i_AllSimData < JsonSimDataArray.Num(); i_AllSimData++)
				{
					/*if(i_AllSimData >= 22){
						UE_LOG(LogTemp, Warning, TEXT("Index: %d"), i_AllSimData);
					}*/

					if (!JsonSimDataArray[i_AllSimData]->AsObject().IsValid())
					{
						UE_LOG(LogTemp, Warning, TEXT("Invalid JSON Object"));
						break;
					}

					// Get the JSON object for this 
					TSharedPtr<FJsonObject> JSONSimDataObject = JsonSimDataArray[i_AllSimData]->AsObject();

					// get time field
					float TimeVal = JSONSimDataObject->GetNumberField(TEXT("time"));

					//// Log the time value
					// UE_LOG(LogTemp, Warning, TEXT("Time Value: %f"), TimeVal);

					// get the sample array for this
					TArray<TSharedPtr<FJsonValue>> JSONSampleArray = JSONSimDataObject->GetArrayField(TEXT("samples"));

					// create a movement sample array
					TArray<FSimMovementSample> MovementSamples;

					// loop through the sample array and build the movement sample values
					for (int32 JsimSample = 0; JsimSample < JSONSampleArray.Num(); JsimSample++)
					{
						if (!JSONSampleArray[JsimSample]->AsObject().IsValid())
						{
							UE_LOG(LogTemp, Warning, TEXT("Invalid JSON Object"));
							continue;
						}
					
						// Get the JSON object
						TSharedPtr<FJsonObject> JSONSampleDataObject = JSONSampleArray[JsimSample]->AsObject();
					
						// Get the entity ID
						int32 EntityID = JSONSampleDataObject->GetIntegerField(TEXT("entity"));
					
						// Initilize the position variable
						FVector Position = FVector::ZeroVector;
					
						// Get the Position field
						TSharedPtr<FJsonObject> PositionValue = JSONSampleDataObject->GetObjectField(TEXT("position"));
						if (PositionValue.IsValid())
						{
							// Map the values to the position
							Position.X = PositionValue->GetNumberField(TEXT("x"));
							Position.Y = -PositionValue->GetNumberField(TEXT("y"));
							Position.Z = PositionValue->GetNumberField(TEXT("z"));
						}
						else
						{
							// Handle missing or invalid "position" field.
							UE_LOG(LogTemp, Warning, TEXT("Missing or invalid 'position' field. Position will be set to 0,0,0"));
					
						}
					
						// Initialize the rotation variable
						FRotator Rotation = FRotator::ZeroRotator;
					
						// Get the Rotation field
						float RotationValue = JSONSampleDataObject->GetNumberField(TEXT("rotation"));
						if (PositionValue.IsValid())
						{
							// convert the degree rotation value to x,y,z // the minus 90 is to adjust the rotation to the correct direction for mesh needs better handle on this
							Rotation = FRotator(0.0f, (-RotationValue -  90), 0.0f);//TODO: this is correct(for test data) and add method for different modeling studios
						}
						else
						{
							// Handle missing or invalid "rotation" field.
							UE_LOG(LogTemp, Warning, TEXT("Missing or invalid 'rotation' field. Rotation will be set to 0,0,0"));
					
						}
					
						// Get the speed
						float Speed = JSONSampleDataObject->GetNumberField(TEXT("speed"));
					
						// Get the mode
						FString Mode = JSONSampleDataObject->GetStringField(TEXT("mode"));
					
						// Create the movement sample
						FSimMovementSample MovementSample(EntityID, Position, Rotation, Speed, Mode);
						
						/*if (MovementSamples.Num() >= 22) {
							UE_LOG(LogTemp, Warning, TEXT("Movement Samples Debug"));
						}*/


						// Add the movement sample
						MovementSamples.Add(MovementSample);
						
						/*if (MovementSamples.Num() >= 22) {
							UE_LOG(LogTemp, Warning, TEXT("Movement Samples Debug"));
						}*/
					}

					/*if (i_AllSimData >= 22) {
						UE_LOG(LogTemp, Warning, TEXT("Index: %d"), i_AllSimData);
					}
					if (SimulationData.Num() >= 22) {
						
						UE_LOG(LogTemp, Warning, TEXT("SimulationData Debug"));
					}*/
					//auto* test = &MovementSamples[-1];
					///*FSimMovementSample* fSimSample = nullptr;
					//if (MovementSamples.Num() > 0) {
					//	fSimSample = &SimulationData[-1];
					//}	*/
					//TMap<float, TArray<FSimMovementSample>>* test2 = &SimulationData;
					//TArray<FSimMovementSample>* test3 = &SimulationData[-1];
					//if (!SimulationData.IsEmpty()) {
					//	test2[-1];
					//}
					
					// add the movement sample values to the simulation data
					SimulationData.Add(i_AllSimData, MovementSamples);
					//AddMovementSample(i_AllSimData, MovementSamples);

					/*auto* test4 = &MovementSamples[-1];
					auto* test5 = &SimulationData[-1];*/
					//if (SimulationData.Num() >= 22) {
						
						//UE_LOG(LogTemp, Warning, TEXT("SimulationData Debug"));
					//}
				}
				// log number of movement samples
				//UE_LOG(LogTemp, Warning, TEXT("Number of Movement Samples: %d"), SimulationData.Num());
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("File not found!"));
		}
		// If you need to perform any UI or game-thread-related operations after this,
		// you can use `AsyncTask(ENamedThreads::GameThread, [](){})` to switch back to the main thread.
		// AsyncTask(ENamedThreads::GameThread, [this]()
		// {
		// 	// Execute anything that needs to run on the game thread, like updating the UI
		// 	UE_LOG(LogTemp, Warning, TEXT("Finished parsing simulation data"));
		// });
	});
}

double FVelocityVector2D::InferStepDuration()
{
	// Min. step duration inference speed 0.26547 m/s is derived from 
	// Wang 2018: Table 4 https://doi.org/10.1016/j.physa.2018.02.021
	// Step length = 0.613 x pow(v, 0.631), equates to Step time = 0.613 x pow(v, -0.369)
	// Step duration trends above 1.0 second for v at or below 0.26547 m/s
	// At these speeds, the step length is less than 0.265m, which is ~ an average shoe length
	// and, so we know that feet are not lifting off the ground, so definitely shuffling
	// and therefore we cannot rely on regular step frequency. 
	// Alternatives are: 
	// 0.3m/s (Sl = 0.287m) as a generic threshold as a rounded figure
	// or 0.38062 m/s (Sl=0.333m) which is  the speed at which we transition from shuffle to slow walk
	// (https://doi.org/10.1016/j.physa.2022.126927)
	// We are not using the step frequency 4th order polynomial from the step extent/contact buffer paper
	// because that level of complexity just seems computationally excessive for this application

	if (Speed > 0.26547) {
		double duration = 0.613 * pow(Speed, -0.369);
		return duration;
	}
	else return 1.0;
}

const double FVelocityVector2D::InferStrideDuration()
{
	return 2.0 * InferStepDuration();
}

FSimMovementSample::FSimMovementSample()
{
	EntityID = 0;
	Position = FVector::ZeroVector;
	Rotation = FRotator::ZeroRotator;
	Speed = 0.0f;
	Mode = "";
	MovementBracket = EPedestrianMovementBracket::Emb_NotMoving;
	StepDurationMS = 0;
	StepVector = FVelocityVector2D();
}

FSimMovementSample::FSimMovementSample(int32 InEntityID, FVector InPosition, FRotator InRotation, float InSpeed, FString InMode)
{
	EntityID = InEntityID;
	Position = InPosition;
	Rotation = InRotation;
	Speed = InSpeed;
	Mode = InMode;
	MovementBracket = EPedestrianMovementBracket::Emb_NotMoving;
	StepDurationMS = 0;
	StepVector = FVelocityVector2D();
}

FSimMovementSample::FSimMovementSample(int32 InEntityID, FVector InPosition, FRotator InRotation, float InSpeed,
	FString InMode, EPedestrianMovementBracket InMovementBracket, unsigned long InStepDurationMS,
	FVelocityVector2D InStepVector)
{
	EntityID = InEntityID;
	Position = InPosition;
	Rotation = InRotation;
	Speed = InSpeed;
	Mode = InMode;
	MovementBracket = InMovementBracket;
	StepDurationMS = InStepDurationMS;
	StepVector = InStepVector;
}

FSimMovementSample::~FSimMovementSample()
{
}
