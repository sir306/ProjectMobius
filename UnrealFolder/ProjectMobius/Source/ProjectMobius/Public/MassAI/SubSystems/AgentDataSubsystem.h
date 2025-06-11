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

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/ProjectMobiusInterface.h"
#include "Subsystems/WorldSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonTypes.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "HAL/Runnable.h" // FRunnable - for threading so we can get the data in the background
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "AgentDataSubsystem.generated.h"


struct FVatMovementFrames;
class FJsonDataRunnable;
/**
 * Delegates for the Agent Data Subsystem
 */
// A delegate to broadcast when the simulation data has finished loading
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoadSimulationDataComplete); // To broadcast simulation data file changes
// A delegate to broadcast the new load percentage of the pedestrian data
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoadSimulationDataProgress, float, LoadPercentage); // To broadcast simulation data file changes
// A delegate to broadcast the new max agent count
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMaxAgentCount, int32, MaxAgentCount); // To broadcast simulation data file changes

struct FMovementPreProcessData
{
	FVector Location;
	// The movement bracket
	EPedestrianMovementBracket MovementBracket = EPedestrianMovementBracket::Emb_NotMoving;
	// The step duration in milliseconds
	unsigned long StepDurationMS = 0;
};

struct FAgentData
{
	TArray<FMovementPreProcessData> MovementData;
};

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UAgentDataSubsystem : public UWorldSubsystem, public IProjectMobiusInterface //public UTickableWorldSubsystem
{
	GENERATED_BODY()
	
public:
	/** Constructor */
	UAgentDataSubsystem();

	/** Initializer */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Deinitializer */
	virtual void Deinitialize() override;

	/** Get JSON Data File */
	UFUNCTION(BlueprintCallable, Category = "MassAI|Data")
	void GetJSONDataFile(FString InJsonDataFile);

	/**
	 * Gets Json data when file has been changed 
	 */
	UFUNCTION()
	void GetUpdatedJSONDataFile();
	
	/** Build Pedestrian Movement Data */
	UFUNCTION(BlueprintCallable, Category = "MassAI|Data")
	void BuildPedestrianMovementData();

	/** Build Pedestrian Agent Info */
	UFUNCTION(BlueprintCallable, Category = "MassAI|Data")
	void BuildPedestrianAgentInfo();

	/** Get Entity Info by Index */
	UFUNCTION(BlueprintCallable, Category = "MassAI|Data")
	void SetEntityInfoByIndex(int32 Index, FEntityInfoFragment& EntityInfoFragToUpdate) const;

	/**
	 * Function bound to the delegate to broadcast for updated max agent count
	 *
	 * @param[int32] NewMaxAgentCount - The new max agent count
	 */
	UFUNCTION()
	void UpdateMaxAgentCount(int32 NewMaxAgentCount);
	
protected:
	/**
	* Check File Path Exists 
	* 
	* @param FilePath: The file path to check if it exists
	* 
	* @return bool: True if the file path exists, false if it does not
	* 
	*/
	bool CheckFilePathExists(FString FilePath);

	/**
	* Create JSON Reader and get the JSON String
	* 
	* @param OutJsonString: The JSON string to output
	* @param OutJsonReader: The JSON reader to output
	* @param JsonFile: The JSON file to load
	* 
	*/
	void CreateJsonReaderAndString(FString& OutJsonString, TSharedRef<TJsonReader<TCHAR>>& OutJsonReader, FString JsonFile);


#pragma region PROPERTIES
public:
	/** Pointer to the FRunnable JSON Parser */
	FJsonDataRunnable* JsonDataRunnable = nullptr;
	
	/** JSON Object */
	TSharedPtr<FJsonObject> JSONObject;
protected:
	/** The JSON Data File */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentData")
	FString JSONDataFile;

	/** The JSON Data String */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentData")
	FString JSONDataString;

	/** The JSON Reader */
	/*TSharedRef<TJsonReader<TCHAR>> JSONReader;*/

	

	/** Max Agents from data set */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentData")
	int32 MaxAgents;

	/** The data of all agent movements */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentData")
	//FSimulationFragment AgentMovementInfoData;

	/** QuadTree Actor this is our visualization helper */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentData")
	class AQuadTree* QuadTreeDataActor;
	
#pragma endregion PROPERTIES


public:
#pragma region GETTERS_SETTERS
	/** Get Max Agents */
	FORCEINLINE int32 GetMaxAgents() const { return MaxAgents; }

	/** Get the agent movement sample */
	//FORCEINLINE FSimulationFragment GetAgentMovementInfoData() const { return AgentMovementInfoData; }


#pragma endregion GETTERS_SETTERS
};

