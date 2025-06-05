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
