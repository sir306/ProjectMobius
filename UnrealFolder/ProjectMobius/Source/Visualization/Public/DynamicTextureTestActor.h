// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DynamicPixelRenderingTexture.h"
#include "GameFramework/Actor.h"
#include "DynamicTextureTestActor.generated.h"

UCLASS()
class VISUALIZATION_API ADynamicTextureTestActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ADynamicTextureTestActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicTextureTestActor")
	UDynamicPixelRenderingTexture* DynamicPixelRenderingTexture;

	UFUNCTION(BlueprintCallable)
	void TestVoronoiDiagram();
};