//#pragma pack(push, 4) // Save previous alignment and set to 4-byte alignment for storage efficiency
struct FVatAnimDataMB
{
	EPedestrianMovementBracket MovementBracket = EPedestrianMovementBracket::Emb_NotMoving; // predefined movement bracket (for animation)
	unsigned long StepDurationMS = 0; // step duration in milliseconds
};
//#pragma pack(pop)   // Restore previous alignment

class FJsonDataRunnable : public FRunnable
{
public:
	/**
	 * Constructor for the Json Data Runnable
	 * It will broadcast the percentage of the data loaded and when complete
	 *
	 * @param[FString] InJsonDataFile: The JSON data file to load
	 */
	explicit FJsonDataRunnable(FString InJsonDataFile);

	/** Destructor */
	virtual ~FJsonDataRunnable() override;
	
	// The FRunnable interface functions
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	/** Helper function to get all the movement samples for a given agent ID */
	TArray<FSimMovementSample> GetMovementSamples(int32 AgentID);

	/** Calculate smoothed step-motion animation movement brackets for each agent, using agent speeds smoothed (roughly) over a step duration */
	void CalcSmoothedStepMovementBrackets(TArray<FAgentData> AgentSamples);

	static int CalculateSrcVectors(TArray<FVector>& Vec3D, FAgentData Sample);

	// Allocate space for the animPts, to reduce fragmentation
	void AllocateAnimPts(size_t size) { EmbAvatarAnims.SetNum(size, EAllowShrinking::No); }

	void SetAnimPt(int t, EPedestrianMovementBracket emb, float StepDuration);

	// Create a 3D vector-sum over a duration of timestep records
	static void AddManyVectors(TArray<FVector>& Vec3D, int TStartStep, int TSpanStepPts, FVector& SumVec);

	// Compute the correct step animation parameters for the given speed
	EPedestrianMovementBracket CalculateStepAnimationParams(float CurrentSpeed, float& StepsPerSecond);

	/** Delegate to broadcast when the simulation data has finished loading */
	FOnLoadSimulationDataComplete OnLoadSimulationDataComplete;

	/** Delegate to broadcast new load percentages */
	FOnLoadSimulationDataProgress OnLoadSimulationDataProgress;

	/** Delegate to broadcast new max agent count */
	FOnMaxAgentCount OnMaxAgentCount;

	/** Stores the Movement data */
	FSimulationFragment AgentMovementInfoData = FSimulationFragment();

	// "Not walking" threhold taken from step length vs speed equation Fig. 10c 
	// in step extent/contact buffer 2022 paper: https://doi.org/10.1016/j.physa.2022.126927
	const double MinSpeedWalking = 0.046;

	int32 MaxAgents = 0;

	float TimeBetweenSteps = 0.0f;

	/** JSON Object */
	TSharedPtr<FJsonObject, ESPMode::ThreadSafe> JSONObject;

	int32 CurrentAgentAnimSmoothing = 0; // Current agent getting movement smoothed
	// Peter Thompsons Smoothed Step Motion Animation Movement Variables
	double minimumStepDuration = 0.5; // Minimum step duration in seconds, to assess suitable animation
	double maximumStepDuration = 1.0; // Maximum step duration in seconds, to assess suitable animation
	int minTimedSrcRecordsForStep = 5; // Min. num. time steps to forward-assess
	int maxTimedSrcRecordsForStep = 10; // Max. num. time steps to forward-assess
	double timeDurationPerRecord = 0.1; // Time duration per record in seconds
	TArray<FVatAnimDataMB> EmbAvatarAnims; // Optionally available for animation data
	TArray<FVelocityVector2D> StepVectors; // Angular vectors, smoothed across estimated steps/strides
	bool calculatedStepAnimationParams = false; // Flag to indicate that the step animation parameters have been calculated
	TArray<FAgentData> AgentDataArray;

	bool bIsRunning = false; // Flag to indicate if the thread is running
	
protected:
	/** Pointer to a thread */
	FRunnableThread* Thread = nullptr;

	FString JsonFilePath = FString();

	/** The JSON Data File */
	FString JsonDataFile = FString();
	
	/** Current Data Count */
	int32 CurrentDataCount = 0;

	/** Target Data Count */
	int32 TargetDataCount = 0;

	/** Bool to tell when the thread should stop */
	bool bShouldStop = false;
	
};

