// Fill out your copyright notice in the Description page of Project Settings.


#include "Util/FrameGrabberHelper.h"

#include "Engine/GameViewportClient.h"
#include "HAL/PlatformFileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "RenderingThread.h"  // For FlushRenderingCommands (Mac GPU sync)
#include "Slate/SceneViewport.h"
#include "Subsystems/MobiusCustomLoggerSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Widgets/SViewport.h"
#if PLATFORM_MAC
#include "UnrealClient.h"  // For FScreenshotRequest
#endif

namespace
{
	// Helper to safely log via MobiusCustomLoggerSubsystem (works from any thread)
	void MobiusLog(const FString& Message)
	{
		if (IsInGameThread())
		{
			if (UMobiusCustomLoggerSubsystem* Logger = GEngine ? UMobiusCustomLoggerSubsystem::Get(GEngine->GetWorld()) : nullptr)
			{
				Logger->EnqueueLogMessage(Message);
			}
		}
		else
		{
			// From background thread, dispatch to game thread
			AsyncTask(ENamedThreads::GameThread, [Message]()
			{
				if (UMobiusCustomLoggerSubsystem* Logger = GEngine ? UMobiusCustomLoggerSubsystem::Get(GEngine->GetWorld()) : nullptr)
				{
					Logger->EnqueueLogMessage(Message);
				}
			});
		}
	}

	// Helper to report errors via MobiusUserFeedbackSubsystem (works from any thread)
	void MobiusReportError(const FText& Title, const FText& Message, const FText& Location,
		EMobiusErrorSeverity Severity = EMobiusErrorSeverity::Error, bool bShowPrompt = true)
	{
		if (IsInGameThread())
		{
			if (UMobiusUserFeedbackSubsystem* Feedback = GEngine ? UMobiusUserFeedbackSubsystem::Get(GEngine->GetWorld()) : nullptr)
			{
				Feedback->ReportError(
					FText::FromString(TEXT("Screenshot Error")),
					Title,
					Message,
					Location,
					Severity,
					bShowPrompt);
			}
		}
		else
		{
			// From background thread, dispatch to game thread
			AsyncTask(ENamedThreads::GameThread, [Title, Message, Location, Severity, bShowPrompt]()
			{
				if (UMobiusUserFeedbackSubsystem* Feedback = GEngine ? UMobiusUserFeedbackSubsystem::Get(GEngine->GetWorld()) : nullptr)
				{
					Feedback->ReportError(
						FText::FromString(TEXT("Screenshot Error")),
						Title,
						Message,
						Location,
						Severity,
						bShowPrompt);
				}
			});
		}
	}
}

