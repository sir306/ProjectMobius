/**
* MIT License
 * Copyright (c) 2025 Nicholas R. Harding
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
    VRCapture->RegisterComponent();  // Ensure it's properly registered

    // Set capture source
    VRCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

    // Create render target (Full HD)
    VRRenderTarget = NewObject<UTextureRenderTarget2D>(this);
    VRRenderTarget->InitAutoFormat(1920, 1080);
    VRCapture->TextureTarget = VRRenderTarget;
}

void UVRScreenCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

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
    // Create a unique folder for this capture
    FString FolderPath = FPaths::ProjectSavedDir() + TEXT("Screenshots/");
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*FolderPath))  // Ensure the directory exists for all screenshots
    {
        PlatformFile.CreateDirectoryTree(*FolderPath);  // Create the directory tree if it doesn't exist
    }

    // Force immediate scene capture to avoid delays
    VRCapture->CaptureSceneDeferred();
    FlushRenderingCommands();  // Ensure the scene is captured immediately
    VRCapture->GetWorld()->Scene->UpdateSceneCaptureContents(VRCapture);

    // Save asynchronously after capture
    AsyncTask(ENamedThreads::GameThread, [this, BaseFileName, FolderPath]() {
        CaptureAndSave(BaseFileName, FolderPath);
    });
}

void UVRScreenCaptureComponent::CaptureAndSave(const FString& BaseFileName, const FString& FolderPath)
{
    TArray<FColor> Pixels;
    FTextureRenderTargetResource* VRResource = VRRenderTarget->GameThread_GetRenderTargetResource();

    VRResource->ReadPixels(Pixels);

    // Save asynchronously to avoid blocking the game thread
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Pixels, FolderPath, BaseFileName]() {
        SaveScreenshot(Pixels, FolderPath + BaseFileName + TEXT(".png"), 1920, 1080);
    });
}

void UVRScreenCaptureComponent::SaveScreenshot(const TArray<FColor>& Bitmap, const FString& FilePath, int32 Width, int32 Height)
{
    // Convert pixel array to PNG and save to disk
    FIntPoint Size(Width, Height);
    FFileHelper::CreateBitmap(*FilePath, Width, Height, Bitmap.GetData(), nullptr, &IFileManager::Get());
}
