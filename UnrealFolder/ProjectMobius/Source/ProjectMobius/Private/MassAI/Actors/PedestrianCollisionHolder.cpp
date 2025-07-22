// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/Actors/PedestrianCollisionHolder.h"


// Sets default values
APedestrianCollisionHolder::APedestrianCollisionHolder()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false; // our collision holder doesn't need to tick every frame, it is just a holder for collision components
}

// Called when the game starts or when spawned
void APedestrianCollisionHolder::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void APedestrianCollisionHolder::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

