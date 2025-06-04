// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VRScreenCaptureComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class HIT_THESISWORK_API UVRScreenCaptureComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UVRScreenCaptureComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	// tick component
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:	
	void InitializeCaptureComponent();

	UFUNCTION(BlueprintCallable, Category = "VR Screen Capture")
	void TakeScreenshot(const FString& BaseFileName);
	
	void CaptureAndSave(const FString& BaseFileName, const FString& FolderPath);

	static void SaveScreenshot(const TArray<FColor>& Bitmap, const FString& FilePath, int32 Width, int32 Height);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR Screen Capture")
	USceneCaptureComponent2D* VRCapture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR Screen Capture")
	UTextureRenderTarget2D* VRRenderTarget;
};
