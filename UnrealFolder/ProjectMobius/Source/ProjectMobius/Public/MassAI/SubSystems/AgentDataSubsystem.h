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
#include "MobiusAgentDataImporter.h"
#include "Interfaces/ProjectMobiusInterface.h"
#include "Subsystems/WorldSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonTypes.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "HAL/Runnable.h" // FRunnable - for threading so we can get the data in the background
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "Templates/UniquePtr.h"
#include "AgentDataSubsystem.generated.h"


struct FVatMovementFrames;
class FProcessAgentSimulationDataRunnable;
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
class PROJECTMOBIUS_API UAgentDataSubsystem : public UTickableWorldSubsystem, public IProjectMobiusInterface
{
	GENERATED_BODY()
	
public:
	/** Constructor */
	UAgentDataSubsystem();

	/** Destructor */
	virtual ~UAgentDataSubsystem() override;

	/** Initializer */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Deinitializer */
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAgentDataSubsystem, STATGROUP_Tickables); }

	/** Path of the currently-imported agent-data file, empty when no import runnable exists (A4: the
	 *  spawn subsystem uses it to locate the matching .msc cache when force-streaming is enabled). */
	FString GetLoadedSimulationDataFilePath() const;

	/** Helper used to copy imported entity metadata into an EntityInfoFragment. */
	static void ParseEntityInfo(const FMobiusAgentEntityData& Entity, FEntityInfoFragment& OutInfo);

	/**
	 * Set the Entity Info fragment by Index from the JSON data
	 *
	 * @param[int32] Index The index of the entity to set
	 * @param[FEntityInfoFragment&] EntityInfoFragToUpdate The entity info fragment to update
	 */
	UFUNCTION(BlueprintCallable, Category = "MassAI|Data")
	void SetEntityInfoByIndex(int32 Index, FEntityInfoFragment& EntityInfoFragToUpdate) const;

	/**
	 * Set the Entity Rendering fragment by Index from the JSON data
	 * 
	 * @param[int32] Index The index of the entity to set
	 * @param[FEntityRenderingFragment&] EntityRenderingFragToUpdate The entity rendering fragment to update
	 */
	void SetEntityRenderingByIndex(int32 Index, FEntityRenderingFragment& EntityRenderingFragToUpdate) const;

	/**
	 * Function bound to the delegate to broadcast for updated max agent count
	 *
	 * @param[int32] NewMaxAgentCount - The new max agent count
	 */
	UFUNCTION()
	void UpdateMaxAgentCount(int32 NewMaxAgentCount);

	/**
	 * Drop cached-per-file state on file switch: CachedEntityData, ProgressQueue,
	 * MaxAgentsQueue, LoadingTaskQueue. Called from MassEntitySpawnSubsystem at the
	 * start of CreatePedestrianTemplateData so prior file residue isn't held through
	 * the next load.
	 */
	void ClearPerFileState();

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
        /** Pointer to the FRunnable agent data processor */
        TUniquePtr<FProcessAgentSimulationDataRunnable> AgentDataRunnable;

	/**
	 * Entity metadata cached before the runnable is torn down.
	 * Populated in BuildPedestrianMovementFragmentData() so that
	 * PedestrianInitializeMOP can access entity info after AgentDataRunnableCleanup
	 * has already destroyed AgentDataRunnable.
	 */
	TArray<FMobiusAgentEntityData> CachedEntityData;

	/** Delegate to broadcast when the simulation data has finished loading */
	UPROPERTY()
	FOnLoadSimulationDataComplete OnLoadSimulationDataComplete;

	/** Delegate to broadcast new load percentages */
	UPROPERTY()
	FOnLoadSimulationDataProgress OnLoadSimulationDataProgress;

	/** Delegate to broadcast new max agent count */
	UPROPERTY()
	FOnMaxAgentCount OnMaxAgentCount;

	UPROPERTY()
	bool bIsDataLoaded = false; // Flag to indicate if the data has been loaded

	TQueue<float,EQueueMode::Mpsc> ProgressQueue; // Queue to hold progress updates
	TQueue<FString,EQueueMode::Mpsc> LoadingTaskQueue; // Queue to hold loading task updates and inform current loading task
	TQueue<int32,EQueueMode::Mpsc> MaxAgentsQueue;
	
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

	UPROPERTY(EditAnywhere)
	float LoadProgress;

	UPROPERTY(EditAnywhere)
	FString CurrentLoadingTask = FString();

	
	
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

