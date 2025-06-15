// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
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
#include "GameFramework/PlayerController.h"
#include "MobiusController.generated.h"


/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API AMobiusController : public APlayerController
{
	GENERATED_BODY()

	AMobiusController();

public:
	/**
	 * Override the BeginPlay method to set the default behavior of the controller
	 */
	virtual void BeginPlay() override;

	/** Override EndPlay to clean up bound delegates */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	/** Get the required subsystems to create a screenshot */
	void GetScreenshotRequiredSubsystemsAndData();

	/** Pedestrian Vector file has changed so change filepath to match */
	UFUNCTION(BlueprintCallable, Category = "MobiusController|Methods")
	void UpdatePedestrianVectorFilePath(FString NewFilePath);

	/** Update PedestrianVectorFileName to the same as the currently loaded file when changed */
	UFUNCTION(BlueprintCallable, Category = "MobiusController|Methods")
	void UpdatePedestrianVectorFileName(FString NewFileName);
	
	/***/
	UFUNCTION(BlueprintCallable, Category = "MobiusController|Methods")
	void TakeScreenshot();
	void TakeScreenshot(const FString& BaseFileName);

	/** Method that is bound to builtin screenshot method, using the builtin method we remove the need for render targets */
	UFUNCTION()
	void OnScreenShotCaptured(int Width, int Height, const TArray<FColor>& Bitmap) const;
	
	UFUNCTION(BlueprintCallable, Category = "MobiusController|Methods")
	void SaveScreenshot(const TArray<FColor>& Bitmap, const FString& FilePath, int32 Width, int32 Height);

	/**
	 * Load stored camera save points from the mobius capture folder if it exists for the current pedestrian vector file
	 */
	UFUNCTION(BlueprintCallable, Category = "MobiusController|Methods|CameraSave")
	void LoadCameraSavePoints();

	/**
	 * Save new camera save point into the relevant mobius capture folder camera save point file, if no
	 * file exists then create a new one
	 *
	 * @param[FTransform] CameraTransform - The transform of the camera to save
	 */
	UFUNCTION(BlueprintCallable, Category = "MobiusController|Methods|CameraSave")
	void SaveCameraSavePoint(const FTransform& CameraTransform);

	/**
	 * Interpolates the camera position and rotation to the given transform over a specified duration.
	 *
	 * @param[FTransform] TargetTransform - The target transform to interpolate to.
	 * @param[float] Duration - The duration over which to interpolate.
	 */
	UFUNCTION(BlueprintCallable, Category = "MobiusController|Methods|CameraSave")
	void InterpolateCameraToTransform(const FTransform& TargetTransform, float Duration);
	
	/** TEMP method to cycle through loaded camera save points */
	UFUNCTION(BlueprintCallable, Category = "MobiusController|Methods|CameraSave")
	void CycleCameraSavePoints();

#pragma region PROPERTIES
	/** Ptr to the Time dialation subsystem to get the current simulation time */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MobiusController|Properties")
	TObjectPtr<class UTimeDilationSubSystem> TimeDilationSubsystem;
	
	/** File Path relative to pedestrian vectors - defaults to project directory if unable to find */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MobiusController|Properties")
	FString ScreenshotFilePath = FPaths::ProjectDir();

	FString PedestrianVectorFileName = TEXT("TestCapture");

	/** The file name that is given for a screenshot */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MobiusController|Properties")
	FString ScreenShotFileName = TEXT("TestCapture");

	/** filename for camera captures in their relevant directory */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MobiusController|Properties|CameraSave")
	FString CameraSavePointsFileName = TEXT("MobiusCamSavePoints.json");

	/** Stores pre-existing saves from camera save file into an array that can be used for moving the user */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MobiusController|Properties|CameraSave")
	TArray<FTransform> LoadedCameraTransforms;
#pragma endregion PROPERTIES
};