void UFrameGrabberHelper::Configure(bool bInUseFullResolution, FIntPoint InDownscaleSize)
{
	bConfiguredUseFullResolution = bInUseFullResolution;

	// Validate and store downscale size
	if (InDownscaleSize.X > 0 && InDownscaleSize.Y > 0)
	{
		ConfiguredDownscaleSize = InDownscaleSize;
		MobiusLog(FString::Printf(TEXT("[FrameGrabber] Configured with custom size: %dx%d, FullRes: %s"),
			InDownscaleSize.X, InDownscaleSize.Y, bInUseFullResolution ? TEXT("true") : TEXT("false")));
	}
	else
	{
		ConfiguredDownscaleSize = FIntPoint(800, 600);
		MobiusLog(FString::Printf(TEXT("[FrameGrabber] Configured with default size: 800x600 (invalid size provided: %dx%d)"),
			InDownscaleSize.X, InDownscaleSize.Y));
	}

	// Set target size for Mac (which doesn't use TryInitialize)
#if PLATFORM_MAC
	TargetSize = bConfiguredUseFullResolution ? FIntPoint(1920, 1080) : ConfiguredDownscaleSize;
	MobiusLog(FString::Printf(TEXT("[FrameGrabber][Mac] Target size set to: %dx%d"), TargetSize.X, TargetSize.Y));
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

	if (!GEngine)
	{
		MobiusLog(TEXT("[FrameGrabber] TryInitialize failed: GEngine is null"));
		return false;
	}

	if (!GEngine->GameViewport)
	{
		MobiusLog(TEXT("[FrameGrabber] TryInitialize failed: GameViewport is null"));
		return false;
	}

	TSharedPtr<SViewport> GameViewportWidget = GEngine->GameViewport->GetGameViewportWidget();
	if (!GameViewportWidget.IsValid())
	{
		MobiusLog(TEXT("[FrameGrabber] TryInitialize failed: GameViewportWidget not constructed yet"));
		return false;
	}

	// The real viewport interface backing the SViewport
	TSharedPtr<ISlateViewport> SlateViewportInterface = GameViewportWidget->GetViewportInterface().Pin();
	if (!SlateViewportInterface.IsValid())
	{
		MobiusLog(TEXT("[FrameGrabber] TryInitialize failed: SlateViewportInterface not wired up yet"));
		return false;
	}

	// This is typically an FSceneViewport for the game viewport
	TSharedPtr<FSceneViewport> SceneViewport = StaticCastSharedPtr<FSceneViewport>(SlateViewportInterface);
	if (!SceneViewport.IsValid())
	{
		MobiusLog(TEXT("[FrameGrabber] TryInitialize failed: Unexpected viewport type (not FSceneViewport)"));
		MobiusReportError(
			FText::FromString(TEXT("Viewport Error")),
			FText::FromString(TEXT("Could not access scene viewport for screenshot capture.")),
			FText::FromString(TEXT("FrameGrabberHelper::TryInitialize")),
			EMobiusErrorSeverity::Warning,
			false); // Don't show popup for this, it may resolve on retry
		return false;
	}

	// Use GetSizeXY() (current backbuffer size)
	ViewportSize = SceneViewport->GetSizeXY();

	// If either dimension is 0, we can't capture yet
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		MobiusLog(FString::Printf(TEXT("[FrameGrabber] TryInitialize failed: Invalid viewport size %dx%d"),
			ViewportSize.X, ViewportSize.Y));
		return false;
	}

	TargetSize = bConfiguredUseFullResolution ? ViewportSize : ConfiguredDownscaleSize;

	// Clamp/validate target too (avoid passing nonsense to frame grabber)
	if (TargetSize.X <= 0 || TargetSize.Y <= 0)
	{
		MobiusLog(FString::Printf(TEXT("[FrameGrabber] TryInitialize failed: Invalid target size %dx%d"),
			TargetSize.X, TargetSize.Y));
		MobiusReportError(
			FText::FromString(TEXT("Configuration Error")),
			FText::FromString(FString::Printf(TEXT("Invalid screenshot target size: %dx%d"), TargetSize.X, TargetSize.Y)),
			FText::FromString(TEXT("FrameGrabberHelper::TryInitialize")),
			EMobiusErrorSeverity::Error,
			true);
		return false;
	}

	FrameGrabber = MakeUnique<FFrameGrabber>(SceneViewport.ToSharedRef(), TargetSize);
	FrameGrabber->StartCapturingFrames();

	MobiusLog(FString::Printf(TEXT("[FrameGrabber] Initialized successfully. ViewportSize: %dx%d, TargetSize: %dx%d"),
		ViewportSize.X, ViewportSize.Y, TargetSize.X, TargetSize.Y));

	return true;
}
#endif // !PLATFORM_MAC

void UFrameGrabberHelper::SetSavePath(const FString& InSavePath)
{
	SavePath = InSavePath;
	MobiusLog(FString::Printf(TEXT("[FrameGrabber] Save path set to: %s"), *SavePath));
}

