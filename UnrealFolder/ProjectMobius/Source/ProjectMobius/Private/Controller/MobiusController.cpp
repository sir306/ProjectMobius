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

#include "Controller/MobiusController.h"

#include "HeadMountedDisplayFunctionLibrary.h"
#include "ImageUtils.h"
#include "IXRTrackingSystem.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Subsystems/MobiusControllerSubsystem.h"
#include "SubSystems/TimeDilationSubSystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Util/FrameGrabberHelper.h"


AMobiusController::AMobiusController()
{

}

void AMobiusController::BeginPlay()
{
	Super::BeginPlay();
	bool bVr = UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled();
	
	if (bVr)
	{
		// --- VR PROFILE: Game-only; OS cursor off; WidgetInteraction drives UI ---
		FInputModeGameOnly Mode;
		SetInputMode(Mode);

		// In VR you don’t want a desktop cursor getting in the way.
		bShowMouseCursor = false;

		// Safety: make sure mouse-generated click/hover events don’t interfere
		bEnableClickEvents = false;
		bEnableMouseOverEvents = false;
	}
	else
	{
		// --- DESKTOP PROFILE: Game + UI; cursor visible; no lock; don’t hide on capture ---
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);

		// Optionally set a specific widget to focus (UMG root), if you have one:
		// Mode.SetWidgetToFocus(MyRootWidget ? MyRootWidget->TakeWidget() : TSharedPtr<SWidget>{});

		SetInputMode(Mode);

		bShowMouseCursor = true;
		bEnableClickEvents = true;
		bEnableMouseOverEvents = true;
	}
	GetScreenshotRequiredSubsystemsAndData();

	// Initialize FrameGrabberHelper for screenshot capture (lazy initialization - will init when viewport is ready)
	FrameGrabberHelper = NewObject<UFrameGrabberHelper>(this);
	FrameGrabberHelper->Configure(false, FIntPoint(1920, 1080)); // Use downscale mode with 1920x1080 target resolution
	FrameGrabberHelper->SetSavePath(ScreenshotFilePath + TEXT("/MobiusCaptures/"));

	// send this to the Mobius Controller Subsystem to set the current player controller
	if (UMobiusControllerSubsystem* MobiusControllerSubsystem = GetWorld()->GetSubsystem<UMobiusControllerSubsystem>())
	{
		MobiusControllerSubsystem->SetCurrentPlayerController(this);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Mobius Controller Subsystem is not valid"));
	}
}

void AMobiusController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CachedGameInstance.IsValid())
	{
		CachedGameInstance->OnPedestrianVectorFileChanged.RemoveDynamic(this, &AMobiusController::UpdatePedestrianVectorFilePath);
		CachedGameInstance = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void AMobiusController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (FrameGrabberHelper)
	{
		FrameGrabberHelper->Tick(DeltaTime);
	}
}

void AMobiusController::GetScreenshotRequiredSubsystemsAndData()
{
	if (GetWorld())
	{
		TimeDilationSubsystem = GetWorld()->GetSubsystem<UTimeDilationSubSystem>();

		// Get game instance
		if (auto GameInst = GetWorld()->GetGameInstance())
		{
			// cast to mobius instance
			if (UProjectMobiusGameInstance* MobiusGameInstance = Cast<UProjectMobiusGameInstance>(GameInst))
			{
				CachedGameInstance = MobiusGameInstance;
				// bind the file update delegate
				MobiusGameInstance->OnPedestrianVectorFileChanged.AddDynamic(this, &AMobiusController::UpdatePedestrianVectorFilePath);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Mobius Game Instance is not valid"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Game Instance is not valid"));
		}
	}
}

void AMobiusController::UpdatePedestrianVectorFilePath(FString NewFilePath)
{
	// this is the full file path to the pedestrian vector file
	// Get the file path
	ScreenshotFilePath = FPaths::GetPath(NewFilePath);

	// strip the filename and extension from the file path and set the file name
	UpdatePedestrianVectorFileName(FPaths::GetBaseFilename(NewFilePath));
}

void AMobiusController::UpdatePedestrianVectorFileName(FString NewFileName)
{
	PedestrianVectorFileName = NewFileName;
}

