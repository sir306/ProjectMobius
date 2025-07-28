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
	

	
#pragma endregion PROPERTIES 
};
