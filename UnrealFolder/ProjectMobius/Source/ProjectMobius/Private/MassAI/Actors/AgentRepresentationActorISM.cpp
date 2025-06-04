// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/Actors/AgentRepresentationActorISM.h"
#include "Components/InstancedStaticMeshComponent.h"

// Sets default values
AAgentRepresentationActorISM::AAgentRepresentationActorISM()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

    SetupTheMaleAndFemaleISMComps();

}

// Called when the game starts or when spawned
void AAgentRepresentationActorISM::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AAgentRepresentationActorISM::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AAgentRepresentationActorISM::SetupTheMaleAndFemaleISMComps()
{
	// Create Instance Static Mesh Component
	MaleISMComponent = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MaleISMComponent"));

	// attach the ISM Component to the root component
	MaleISMComponent->SetupAttachment(RootComponent);

	// set the ISM Component to be moveable
	MaleISMComponent->SetMobility(EComponentMobility::Movable);

	MaleISMComponent->SetNumCustomDataFloats(6);

	// Female ISM Component
	FemaleISMComponent = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FemaleISMComponent"));

	// attach the ISM Component to the root component
	FemaleISMComponent->SetupAttachment(RootComponent);

	// set the ISM Component to be moveable
	FemaleISMComponent->SetMobility(EComponentMobility::Movable);

	// set the number of custom data floats
	FemaleISMComponent->SetNumCustomDataFloats(6);
}