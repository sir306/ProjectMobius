// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 Nicholas R. Harding
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