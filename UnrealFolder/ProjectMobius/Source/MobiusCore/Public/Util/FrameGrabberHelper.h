// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FrameGrabber.h"
#include "UObject/Object.h"
#include "FrameGrabberHelper.generated.h"


/**
 *
 */
UCLASS()
class MOBIUSCORE_API UFrameGrabberHelper : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Configure the frame grabber settings. Actual initialization is deferred until the viewport is available.
	 * @param bInUseFullResolution - If true, capture at full viewport resolution; if false, use downscale size
	 * @param InDownscaleSize - Target size when downscaling (used if bInUseFullResolution is false).
	 *                          If invalid (<=0 in either dimension), falls back to default 800x600.
	 */
	void Configure(bool bInUseFullResolution = false, FIntPoint InDownscaleSize = FIntPoint(800, 600));

	/** Set the directory path where captured screenshots will be saved */
	void SetSavePath(const FString& InSavePath);

	/** Request a screenshot capture on next frame */
	void TriggerCapture(const FString& InFileName);

	/** Tick to be called each frame to poll frames internally */
	void Tick(float DeltaTime);

	bool IsCapturing() const { return bIsCapturing; }
#if PLATFORM_MAC
	bool IsInitialized() const { return true; } // Mac uses FScreenshotRequest, always "initialized"
#else
	bool IsInitialized() const { return FrameGrabber.IsValid(); }
#endif

private:
#if !PLATFORM_MAC
	/** Attempt to initialize the frame grabber from the game engine's scene viewport */
	bool TryInitialize();

	TUniquePtr<FFrameGrabber> FrameGrabber;
#endif

	FIntPoint TargetSize;
	FIntPoint ViewportSize;
	FIntPoint ConfiguredDownscaleSize = FIntPoint(800, 600);

	FString PendingFileName;
	FString SavePath;

	bool bConfiguredUseFullResolution = false;
	bool bIsCapturing = false;

#if PLATFORM_MAC
	/** Path where Mac screenshot will be saved (set before callback fires) */
	FString MacPendingScreenshotPath;

	/** Mac screenshot callback - receives pixel data from engine and saves to our custom path */
	void OnMacScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors);
#else
	/** Internally process captured frames (called once after TriggerCapture) */
	void ProcessCapturedFrames(TArray<FCapturedFrameData>& Frames);
#endif
};
