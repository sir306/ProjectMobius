// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AgentRepresentationActorISM.generated.h"

UCLASS()
class PROJECTMOBIUS_API AAgentRepresentationActorISM : public AActor
{
	GENERATED_BODY()
	
public:
	// Sets default values for this actor's properties
	AAgentRepresentationActorISM();

	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Function to setup the ISM Components
	void SetupTheMaleAndFemaleISMComps();

#pragma region Components
	// Male ISM components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UInstancedStaticMeshComponent> MaleISMComponent;

	// Female ISM components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UInstancedStaticMeshComponent> FemaleISMComponent;


#pragma endregion Components
protected:

public:	
#pragma region GETTERS_SETTERS
	/** Getter for the Male ISMComponent */
	FORCEINLINE UInstancedStaticMeshComponent* GetMaleISMComponent() const { return MaleISMComponent; }

	/** Getter for the Female ISMComponent */
	FORCEINLINE UInstancedStaticMeshComponent* GetFemaleISMComponent() const { return FemaleISMComponent; }

	/** Getter for both ISMComponents */
	FORCEINLINE TArray<UInstancedStaticMeshComponent*> GetISMComponents() const { return { MaleISMComponent, FemaleISMComponent }; }
	
#pragma endregion GETTERS_SETTERS
};
