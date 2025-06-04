// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h" // So we can use the FMassFragment
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
#include "SimulationFragment.generated.h"


/**
 * Vector Velocity 2D sample, used to store angular and linear velocity for agents
 * Created by: Peter Thompson
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FVelocityVector2D
{
	GENERATED_BODY()
public:
	// Constructor from angle and speed
	FVelocityVector2D(double a = 0.0, double s = 0.0) : Angle(a), Speed(s) {}

	// Constructor from a Vector3D and duration time
	FVelocityVector2D(const FVector& Vec, double TravelTime) {
		Angle = FMath::Atan2(Vec.Y, Vec.X);
		Speed = Vec.Length() / TravelTime;
	}
     
	// get/set methods for angle and speed
	double GetAngle() const { return Angle; }
	double GetSpeed() const { return Speed; }
	void SetAngle(double a) { Angle = a; }
	void SetSpeed(double s) { Speed = s; }

	// Static method to infer step duration for normalised speed
	double InferStepDuration(void);

	// Static method to infer stride duration for normalised speed equalt to double step duration
	const double InferStrideDuration(void);

private:
	double Angle;
	double Speed;
};

/**
 *
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FSimMovementSample
{
	GENERATED_BODY()
public:
	FSimMovementSample();
	FSimMovementSample(int32 InEntityID, FVector InPosition, FRotator InRotation, float InSpeed, FString InMode);
	FSimMovementSample(int32 InEntityID, FVector InPosition, FRotator InRotation, float InSpeed, FString InMode, EPedestrianMovementBracket InMovementBracket, unsigned long InStepDurationMS, FVelocityVector2D InStepVector);
	~FSimMovementSample();

#pragma region PUBLIC_PROPERTIES
	/** Entity ID */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	int32 EntityID;

	/** Position for Entity */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	FVector Position;

	/** Mode for the sample */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	FString Mode;

	/** Rotation for Entity */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	FRotator Rotation;

	/** Speed for Entity */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	float Speed;

	/** predefined movement bracket (for animation) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovementSample")
	EPedestrianMovementBracket MovementBracket;
	
	/** step duration in milliseconds */
	unsigned long StepDurationMS;

	/** Angular vectors, smoothed across estimated steps/strides */
	FVelocityVector2D StepVector;

#pragma endregion PUBLIC_PROPERTIES
};

/**
 * 
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FSimulationFragment : public FMassSharedFragment
{
	GENERATED_BODY()
public:
	// Default Constructor
	FSimulationFragment();

	// Overloaded Constructor
	FSimulationFragment(const FString& InJsonDataPath);
	
	~FSimulationFragment();

#pragma region PUBLIC_METHODS
	/**
	* A method to add movement samples to the simulation data
	* @param TimeStep - The time value for the movement sample key
	* @param MovementSample - The movement sample to add to the simulation data
	*/
	void AddMovementSample(int32 TimeStep, TArray<FSimMovementSample> MovementSample);

	/**
	* Build the Movement Sample for the specified TimeVal
	* @param TimeVal - The time value for the movement sample key
	*/
	TArray<FSimMovementSample> BuildMovementSample(float TimeVal);

	/**
	* Get the Simulation Data from JSON file
	* TODO: Implement parameter for JSONData
	* @param JSONData - The JSON data to get the simulation data from
	*/
	void GetSimulationData(FString JSONData);

	/*
	 * Asynchronously get the simulation data from JSON file
	 * 
	 * @param JSONData - The JSON data to get the simulation data from
	 * TODO: @param Callback - The callback to call when the data is loaded
	 */
	void AsyncGetSimData(FString JSONData);

#pragma endregion PUBLIC_METHODS

#pragma region PUBLIC_PROPERTIES
	// TODO: Add buffer method and not store all data in this struct
	// TODO: This Map logic needs improving as it is not efficient with large data sets and looping over all data is poor
	/** TMap for data the key is time and value is struct array of FSimMovementSample */
	TMap<int32, TArray<FSimMovementSample>> SimulationData;

	float MaxTime;

	///** Mapped Simulation Data */
	//TArray<TArray<TMap<int32, FSimMovementSample>>> MappedSimulationData;

#pragma endregion PUBLIC_PROPERTIES
};
