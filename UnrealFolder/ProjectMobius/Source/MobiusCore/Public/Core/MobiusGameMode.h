// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MobiusGameMode.generated.h"

class UUserProjectSettings;
/**
 * 
 */
UCLASS()
class MOBIUSCORE_API AMobiusGameMode : public AGameModeBase
{
	GENERATED_BODY()
	
	explicit AMobiusGameMode(const FObjectInitializer& ObjectInitializer);

public:
	
	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void BeginPlay() override;
	
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "User Project Settings")
	UUserProjectSettings* ProjectUserSettings = nullptr;
};