/**
 * Wall-clock phase timings of one import run (PRD 02 T6). Filled by
 * FProcessAgentSimulationDataRunnable::Run; consumed by the Mobius.Timing.* automation tests
 * (ratio assertions + trend CSV). All values in seconds; phases that did not run stay 0.
 */
struct FMobiusImportTimings
{
	/** LoadFileAndDeserialize: source bytes -> FMobiusAgentSimulationData (JSON/HDF5 parse). */
	double ParseSeconds = 0.0;
	/** RunSimulationDataGatheringLoop: dense per-timestep map build + unit conversions. */
	double ConvertSeconds = 0.0;
	/** CalcSmoothedStepMovementBrackets. */
	double BracketSeconds = 0.0;
	/** WriteCacheForImport (A3; includes the reuse-check when it skips). */
	double CacheWriteSeconds = 0.0;
	/** TryFastReloadFromCache (A6) when it succeeded. */
	double FastReloadSeconds = 0.0;
	/** Whole Run(). */
	double TotalSeconds = 0.0;
	/** True when this run served the .msc cache instead of parsing the source. */
	bool bUsedFastReload = false;
};

/**
 * Runnable class to process simulation data in a separate thread
 */
class PROJECTMOBIUS_API FProcessAgentSimulationDataRunnable : public FRunnable
{
public:
	/**
	 * Constructor for the Json Data Runnable
	 * It will broadcast the percentage of the data loaded and when complete
	 *
	 * @param[FString] InJsonDataFile: The JSON data file to load
	 * @param bAutoStartThread When false, no worker thread is spawned and the caller drives Run()
	 *        itself — automation tests use this to execute the import synchronously and inspect
	 *        the produced structures before destruction (the destructor resets them). Production
	 *        code always uses the default.
	 */
	explicit FProcessAgentSimulationDataRunnable(FString InAgentDataFile, TWeakObjectPtr<UAgentDataSubsystem> Owner, bool bAutoStartThread = true);

	/** Destructor */
	virtual ~FProcessAgentSimulationDataRunnable() override;
	
	// The FRunnable interface functions
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	/** Helper function to get all the movement samples for a given agent ID */
	TArray<FSimMovementSample> GetMovementSamples(int32 AgentID);

	/** Source agent-data file this runnable is importing (A4: keys the .msc cache lookup). */
	const FString& GetSimulationDataFilePath() const { return SimulationDataFilePath; }

	/** Calculate smoothed step-motion animation movement brackets for each agent, using agent speeds smoothed (roughly) over a step duration */
	void CalcSmoothedStepMovementBrackets(const TArray<FAgentData>& AgentSamples);

	/**
	 * @brief Calculates rotation from movement direction when the HDF5 source data lacks rotation information.
	 *
	 * This method is called when AgentSimulationData.Metadata.bHasRotationData is false, indicating that
	 * the source file did not contain rotation/heading data for entities.
	 *
	 * @par Algorithm:
	 * For each entity:
	 * 1. Collect all position samples ordered by timestep
	 * 2. For each timestep, calculate direction vector to the NEXT position (look-ahead approach)
	 * 3. Convert direction to rotation angle in degrees using FMath::Atan2
	 * 4. For the last timestep, carry forward the previous calculated rotation
	 * 5. For stationary entities (no position change), maintain last valid rotation
	 *
	 * @par Unit Handling:
	 * - Input positions are in centimeters (after SI conversion during HDF5 loading)
	 * - Output rotation is in degrees, stored in FSimMovementSample::Rotation
	 *
	 * @note Thread-safe: Respects bShouldStop flag for early termination
	 * @note This provides a reasonable approximation but may not match real-world heading
	 *       for entities that move sideways or backwards
	 *
	 * @see AgentSimulationData.Metadata.bHasRotationData - Flag that triggers this calculation
	 */
	void CalculateRotationFromMovement();

