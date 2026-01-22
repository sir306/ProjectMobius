// Fill out your copyright notice in the Description page of Project Settings.


#include "Util/FrameGrabberHelper.h"

#include "Engine/GameViewportClient.h"
#include "HAL/PlatformFileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "RenderingThread.h"  // For FlushRenderingCommands (Mac GPU sync)
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"
#if PLATFORM_MAC
#include "UnrealClient.h"  // For FScreenshotRequest
#endif

void UFrameGrabberHelper::Configure(bool bInUseFullResolution, FIntPoint InDownscaleSize)
{
	bConfiguredUseFullResolution = bInUseFullResolution;

	// Validate and store downscale size
	if (InDownscaleSize.X > 0 && InDownscaleSize.Y > 0)
	{
		ConfiguredDownscaleSize = InDownscaleSize;
	}
	else
	{
		ConfiguredDownscaleSize = FIntPoint(800, 600);
	}

	// Set target size for Mac (which doesn't use TryInitialize)
#if PLATFORM_MAC
	TargetSize = bConfiguredUseFullResolution ? FIntPoint(1920, 1080) : ConfiguredDownscaleSize;
#endif
}

#if !PLATFORM_MAC
bool UFrameGrabberHelper::TryInitialize()
{
	// Already initialized
	if (FrameGrabber.IsValid())
	{
		return true;
	}

	// Must be on the game thread: Slate + viewport access is not thread-safe
	check(IsInGameThread());

	if (!GEngine || !GEngine->GameViewport)
	{
		return false;
	}

	TSharedPtr<SViewport> GameViewportWidget = GEngine->GameViewport->GetGameViewportWidget();
	if (!GameViewportWidget.IsValid())
	{
		return false; // widget not constructed yet
	}

	// The real viewport interface backing the SViewport
	TSharedPtr<ISlateViewport> SlateViewportInterface = GameViewportWidget->GetViewportInterface().Pin();
	if (!SlateViewportInterface.IsValid())
	{
		return false; // not wired up yet
	}

	// This is typically an FSceneViewport for the game viewport
	TSharedPtr<FSceneViewport> SceneViewport = StaticCastSharedPtr<FSceneViewport>(SlateViewportInterface);
	if (!SceneViewport.IsValid())
	{
		return false; // unexpected viewport type
	}

	// Use GetSizeXY() (current backbuffer size)
	ViewportSize = SceneViewport->GetSizeXY();

	// If either dimension is 0, we can't capture yet
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return false;
	}

	TargetSize = bConfiguredUseFullResolution ? ViewportSize : ConfiguredDownscaleSize;

	// Clamp/validate target too (avoid passing nonsense to frame grabber)
	if (TargetSize.X <= 0 || TargetSize.Y <= 0)
	{
		return false;
	}

	FrameGrabber = MakeUnique<FFrameGrabber>(SceneViewport.ToSharedRef(), TargetSize);
	FrameGrabber->StartCapturingFrames();

	UE_LOG(LogTemp, Log, TEXT("FrameGrabberHelper initialized. ViewportSize: %dx%d, TargetSize: %dx%d"),
		ViewportSize.X, ViewportSize.Y, TargetSize.X, TargetSize.Y);

	return true;
}
#endif // !PLATFORM_MAC

void UFrameGrabberHelper::SetSavePath(const FString& InSavePath)
{
	SavePath = InSavePath;
}

void UFrameGrabberHelper::TriggerCapture(const FString& InFileName)
{
	if (bIsCapturing)
	{
		UE_LOG(LogTemp, Warning, TEXT("Already capturing a screenshot on previous frame, ignoring new request."));
		return;
	}

	PendingFileName = InFileName;

#if PLATFORM_MAC
	// Mac: Use FScreenshotRequest which properly handles Metal's async rendering
	// Determine output path
	FString DestPath = SavePath;
	if (DestPath.IsEmpty())
	{
		DestPath = FPaths::ProjectSavedDir() / TEXT("MobiusCaptures/");
	}

	// Ensure directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*DestPath))
	{
		PlatformFile.CreateDirectoryTree(*DestPath);
	}

	FString FullPath = DestPath / (PendingFileName + TEXT(".png"));

	UE_LOG(LogTemp, Log, TEXT("[Mac] Using FScreenshotRequest for capture: %s"), *FullPath);

	// Use RequestScreenshot with filename - this saves directly and handles Metal properly
	FScreenshotRequest::RequestScreenshot(FullPath, false, false); // filename, bInShowUI, bAddFilenameSuffix

	bIsCapturing = true;

	// Reset capturing flag after a short delay since we can't easily detect completion
	// The engine handles the actual save asynchronously
	FTimerHandle TimerHandle;
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetWorld()->GetTimerManager().SetTimer(
			TimerHandle,
			[this]() { bIsCapturing = false; },
			0.5f, // Half second delay
			false
		);
	}
	else
	{
		bIsCapturing = false;
	}