void UFrameGrabberHelper::TriggerCapture(const FString& InFileName)
{
	MobiusLog(FString::Printf(TEXT("[FrameGrabber] TriggerCapture called with filename: %s"), *InFileName));

	if (bIsCapturing)
	{
		MobiusLog(TEXT("[FrameGrabber] Warning: Already capturing a screenshot on previous frame, ignoring new request."));
		MobiusReportError(
			FText::FromString(TEXT("Capture In Progress")),
			FText::FromString(TEXT("A screenshot capture is already in progress. Please wait for it to complete.")),
			FText::FromString(TEXT("FrameGrabberHelper::TriggerCapture")),
			EMobiusErrorSeverity::Warning,
			false); // Don't show popup, just log
		return;
	}

	if (InFileName.IsEmpty())
	{
		MobiusLog(TEXT("[FrameGrabber] Error: Empty filename provided for screenshot capture."));
		MobiusReportError(
			FText::FromString(TEXT("Invalid Filename")),
			FText::FromString(TEXT("Screenshot capture requires a valid filename.")),
			FText::FromString(TEXT("FrameGrabberHelper::TriggerCapture")),
			EMobiusErrorSeverity::Error,
			true);
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
		MobiusLog(FString::Printf(TEXT("[FrameGrabber][Mac] Using default save path: %s"), *DestPath));
	}

	// Ensure directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*DestPath))
	{
		const bool bCreated = PlatformFile.CreateDirectoryTree(*DestPath);
		if (!bCreated)
		{
			MobiusLog(FString::Printf(TEXT("[FrameGrabber][Mac] Error: Failed to create directory: %s"), *DestPath));
			MobiusReportError(
				FText::FromString(TEXT("Directory Error")),
				FText::FromString(FString::Printf(TEXT("Failed to create screenshot directory: %s"), *DestPath)),
				FText::FromString(TEXT("FrameGrabberHelper::TriggerCapture")),
				EMobiusErrorSeverity::Error,
				true);
			return;
		}
		MobiusLog(FString::Printf(TEXT("[FrameGrabber][Mac] Created directory: %s"), *DestPath));
	}

	// Full path for the screenshot (without .png - engine may add it)
	FString FullPath = DestPath / PendingFileName;

	MobiusLog(FString::Printf(TEXT("[FrameGrabber][Mac] Requesting screenshot via FScreenshotRequest: %s"), *FullPath));

	// Request screenshot with filename, no HDR, show UI notification, no unique suffix
	// Signature: RequestScreenshot(const FString& Filename, bool bShowUI, bool bAddFilenameSuffix, bool bInHDR)
	FScreenshotRequest::RequestScreenshot(FullPath, true, false, false);

	bIsCapturing = true;

	// Reset capturing flag after a delay since the engine saves asynchronously
	FTimerHandle TimerHandle;
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWorld())
	{
		GEngine->GameViewport->GetWorld()->GetTimerManager().SetTimer(
			TimerHandle,
			[this]() {
				bIsCapturing = false;
				MobiusLog(TEXT("[FrameGrabber][Mac] Capture flag reset after timeout."));
			},
			0.5f,
			false
		);
	}
	else
	{
		MobiusLog(TEXT("[FrameGrabber][Mac] Warning: Could not set timer for capture reset - no valid world context."));
		bIsCapturing = false;
	}
