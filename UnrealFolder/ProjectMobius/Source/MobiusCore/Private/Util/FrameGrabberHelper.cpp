// Fill out your copyright notice in the Description page of Project Settings.


#include "Util/FrameGrabberHelper.h"

#include "Engine/GameViewportClient.h"
#include "HAL/PlatformFileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"

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
//TODO: fix screenshot capture for Mac DEVICES (Works fine on windows)
void UFrameGrabberHelper::ProcessCapturedFrames(TArray<FCapturedFrameData>& Frames)
{
	if (Frames.Num() == 0)
		return;

	const FCapturedFrameData& Frame = Frames[0];

	// === MAC DIAGNOSTIC LOGGING START ===
#if PLATFORM_MAC
	{
		const int32 TotalPixels = Frame.ColorBuffer.Num();
		UE_LOG(LogTemp, Warning, TEXT("[Mac Debug] ColorBuffer size: %d pixels, BufferSize: %dx%d, TargetSize: %dx%d"),
			TotalPixels, Frame.BufferSize.X, Frame.BufferSize.Y, TargetSize.X, TargetSize.Y);

		if (TotalPixels > 0)
		{
			// Check first pixel
			const FColor& First = Frame.ColorBuffer[0];
			UE_LOG(LogTemp, Warning, TEXT("[Mac Debug] First pixel: R=%d G=%d B=%d A=%d"),
				First.R, First.G, First.B, First.A);

			// Check center pixel
			const int32 CenterIdx = TotalPixels / 2;
			const FColor& Center = Frame.ColorBuffer[CenterIdx];
			UE_LOG(LogTemp, Warning, TEXT("[Mac Debug] Center pixel[%d]: R=%d G=%d B=%d A=%d"),
				CenterIdx, Center.R, Center.G, Center.B, Center.A);

			// Count non-zero pixels (sample every 1000th pixel for speed)
			int32 NonZeroCount = 0;
			int32 SampleCount = 0;
			float AvgR = 0, AvgG = 0, AvgB = 0, AvgA = 0;
			for (int32 i = 0; i < TotalPixels; i += 1000)
			{
				const FColor& P = Frame.ColorBuffer[i];
				if (P.R != 0 || P.G != 0 || P.B != 0 || P.A != 0)
				{
					NonZeroCount++;
				}
				AvgR += P.R; AvgG += P.G; AvgB += P.B; AvgA += P.A;
				SampleCount++;
			}
			if (SampleCount > 0)
			{
				AvgR /= SampleCount; AvgG /= SampleCount; AvgB /= SampleCount; AvgA /= SampleCount;
			}
			UE_LOG(LogTemp, Warning, TEXT("[Mac Debug] Sampled %d pixels: %d non-zero, Avg RGBA=(%.1f, %.1f, %.1f, %.1f)"),
				SampleCount, NonZeroCount, AvgR, AvgG, AvgB, AvgA);

			// Diagnosis hint
			if (NonZeroCount == 0)
			{
				UE_LOG(LogTemp, Error, TEXT("[Mac Debug] DIAGNOSIS: All sampled pixels are zero - likely SYNC ISSUE (frame not rendered yet)"));
			}
			else if (AvgA < 10)
			{
				UE_LOG(LogTemp, Error, TEXT("[Mac Debug] DIAGNOSIS: Very low alpha - possible ALPHA CHANNEL ISSUE"));
			}
			else if (AvgR < 5 && AvgG < 5 && AvgB < 5 && AvgA > 200)
			{
				UE_LOG(LogTemp, Error, TEXT("[Mac Debug] DIAGNOSIS: Very dark with good alpha - possible LINEAR/GAMMA color space issue"));
			}
			else if (FMath::Abs(AvgR - AvgB) > 50)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Mac Debug] HINT: Large R/B difference - check if RGBA vs BGRA swap needed"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[Mac Debug] DIAGNOSIS: ColorBuffer is EMPTY"));
		}
	}
#endif
	// === MAC DIAGNOSTIC LOGGING END ===

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
