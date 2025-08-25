// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EnumsAndStructs/FlowCounterStructs.h"
#include "GameFramework/Actor.h"
#include "FlowCounter.generated.h"

class UDeformableQuadComponent;
class UStatisticSubsystem;
class UBoxComponent;

UCLASS()
class MOBIUSCORE_API AFlowCounter : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AFlowCounter();

	virtual ~AFlowCounter() override;
	
	virtual void PostInitializeComponents() override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

#pragma region METHODS
	/** */
	UFUNCTION(BlueprintCallable, Category = "FlowCounter|Methods")
	void MoveGatePillarMeshToLocation(int32 PillarIndex, const FVector& NewLocation);
	
	/**
	 * To Resize the trigger box for the flow counter, we need to get the distance between the two pillar meshes
	 * @param[float] OutDistanceBetweenPillars The distance between the two pillar meshes
	 * @param[FVector] OutCenterLocation The center location of two pillar meshes
	 */
	UFUNCTION(BlueprintCallable, Category = "FlowCounter|Methods")
	void ResizeFlowCounterTriggerBox(float& OutDistanceBetweenPillars, FVector& OutCenterLocation) const;

	/**
	 * Resize extent of trigger box
	 * @param[const FVector&] NewExtent The new extent of the trigger box
	 */
	UFUNCTION(BlueprintCallable, Category = "FlowCounter|Methods")
	void ResizeFlowCounterTriggerBoxExtent(const FVector& NewExtent);
	
	/**
	 * Update location of trigger box
	 * @param[const FVector&] NewLocation The new location of the trigger box
	 */
	UFUNCTION(BlueprintCallable, Category = "FlowCounter|Methods")
	void UpdateFlowCounterTriggerBoxLocation(const FVector& NewLocation);
	
	/**
	 * Update trigger box a method to call when pillar placement changes/or when user confirms placement
	 * or when the flow counter is placed in the world for the first time
	 */
	UFUNCTION(BlueprintCallable, Category = "FlowCounter|Methods")
	void UpdateFlowCounterTriggerBox();
	bool ProcessAgentFlowCrossing(const FFlowCounterData& Data);
	bool HasAgentAlreadyPassedThrough(int32 AgentID) const;

	/** */
	void NewAgentData(TArray<FFlowCounterData>& NewAgentData);

	/**
	 * Notifies the statistic subsystem that the flow counter exists in the world or has been updated.
	 * This function communicates with the UStatisticActorManagementSubsystem to register or update
	 * this flow counter, ensuring the subsystem is aware of the current state of the flow counter.
	 */
	UFUNCTION(BlueprintCallable, Category = "FlowCounter|Methods")
	void AddFlowCounterToSubsystem();
	void RemoveFlowCounterToSubsystem();

	UFUNCTION(BlueprintCallable, Category = "FlowCounter|Methods")
	void ResetFlowCounterTrackingData();

	UFUNCTION(BlueprintCallable, Category = "FlowCounter|Methods")
	void NewSimTime(float UpdatedTime);

	// Call these from code or Blueprints to move vertices
	UFUNCTION(BlueprintCallable)
	void SetCorners(const FVector& A, const FVector& B, const FVector& C, const FVector& D);

	UFUNCTION(BlueprintCallable)
	void SetSize(float Width, float Height);   // convenience wrapper

	void FlashBarrierColor();

	/**
	 * Assigns agents to appropriate buckets based on their intersection locations.
	 *
	 * @param[TArray<int32>] AllAgents A list of agent IDs to be processed and assigned to buckets.
	 *
	 * The method retrieves each agent's intersection location from the internal map and determines
	 * which bucket segment the agent belongs to. If a matching bucket segment is found, the agent
	 * is added to the bucket, and the bucket's agent count is updated. Error handling is in place
	 * to skip agents with missing data.
	 */
	void AssignAgentsToBuckets(TArray<int32> AllAgents);

	void AssignAgentToBuckets(int32 AgentID);

	void AssignAgentToBucketUsingThreshold(int32 AgentID, float IntersectionThreshold);

	UFUNCTION(BlueprintCallable, Category = "FlowCounter|Methods")
	void UpdateNumberOfBucketSegments(int32 NewNumberOfSegments);

	/** */
	void RemoveAgentFromBuckets(int32 AgentID);

	void UpdateFlowBucketsWithCurrentAgentsFromTimeChange();