void AMobiusController::TakeScreenshot()
{
	// ensure the time file name extension is safe 
	FString SafeTimeString = FDateTime::Now().ToString(TEXT("%d-%m-%y_%H-%M-%S"));

	FString SaveFilename = PedestrianVectorFileName;
	
	// if the time dilation subsystem is valid then get the current sim time
	if (TimeDilationSubsystem)
	{
		// Get the current simulation time
		float TotalTime = TimeDilationSubsystem->GetCurrentSimTime();

		UE_LOG(LogTemp, Warning, TEXT("Total Time: %f"), TotalTime);

		const bool bIncludeHours = FMath::FloorToInt32(TotalTime / 3600) > 0;
		const FString TimeString = TimeDilationSubsystem->FormatSimTime(TotalTime, bIncludeHours).ToString();

		TArray<FString> Parts;
		TimeString.ParseIntoArray(Parts, TEXT(":"));

		if (bIncludeHours && Parts.Num() == 3)
		{
			FString SecStr, MsStr;
			Parts[2].Split(TEXT("."), &SecStr, &MsStr);
			SaveFilename += FString::Printf(TEXT("_%sh%sm%ss%sms_"), *Parts[0], *Parts[1], *SecStr, *MsStr);
		}
		else if (!bIncludeHours && Parts.Num() >= 2)
		{
			FString SecStr, MsStr;
			Parts[1].Split(TEXT("."), &SecStr, &MsStr);
			SaveFilename += FString::Printf(TEXT("_%sm%ss%sms_"), *Parts[0], *SecStr, *MsStr);
		}
		else
		{
			SaveFilename += FString::Printf(TEXT("_%s_"), *TimeString.Replace(TEXT(":"), TEXT("-")));
		}

		SaveFilename += FString::Printf(TEXT("%s"), *SafeTimeString);
	}
	else // if an issue we should create a file name with current time and date to ensure the data is saved
	{
		SaveFilename += FString::Printf(TEXT("_TimeDilationSystemCorrupt_%s"), *SafeTimeString);

	}
	//log the file name
	UE_LOG(LogTemp, Warning, TEXT("Pedestrian Vector File Name: %s"), *SaveFilename);
	
	TakeScreenshot(SaveFilename);
}

void AMobiusController::TakeScreenshot(const FString& BaseFileName)
{
	// Create a unique folder for this capture (also used by camera save points)
	FString DestPath = ScreenshotFilePath + TEXT("/MobiusCaptures/");
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*DestPath))  // Ensure the directory exists for all screenshots
	{
		PlatformFile.CreateDirectoryTree(*DestPath);  // Create the directory tree if it doesn't exist
	}

	// Assign the name for the screenshot file
	ScreenShotFileName = BaseFileName;

	// Request the screenshot via FrameGrabberHelper
	if (FrameGrabberHelper)
	{
		FrameGrabberHelper->SetSavePath(DestPath);
		FrameGrabberHelper->TriggerCapture(BaseFileName);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FrameGrabberHelper is not initialized, cannot take screenshot"));
	}
}

