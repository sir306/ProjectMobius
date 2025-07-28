// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlowCounter.generated.h"

class UBoxComponent;

UCLASS()
class MOBIUSCORE_API AFlowCounter : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AFlowCounter();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

#pragma region METHODS
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

#pragma endregion METHODS 

	
#pragma region PROPERTIES
public:
	/** Box Collision component to track agents in the trigger area and calculate if their vector pass through the gate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Visuals", meta = (AllowPrivateAccess = "true"))
	UBoxComponent* FlowCounterTriggerBox;
	
private:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Visuals", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* FlowCounterPillarMesh1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Visuals", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* FlowCounterPillarMesh2;
	
	// may want a reference to a widget for the flow counter to display the number of agents passing through
	
#pragma endregion PROPERTIES 
};