	/**
	 * @brief Calculates speed from position deltas when the HDF5 source data lacks speed information.
	 *
	 * This method is called when AgentSimulationData.Metadata.bHasSpeedData is false, indicating that
	 * the source file did not contain speed data for entities.
	 *
	 * @par Algorithm:
	 * For each entity:
	 * 1. Collect all position samples ordered by timestep
	 * 2. For each timestep (except last), calculate: speed = distance / TimeBetweenSteps
	 *    - Distance is Euclidean distance to next position in centimeters
	 *    - Speed is converted to m/s by dividing by 100
	 * 3. For the last timestep, carry forward the previous calculated speed
	 *
	 * @par Unit Handling:
	 * - Input positions are in centimeters (after SI conversion during HDF5 loading)
	 * - TimeBetweenSteps is in seconds
	 * - Output speed is in meters per second (m/s)
	 *
	 * @note Thread-safe: Respects bShouldStop flag for early termination
	 *
	 * @warning TODO: Some agents appear to be moving but have 0 speed calculated.
	 *          Investigate possible causes:
	 *          - TimeBetweenSteps not set correctly before this function is called
	 *          - Position data not properly converted to cm before storage
	 *          - Entities appearing in only a single timestep (skipped due to Num() < 2 check)
	 *          - Floating point precision issues with very small movements
	 *
	 * @see AgentSimulationData.Metadata.bHasSpeedData - Flag that triggers this calculation
	 */
	void CalculateSpeedFromMovement();

	static int CalculateSrcVectors(TArray<FVector>& Vec3D, const FAgentData& Sample);

	// Allocate space for the animPts, to reduce fragmentation
	void AllocateAnimPts(size_t size) { EmbAvatarAnims.SetNum(size, EAllowShrinking::No); }

	// Create a 3D vector-sum over a duration of timestep records
	static void AddManyVectors(TArray<FVector>& Vec3D, int TStartStep, int TSpanStepPts, FVector& SumVec);

	// Compute the correct step animation parameters for the given speed
	EPedestrianMovementBracket CalculateStepAnimationParams(float CurrentSpeed, float& StepsPerSecond);

	/** Stores the Movement data */
	FSimulationFragment AgentMovementInfoData = FSimulationFragment();

	// "Not walking" threhold taken from step length vs speed equation Fig. 10c 
	// in step extent/contact buffer 2022 paper: https://doi.org/10.1016/j.physa.2022.126927
	const double MinSpeedWalking = 0.046;

	int32 MaxAgents = 0;

	float TimeBetweenSteps = 0.0f;

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
	bool bReadyToDelete = false; // Flag to indicate if the thread is ready to be deleted

	TArray<int32> NumOfAgentsPerTimeStep = TArray<int32>();

	/** The type of simulation file being processed */
	EMobiusAgentFileFormat AgentFileFormat = EMobiusAgentFileFormat::Unknown;

	/** Imported agent simulation data (public for subsystem access) */
	FMobiusAgentSimulationData AgentSimulationData = FMobiusAgentSimulationData();

	/** Phase timings of the last Run() (PRD 02 T6) — see FMobiusImportTimings. */
	FMobiusImportTimings ImportTimings;

protected:
	/** Pointer to a thread */
	FRunnableThread* Thread = nullptr;

	/** File Path to the simulation data */
	FString SimulationDataFilePath = FString();

	/** The Simulation Data File, acceptable formats include JSON (.json) and HDF5 (.h5) */
	FString SimulationDataFile = FString();

	/** Current Data Count */
	int32 CurrentDataCount = 0;

	/** Target Data Count */
	int32 TargetDataCount = 0;

	/** Bool to tell when the thread should stop */
	FThreadSafeBool bShouldStop = false;

private:
	/** Load the agent file through the importer facade */
	bool LoadFileAndDeserialize();

	/** A6: populate everything from a valid v2 .msc cache, skipping the source parse entirely.
	 *  Returns false (with any partial state reset) on miss/stale-hash/corruption — the caller then
	 *  runs the normal full import, which rewrites the cache. */
	bool TryFastReloadFromCache();

	/** Read metadata values from the imported data */
	void ProcessMetadata(bool& bCalculateTimeBetweenSteps, bool& bCalculateMaxTime);

	/** Main simulation processing loop */
	void RunSimulationDataGatheringLoop(bool bCalculateTimeBetweenSteps, bool bCalculateMaxTime);

	/** Send the final progress and completion events */
	void FinalizeProgress();

	/** Owner Subsystem */
	TWeakObjectPtr<UAgentDataSubsystem> OwnerSubsystem;
};
