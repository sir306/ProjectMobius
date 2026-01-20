// Fill out your copyright notice in the Description page of Project Settings.


#include "Util/FrameGrabberHelper.h"

#include "Engine/GameViewportClient.h"
#include "HAL/PlatformFileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Slate/SceneViewport.h"

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
}

bool UFrameGrabberHelper::TryInitialize()
{
	if (FrameGrabber.IsValid())
	{
		return true; // Already initialized
	}

	// Use GameViewport which works in both standalone and PIE modes
	if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->GetGameViewportWidget())
	{
		return false; // Viewport not ready yet
	}
	
	// check gameviewport size not zero if it is not ready yet
	if (GEngine->GameViewport->Viewport->GetSizeXY().X == 0 || GEngine->GameViewport->Viewport->GetSizeXY().Y == 0)
	{
		return false; // Viewport size not ready yet
	}

	TSharedPtr<FSceneViewport> SceneViewport = MakeShareable(new FSceneViewport(GEngine->GameViewport, GEngine->GameViewport->GetGameViewportWidget()));
	if (!SceneViewport.IsValid())
	{
		return false; // Scene viewport not ready yet
	}

	// TODO: FIX REQUIRED ViewportSize is always 0 and not correct, currently crashes when making FFrameGrabber with SceneViewport.ToSharedRef() as size is 0 and must be bigger
	ViewportSize = SceneViewport->GetSize();
	
	if (ViewportSize.X <= 0 && ViewportSize.Y <= 0)
	{
		return false;
	}

	if (bConfiguredUseFullResolution)
	{
		TargetSize = ViewportSize;
	}
	else
	{
		TargetSize = ConfiguredDownscaleSize;
	}

	FrameGrabber = MakeUnique<FFrameGrabber>(SceneViewport.ToSharedRef(), ViewportSize);
	FrameGrabber->StartCapturingFrames();

	UE_LOG(LogTemp, Log, TEXT("FrameGrabberHelper initialized. ViewportSize: %dx%d, TargetSize: %dx%d"),
		ViewportSize.X, ViewportSize.Y, TargetSize.X, TargetSize.Y);

	return true;
}

void UFrameGrabberHelper::SetSavePath(const FString& InSavePath)
{
	SavePath = InSavePath;
}

void UFrameGrabberHelper::TriggerCapture(const FString& InFileName)
{
	// Try lazy initialization if not yet initialized
	if (!FrameGrabber.IsValid())
	{
		if (!TryInitialize())
		{
			UE_LOG(LogTemp, Warning, TEXT("FrameGrabber could not be initialized - viewport not ready."));
			return;
		}
	}

	if (bIsCapturing)
	{
		UE_LOG(LogTemp, Warning, TEXT("Already capturing a screenshot on previous frame, ignoring new request."));
		return;
	}

	PendingFileName = InFileName;
	FrameGrabber->CaptureThisFrame(nullptr);
	bIsCapturing = true;
}

void UFrameGrabberHelper::Tick(float DeltaTime)
{
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
}

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
