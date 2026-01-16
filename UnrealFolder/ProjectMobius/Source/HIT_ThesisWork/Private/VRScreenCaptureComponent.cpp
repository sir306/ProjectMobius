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

#include "VRScreenCaptureComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Async/Async.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"


UVRScreenCaptureComponent::UVRScreenCaptureComponent()
{
    PrimaryComponentTick.bCanEverTick = true;  // Enable ticking to track the pawn's camera
}

void UVRScreenCaptureComponent::BeginPlay()
{
    Super::BeginPlay();
    InitializeCaptureComponent();
}

void UVRScreenCaptureComponent::InitializeCaptureComponent()
{
    // Create and initialize SceneCapture2D component
    VRCapture = NewObject<USceneCaptureComponent2D>(this);
    if (!VRCapture)
    {
        if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
        {
            Feedback->ReportError(
                FText::FromString("Screenshot Error"),
                FText::FromString("Capture component missing"),
                FText::FromString("Failed to create the VR scene capture component."),
                FText::FromString("VRScreenCaptureComponent"));
        }
        return;
    }
    VRCapture->RegisterComponent();  // Ensure it's properly registered

    // Set capture source
    VRCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

    // Create render target (Full HD)
    VRRenderTarget = NewObject<UTextureRenderTarget2D>(this);
    if (!VRRenderTarget)
    {
        if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
        {
            Feedback->ReportError(
                FText::FromString("Screenshot Error"),
                FText::FromString("Render target missing"),
                FText::FromString("Failed to create the screenshot render target."),
                FText::FromString("VRScreenCaptureComponent"));
        }
        return;
    }
    VRRenderTarget->InitAutoFormat(1920, 1080);
    VRCapture->TextureTarget = VRRenderTarget;
}

void UVRScreenCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!VRCapture)
    {
        return;
    }

    // Ensure the SceneCapture2D component always mirrors the pawn's camera transform
    AActor* Owner = GetOwner();
    if (Owner)
    {
        UCameraComponent* CameraComponent = Owner->FindComponentByClass<UCameraComponent>();
        if (CameraComponent)
        {
            FTransform CameraTransform = CameraComponent->GetComponentTransform();
            VRCapture->SetWorldTransform(CameraTransform);
        }
    }
}

void UVRScreenCaptureComponent::TakeScreenshot(const FString& BaseFileName)
{
    if (!VRCapture || !VRRenderTarget)
    {
        if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
        {
            Feedback->ReportError(
                FText::FromString("Screenshot Error"),
                FText::FromString("Capture not initialized"),
                FText::FromString("Screenshot capture components are not ready."),
                FText::FromString("VRScreenCaptureComponent"));
        }
        return;
    }

    // Create a unique folder for this capture
    FString FolderPath = FPaths::ProjectSavedDir() + TEXT("Screenshots/");
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*FolderPath))  // Ensure the directory exists for all screenshots
    {
        if (!PlatformFile.CreateDirectoryTree(*FolderPath))  // Create the directory tree if it doesn't exist
        {
            if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
            {
                Feedback->ReportError(
                    FText::FromString("Screenshot Error"),
                    FText::FromString("Output folder unavailable"),
                    FText::FromString("Failed to create the screenshot output folder."),
                    FText::FromString("VRScreenCaptureComponent"));
            }
            return;
        }
    }

    // Force immediate scene capture to avoid delays
    VRCapture->CaptureSceneDeferred();
    FlushRenderingCommands();  // Ensure the scene is captured immediately
    if (UWorld* World = VRCapture->GetWorld())
    {
        if (World->Scene)
        {
            World->Scene->UpdateSceneCaptureContents(VRCapture);
        }
        else
        {
            if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
            {
                Feedback->ReportError(
                    FText::FromString("Screenshot Error"),
                    FText::FromString("Scene unavailable"),
                    FText::FromString("World scene is not available for capture."),
                    FText::FromString("VRScreenCaptureComponent"));
            }
            return;
        }
    }
    else
    {
        if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
        {
            Feedback->ReportError(
                FText::FromString("Screenshot Error"),
                FText::FromString("World unavailable"),
                FText::FromString("No valid world is available for screenshot capture."),
                FText::FromString("VRScreenCaptureComponent"));
        }
        return;
    }

    // Save asynchronously after capture
    AsyncTask(ENamedThreads::GameThread, [this, BaseFileName, FolderPath]() {
        CaptureAndSave(BaseFileName, FolderPath);
    });
}

void UVRScreenCaptureComponent::CaptureAndSave(const FString& BaseFileName, const FString& FolderPath)
{
    if (!VRRenderTarget)
    {
        if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
        {
            Feedback->ReportError(
                FText::FromString("Screenshot Error"),
                FText::FromString("Render target missing"),
                FText::FromString("Screenshot render target is not available."),
                FText::FromString("VRScreenCaptureComponent"));
        }
        return;
    }
    TArray<FColor> Pixels;
    FTextureRenderTargetResource* VRResource = VRRenderTarget->GameThread_GetRenderTargetResource();

    if (!VRResource)
    {
        if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
        {
            Feedback->ReportError(
                FText::FromString("Screenshot Error"),
                FText::FromString("Render target resource missing"),
                FText::FromString("Could not read from the screenshot render target."),
                FText::FromString("VRScreenCaptureComponent"));
        }
        return;
    }
    VRResource->ReadPixels(Pixels);

    // Save asynchronously to avoid blocking the game thread
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Pixels, FolderPath, BaseFileName]() {
        SaveScreenshot(Pixels, FolderPath + BaseFileName + TEXT(".png"), 1920, 1080, TWeakObjectPtr<UObject>(this));
    });
}

void UVRScreenCaptureComponent::SaveScreenshot(const TArray<FColor>& Bitmap, const FString& FilePath, int32 Width, int32 Height, TWeakObjectPtr<UObject> ContextObject)
{
    // Convert pixel array to PNG and save to disk
    FIntPoint Size(Width, Height);
    if (!FFileHelper::CreateBitmap(*FilePath, Width, Height, Bitmap.GetData(), nullptr, &IFileManager::Get()))
    {
        if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(ContextObject.Get()))
        {
            Feedback->ReportError(
                FText::FromString("Screenshot Error"),
                FText::FromString("Failed to save screenshot"),
                FText::FromString("Could not write the screenshot file to disk."),
                FText::FromString("VRScreenCaptureComponent"));
        }
    }
}