void AMobiusController::LoadCameraSavePoints()
{
	// Determine the directory and file path for camera save points
	FString DestPath = ScreenshotFilePath + TEXT("/MobiusCaptures/");
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    
	// If the directory does not exist, abort
	if (!PlatformFile.DirectoryExists(*DestPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("LoadCameraSavePoints: Directory not found: %s"), *DestPath);
		return;
	}

	// Full path to the JSON file
	FString CameraSavePointsFilePath = DestPath + CameraSavePointsFileName;
	if (!PlatformFile.FileExists(*CameraSavePointsFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("LoadCameraSavePoints: File not found: %s"), *CameraSavePointsFilePath);
		return;
	}

	// Read the entire file into a string
	FString FileContents;
	if (!FFileHelper::LoadFileToString(FileContents, *CameraSavePointsFilePath))
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Camera Save Error"),
				FText::FromString("Failed to load camera save points"),
				FText::FromString("Could not read camera save points file."),
				FText::FromString("MobiusController"));
		}
		UE_LOG(LogTemp, Error, TEXT("LoadCameraSavePoints: Failed to load file: %s"), *CameraSavePointsFilePath);
		return;
	}

	// Parse the JSON root object
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContents);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Camera Save Error"),
				FText::FromString("Invalid camera save JSON"),
				FText::FromString("Camera save points file contains invalid JSON."),
				FText::FromString("MobiusController"));
		}
		UE_LOG(LogTemp, Error, TEXT("LoadCameraSavePoints: Invalid JSON in file: %s"), *CameraSavePointsFilePath);
		return;
	}

	// Retrieve the array field "camera save points"
	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (!RootObject->TryGetArrayField(TEXT("camera save points"), JsonArray))
	{
		UE_LOG(LogTemp, Warning, TEXT("LoadCameraSavePoints: \"camera save points\" array not found in JSON."));
		return;
	}

	// Prepare an array to store the loaded transforms
	TArray<FTransform> LoadedTransforms;
	LoadedTransforms.Reserve(JsonArray->Num());

	// Iterate over each JSON object in the array
	for (const TSharedPtr<FJsonValue>& JsonValue : *JsonArray)
	{
		if (!JsonValue.IsValid() || !JsonValue->AsObject().IsValid())
		{
			continue;
		}

		TSharedPtr<FJsonObject> PointObject = JsonValue->AsObject();
        
		// Extract each string field
		FString LocString    = PointObject->GetStringField(TEXT("Location"));
		FString RotString    = PointObject->GetStringField(TEXT("Rotation"));
		FString ScaleString  = PointObject->GetStringField(TEXT("Scale"));

		// Parse strings back into FVector / FRotator
		FVector Location;
		Location.InitFromString(LocString);

		FRotator Rotation;
		Rotation.InitFromString(RotString);

		FVector Scale;
		Scale.InitFromString(ScaleString);

		// Construct an FTransform and add to our array
		FTransform LoadedTransform(Rotation, Location, Scale);
		LoadedTransforms.Add(LoadedTransform);
	}

	// At this point, LoadedTransforms contains all saved camera transforms.
	// Store them into the member
	LoadedCameraTransforms = MoveTemp(LoadedTransforms);
}


void AMobiusController::SaveCameraSavePoint(const FTransform& CameraTransform)
{
	// add the new transform to the loaded camera transforms
	LoadedCameraTransforms.Add(CameraTransform);
	
	// Determine the directory and file path for saving camera save points
	FString DestPath = ScreenshotFilePath + TEXT("/MobiusCaptures/");
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    
	// Ensure the directory exists; if not, create it
	if (!PlatformFile.DirectoryExists(*DestPath))
	{
		PlatformFile.CreateDirectoryTree(*DestPath);
	}

	// Full path to the JSON file that holds all camera save points
	FString CameraSavePointsFilePath = DestPath + CameraSavePointsFileName;

	// If the file doesn't exist yet, create an empty file
	if (!PlatformFile.FileExists(*CameraSavePointsFilePath))
	{
		FFileHelper::SaveStringToFile(TEXT(""), *CameraSavePointsFilePath);
	}

	// Load existing contents of the file into a string
	FString ExistingFileContents;
	if (!FFileHelper::LoadFileToString(ExistingFileContents, *CameraSavePointsFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to load existing camera save points from %s"), *CameraSavePointsFilePath);
		// Proceed with an empty JSON root if load fails
		ExistingFileContents = TEXT("");
	}

	// Parse the existing JSON (if any)
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ExistingFileContents);
	if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
	{
		// Parsing succeeded; RootObject now holds the existing JSON
	}
	else
	{
		// Either file was empty or invalid JSON—create a fresh root
		RootObject = MakeShareable(new FJsonObject());
	}

	// Retrieve (or create) the array field "camera save points"
	TArray<TSharedPtr<FJsonValue>> SavePointsArray;
	if (RootObject->HasField(TEXT("camera save points")))
	{
		const TArray<TSharedPtr<FJsonValue>>* ExistingArray;
		if (RootObject->TryGetArrayField(TEXT("camera save points"), ExistingArray))
		{
			SavePointsArray = *ExistingArray;
		}
	}
	// If the array didn't exist, SavePointsArray remains empty and will be created below

	// Create a new JSON object for this specific camera transform
	TSharedPtr<FJsonObject> NewPointObject = MakeShareable(new FJsonObject());
	NewPointObject->SetStringField(TEXT("Location"), CameraTransform.GetLocation().ToString());
	NewPointObject->SetStringField(TEXT("Rotation"), CameraTransform.GetRotation().Rotator().ToString());
	NewPointObject->SetStringField(TEXT("Scale"), CameraTransform.GetScale3D().ToString());

	// Wrap the new object in a JSON value and append it to the array
	SavePointsArray.Add(MakeShareable(new FJsonValueObject(NewPointObject)));

	// Assign the updated array back into the root under "camera save points"
	RootObject->SetArrayField(TEXT("camera save points"), SavePointsArray);

	// Serialize the updated root object back to a JSON-formatted string
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	if (FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
	{
		// Overwrite the file with the new JSON content
		FFileHelper::SaveStringToFile(OutputString, *CameraSavePointsFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	else
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Camera Save Error"),
				FText::FromString("Failed to save camera points"),
				FText::FromString("Could not serialize camera save points to JSON."),
				FText::FromString("MobiusController"));
		}
		UE_LOG(LogTemp, Error, TEXT("Failed to serialize camera save points JSON to string."));
	}
}