#else
	// Windows/other: Use FFrameGrabber
	// Try lazy initialization if not yet initialized
	if (!FrameGrabber.IsValid())
	{
		MobiusLog(TEXT("[FrameGrabber] FrameGrabber not initialized, attempting initialization..."));
		if (!TryInitialize())
		{
			MobiusLog(TEXT("[FrameGrabber] Error: Could not initialize FrameGrabber - viewport not ready."));
			MobiusReportError(
				FText::FromString(TEXT("Initialization Failed")),
				FText::FromString(TEXT("Could not initialize screenshot capture. The viewport may not be ready yet.")),
				FText::FromString(TEXT("FrameGrabberHelper::TriggerCapture")),
				EMobiusErrorSeverity::Warning,
				true);
			return;
		}
	}

	MobiusLog(TEXT("[FrameGrabber] Requesting frame capture via FFrameGrabber..."));
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
	{
		MobiusLog(TEXT("[FrameGrabber] ProcessCapturedFrames called with no frames."));
		return;
	}

	const FCapturedFrameData& Frame = Frames[0];

	MobiusLog(FString::Printf(TEXT("[FrameGrabber] Processing captured frame: BufferSize=%dx%d, ColorBuffer=%d pixels"),
		Frame.BufferSize.X, Frame.BufferSize.Y, Frame.ColorBuffer.Num()));

	// Validate frame data
	if (Frame.ColorBuffer.Num() == 0)
	{
		MobiusLog(TEXT("[FrameGrabber] Error: Captured frame has empty ColorBuffer!"));
		MobiusReportError(
			FText::FromString(TEXT("Capture Failed")),
			FText::FromString(TEXT("Screenshot capture returned empty image data.")),
			FText::FromString(TEXT("FrameGrabberHelper::ProcessCapturedFrames")),
			EMobiusErrorSeverity::Error,
			true);
		return;
	}

	if (Frame.BufferSize.X <= 0 || Frame.BufferSize.Y <= 0)
	{
		MobiusLog(FString::Printf(TEXT("[FrameGrabber] Error: Invalid frame buffer size: %dx%d"),
			Frame.BufferSize.X, Frame.BufferSize.Y));
		MobiusReportError(
			FText::FromString(TEXT("Capture Failed")),
			FText::FromString(FString::Printf(TEXT("Screenshot capture returned invalid dimensions: %dx%d"),
				Frame.BufferSize.X, Frame.BufferSize.Y)),
			FText::FromString(TEXT("FrameGrabberHelper::ProcessCapturedFrames")),
			EMobiusErrorSeverity::Error,
			true);
		return;
	}

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
		const bool bCreated = PlatformFile.CreateDirectoryTree(*DestPath);
		if (!bCreated)
		{
			MobiusLog(FString::Printf(TEXT("[FrameGrabber] Error: Failed to create directory: %s"), *DestPath));
			MobiusReportError(
				FText::FromString(TEXT("Directory Error")),
				FText::FromString(FString::Printf(TEXT("Failed to create screenshot directory: %s"), *DestPath)),
				FText::FromString(TEXT("FrameGrabberHelper::ProcessCapturedFrames")),
				EMobiusErrorSeverity::Error,
				true);
			return;
		}
		MobiusLog(FString::Printf(TEXT("[FrameGrabber] Created directory: %s"), *DestPath));
	}

	FString FullPath = DestPath / (PendingFileName + TEXT(".png"));

	// Check if we need to resize or can use buffer directly
	const bool bNeedsResize = (Frame.BufferSize != TargetSize);

	if (bNeedsResize)
	{
		MobiusLog(FString::Printf(TEXT("[FrameGrabber] Resizing frame from %dx%d to %dx%d"),
			Frame.BufferSize.X, Frame.BufferSize.Y, TargetSize.X, TargetSize.Y));

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

			if (PNGData.Num() == 0)
			{
				MobiusLog(FString::Printf(TEXT("[FrameGrabber] Error: PNG compression failed for: %s"), *FullPath));
				MobiusReportError(
					FText::FromString(TEXT("Compression Failed")),
					FText::FromString(TEXT("Failed to compress screenshot to PNG format.")),
					FText::FromString(TEXT("FrameGrabberHelper::ProcessCapturedFrames")),
					EMobiusErrorSeverity::Error,
					true);
				return;
			}

			const bool bSaved = FFileHelper::SaveArrayToFile(PNGData, *FullPath);
			if (!bSaved)
			{
				MobiusLog(FString::Printf(TEXT("[FrameGrabber] Error: Failed to save screenshot to: %s"), *FullPath));
				MobiusReportError(
					FText::FromString(TEXT("Save Failed")),
					FText::FromString(FString::Printf(TEXT("Failed to save screenshot to: %s"), *FullPath)),
					FText::FromString(TEXT("FrameGrabberHelper::ProcessCapturedFrames")),
					EMobiusErrorSeverity::Error,
					true);
				return;
			}

			MobiusLog(FString::Printf(TEXT("[FrameGrabber] Screenshot saved successfully (resized %dx%d): %s"),
				TargetSize.X, TargetSize.Y, *FullPath));
		});
	}
	else
	{
		MobiusLog(FString::Printf(TEXT("[FrameGrabber] Saving frame at full resolution: %dx%d"),
			Frame.BufferSize.X, Frame.BufferSize.Y));

		// Use buffer directly at full resolution
		Async(EAsyncExecution::ThreadPool, [ColorBuffer = Frame.ColorBuffer, FullPath, BufferSize = Frame.BufferSize]()
		{
			TArray64<uint8> PNGData;
			FImageUtils::PNGCompressImageArray(BufferSize.X, BufferSize.Y, ColorBuffer, PNGData);

			if (PNGData.Num() == 0)
			{
				MobiusLog(FString::Printf(TEXT("[FrameGrabber] Error: PNG compression failed for: %s"), *FullPath));
				MobiusReportError(
					FText::FromString(TEXT("Compression Failed")),
					FText::FromString(TEXT("Failed to compress screenshot to PNG format.")),
					FText::FromString(TEXT("FrameGrabberHelper::ProcessCapturedFrames")),
					EMobiusErrorSeverity::Error,
					true);
				return;
			}

			const bool bSaved = FFileHelper::SaveArrayToFile(PNGData, *FullPath);
			if (!bSaved)
			{
				MobiusLog(FString::Printf(TEXT("[FrameGrabber] Error: Failed to save screenshot to: %s"), *FullPath));
				MobiusReportError(
					FText::FromString(TEXT("Save Failed")),
					FText::FromString(FString::Printf(TEXT("Failed to save screenshot to: %s"), *FullPath)),
					FText::FromString(TEXT("FrameGrabberHelper::ProcessCapturedFrames")),
					EMobiusErrorSeverity::Error,
					true);
				return;
			}

			MobiusLog(FString::Printf(TEXT("[FrameGrabber] Screenshot saved successfully (%dx%d): %s"),
				BufferSize.X, BufferSize.Y, *FullPath));
		});
	}
}
#endif // PLATFORM_MAC