private:
	void SetupBucketSegments();
	
	int32 BucketIndex_LeftClosed(float T, int32 N, float Eps = 1e-6f);
	
#pragma endregion METHODS 

	
#pragma region PROPERTIES
public:
	/** Box Collision component to track agents in the trigger area and calculate if their vector pass through the gate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties")
	UBoxComponent* FlowCounterTriggerBox;

	/** */
	FFlowCounterZSearchLimits FlowCounterZSearchLimits = FFlowCounterZSearchLimits(0.0f, 100.0f);

	/** */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties")
	FVector FlowCounterLineStartLocation = FVector(0.0f, 0.0f, 0.0f);

	/** */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties")
	FVector FlowCounterLineEndLocation = FVector(0.0f, 0.0f, 0.0f);

protected:
	/** */
	TAtomic<int32> FlowCounterCount = 0;// using TAtomic to ensure thread safety when incrementing the count

	/** */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Stats")
	float FlowRateOverTime = 0.0f; // e.g., agents per minute

	/** */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Stats")
	TArray<FFlowCounterBucketData> FlowCounterBucketData = TArray<FFlowCounterBucketData>();
	
	/**
	 * Stores the previous tracked locations of agents, where each agent is identified by an integer ID
	 * and their corresponding location is stored as an FVector.
	 * @key[int32] The unique ID of an agent.
	 * @value[FVector] The last known location of the corresponding agent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties")
	TMap<int32, FVector> PreviousTrackedAgentLocations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties")
	TMap<int32, FFlowCounterCountedAgentData> AgentsPassedThroughCounter;

	/**
	 * Represents the number of segments or partitions in the bucket system of the flow counter.
	 * Used for dividing the counter's area into distinct sections to calculate the number of agents passing
	 * through each segment.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties")
	int32 NumberOfBucketSegments = 3;//TODO: expose to UI to allow user to set number of segments and reset to default value of 1
	
	/** For a bucket system that tells the amount of agents that passed through a
	 * section of the counter we need to know what a bucket width should be */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties")
	float PassageFlowIncrement = 50.0f;

	/**
	 * A reference to the statistic subsystem that facilitates communication and integration
	 * with the broader system managing statistical data within the game or application.
	 * This subsystem is used to track, update, and register specific statistical elements
	 * relevant to this class, such as flow counter data or related metrics.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties")
	TObjectPtr<UStatisticSubsystem> StatisticSubsystem;
	
private:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Visuals", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* FlowCounterPillarMesh1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Visuals", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* FlowCounterPillarMesh2;

	UPROPERTY(VisibleAnywhere)
	UDeformableQuadComponent* CounterBarrierVisualMesh;

	UPROPERTY(Transient)
	UMaterialInstanceDynamic* CounterBarrierVisualMID;

	UPROPERTY(Transient)
	FTimerHandle FlowColorResetHandle;   // store so we can clear/restart
	
	const FName FlowColorParam = TEXT("FlowBarrierColour");

	UPROPERTY(Transient)
	float CurrentSimTime = 0.0f; // Used to track the current simulation time for the flow counter

	mutable FCriticalSection FlowStateCS;
	
	// may want a reference to a widget for the flow counter to display the number of agents passing through
	
#pragma endregion PROPERTIES

public:
	// GETTERS AND SETTERS

	/** Get the current flow counter count */
	UFUNCTION(BlueprintCallable, Category = "FlowCounter|Getters")
	FORCEINLINE int32 GetFlowCounterCount() const { return FlowCounterCount.Load(); }
};