void AMobiusController::InterpolateCameraToTransform(const FTransform& TargetTransform, float Duration)
{
	// Ensure we have a valid Camera Manager
	if (PlayerCameraManager)
	{
		// (1) If you want to smooth‐interpolate over time, you could store:
		//     - The starting transform (PlayerCameraManager->GetTransform())
		//     - The target transform (TargetTransform)
		//     - A running elapsed‐time counter (initialized to 0.0f)
		//   Then, on each Tick, lerp between start and target until Elapsed >= Duration.
		//   For brevity, this example simply “snaps” the pawn+camera immediately.

		APawn* FoundPawn = GetPawn();
		if (FoundPawn)
		{
			// (2) Immediately move the pawn to the target location:
			FoundPawn->SetActorLocation(TargetTransform.GetLocation());

			// (3) Immediately rotate the pawn (and camera) to the target rotation:
			FRotator TargetRot = TargetTransform.GetRotation().Rotator();
			FoundPawn->SetActorRotation(TargetRot);

			// (4) Make sure the controller’s view matches that rotation:
			SetControlRotation(TargetRot);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("InterpolateCameraToTransform: Player pawn is null."));
		}

		// ──────────────────────────────────────────────────────────────────────────────
		// NOTE on Smoothing/Interpolation:
		//
		// If you truly need to interpolate over “Duration” instead of snapping:
		//  • Store
		//      FTransform StartTransform = PlayerCameraManager->GetTransform();
		//      FTransform EndTransform   = TargetTransform;
		//      float ElapsedTime         = 0.0f;
		//
		//  • In your Tick(float DeltaSeconds) override:
		//      ElapsedTime = FMath::Min(ElapsedTime + DeltaSeconds, Duration);
		//      float Alpha = (Duration > 0.f) ? (ElapsedTime / Duration) : 1.f;
		//
		//      FVector   NewLoc = FMath::Lerp(StartTransform.GetLocation(), EndTransform.GetLocation(), Alpha);
		//      FRotator  NewRot = FMath::Lerp(StartTransform.GetRotation().Rotator(), EndTransform.GetRotation().Rotator(), Alpha);
		//      
		//      if (APawn* P = GetPawn())
		//      {
		//          P->SetActorLocation(NewLoc);
		//          P->SetActorRotation(NewRot);
		//          SetControlRotation(NewRot);
		//      }
		//
		//      // When Alpha == 1.0f, stop updating (you can ClearTimer, or set a flag to skip further ticks).
		//
		//  • You could also drive interpolation with a Timeline or a looping Timer. The key is:
		//      – Capture StartTransform + TargetTransform + Duration + Elapsed
		//      – Each frame (or timer tick), compute Alpha = Elapsed/Duration, lerp, apply to pawn/camera
		//      – Stop once Alpha ≥ 1.0f.
		//
		// This “immediate snap” implementation above satisfies the basic request. If you do need per‐frame smoothing,
		// pick one of the approaches described (Tick-based lerp or a Timeline).
		// ──────────────────────────────────────────────────────────────────────────────

	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Player Camera Manager is not valid"));
	}
}


void AMobiusController::CycleCameraSavePoints()
{
	// TODO: Current implementation is a placeholder and will load the file each time and then pick a random transform
	LoadCameraSavePoints();

	if (LoadedCameraTransforms.Num() > 0)
	{
		// Get a random index from the loaded camera transforms
		int32 RandomIndex = FMath::RandRange(0, LoadedCameraTransforms.Num() - 1);

		// Interpolate the camera to the random transform
		InterpolateCameraToTransform(LoadedCameraTransforms[RandomIndex], 1.0f); // 1 second duration
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("No camera save points available to cycle through"));
	}
}