#else
	// Windows/other: Use FFrameGrabber
	// Try lazy initialization if not yet initialized
	if (!FrameGrabber.IsValid())
	{
		if (!TryInitialize())
		{
			UE_LOG(LogTemp, Warning, TEXT("FrameGrabber could not be initialized - viewport not ready."));
			return;
		}
	}

	FrameGrabber->CaptureThisFrame(nullptr);
	bIsCapturing = true;
#endif
}

void UFrameGrabberHelper::Tick(float DeltaTime)
{
#if PLATFORM_MAC
	// Mac uses FScreenshotRequest with callback - nothing to poll in Tick
	// The OnScreenshotCaptured callback handles everything
#else
	// Windows/other: Poll FFrameGrabber
	// Try lazy initialization if not yet initialized
	if (!FrameGrabber.IsValid())
	{
		TryInitialize();
	}

	if (!bIsCapturing || !FrameGrabber.IsValid())
		return;

	TArray<FCapturedFrameData> Frames = FrameGrabber->GetCapturedFrames();

	if (Frames.Num() > 0)
	{
		ProcessCapturedFrames(Frames);
		bIsCapturing = false;
	}
	// else: if no frame ready yet, wait for next tick
#endif
}
#if !PLATFORM_MAC
// Windows/other: Use FFrameGrabber polling
void UFrameGrabberHelper::ProcessCapturedFrames(TArray<FCapturedFrameData>& Frames)
{
	if (Frames.Num() == 0)
		return;

	const FCapturedFrameData& Frame = Frames[0];

	// Determine output path
	FString DestPath = SavePath;
	if (DestPath.IsEmpty())
	{
		DestPath = FPaths::ProjectSavedDir() / TEXT("MobiusCaptures/");
	}

	// Ensure directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*DestPath))
	{
		PlatformFile.CreateDirectoryTree(*DestPath);
	}

	FString FullPath = DestPath / (PendingFileName + TEXT(".png"));

	// Check if we need to resize or can use buffer directly
	const bool bNeedsResize = (Frame.BufferSize != TargetSize);

	if (bNeedsResize)
	{
		// Downscale color buffer to TargetSize
		TArray<FColor> ResizedColors;
		ResizedColors.SetNumUninitialized(TargetSize.X * TargetSize.Y);

		FImageUtils::ImageResize(
			Frame.BufferSize.X,
			Frame.BufferSize.Y,
			Frame.ColorBuffer,
			TargetSize.X,
			TargetSize.Y,
			ResizedColors,
			false); // Not linear space

		Async(EAsyncExecution::ThreadPool, [ResizedColors = MoveTemp(ResizedColors), FullPath, TargetSize = this->TargetSize]()
		{
			TArray64<uint8> PNGData;
			FImageUtils::PNGCompressImageArray(TargetSize.X, TargetSize.Y, ResizedColors, PNGData);
			FFileHelper::SaveArrayToFile(PNGData, *FullPath);
			UE_LOG(LogTemp, Log, TEXT("Async screenshot saved: %s"), *FullPath);
		});
	}
	else
	{
		// Use buffer directly at full resolution
		Async(EAsyncExecution::ThreadPool, [ColorBuffer = Frame.ColorBuffer, FullPath, BufferSize = Frame.BufferSize]()
		{
			TArray64<uint8> PNGData;
			FImageUtils::PNGCompressImageArray(BufferSize.X, BufferSize.Y, ColorBuffer, PNGData);
			FFileHelper::SaveArrayToFile(PNGData, *FullPath);
			UE_LOG(LogTemp, Log, TEXT("Async screenshot saved: %s"), *FullPath);
		});
	}
}
#endif // PLATFORM_MAC
