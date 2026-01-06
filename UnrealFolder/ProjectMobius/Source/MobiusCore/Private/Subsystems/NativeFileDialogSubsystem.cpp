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

#include "Subsystems/NativeFileDialogSubsystem.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Async/Async.h"

// Include PFD for Windows/Linux
#if PLATFORM_WINDOWS || PLATFORM_LINUX
    #include "PortableFileDialogs/portable-file-dialogs.h"
#endif

// Include AppKit for Mac
#if PLATFORM_MAC
    #include <AppKit/AppKit.h>
    #include <dispatch/dispatch.h>
#endif

// Helper functions (Windows/Linux only to prevent unused function warnings on Mac)
#if PLATFORM_WINDOWS || PLATFORM_LINUX
namespace
{
	static bool IsAgentFileSupported(const FString& FilePath)
	{
		return FilePath.EndsWith(TEXT(".json"));
	}

	static bool IsMeshFileSupported(const FString& FilePath)
	{
		const FString Extension = FPaths::GetExtension(FilePath).ToLower();
		return Extension == TEXT("fbx")
			|| Extension == TEXT("obj")
			|| Extension == TEXT("udatasmith")
			|| Extension == TEXT("ifc")
			|| Extension == TEXT("wkt");
	}
}
#endif

UNativeFileDialogSubsystem::UNativeFileDialogSubsystem()
{
	bSelectionInProgress = false;
	ActiveDialogType = EDialogType::MeshFile;
}

void UNativeFileDialogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
    // Only bind delegates if on Windows/Linux where we use polling
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	FCoreDelegates::OnEnginePreExit.AddUObject(this, &UNativeFileDialogSubsystem::ResetDialogState);
	FCoreDelegates::OnExit.AddUObject(this, &UNativeFileDialogSubsystem::ResetDialogState);
#endif
}

void UNativeFileDialogSubsystem::Deinitialize()
{
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	FCoreDelegates::OnEnginePreExit.RemoveAll(this);
	FCoreDelegates::OnExit.RemoveAll(this);
	ResetDialogState();
#endif
	Super::Deinitialize();
}

void UNativeFileDialogSubsystem::RequestAgentFileDialog(FOnFileSelectedDelegate OnFileSelectedCallback)
{
	StartDialog(EDialogType::AgentFile, OnFileSelectedCallback);
}

void UNativeFileDialogSubsystem::RequestMeshFileDialog(FOnFileSelectedDelegate OnFileSelectedCallback)
{
	StartDialog(EDialogType::MeshFile, OnFileSelectedCallback);
}

void UNativeFileDialogSubsystem::StartDialog(EDialogType DialogType, FOnFileSelectedDelegate OnFileSelectedCallback)
{
	if (bSelectionInProgress)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NativeFileDialogSubsystem] Dialog already in progress."));
		return;
	}

	OnFileSelected = OnFileSelectedCallback;
	ActiveDialogType = DialogType;
	bSelectionInProgress = true;

	// ==========================================================
	// MAC NATIVE IMPLEMENTATION (NSOpenPanel)
	// ==========================================================
#if PLATFORM_MAC
        dispatch_async(dispatch_get_main_queue(), ^{
                @autoreleasepool
                {
                        NSOpenPanel* Panel = [NSOpenPanel openPanel];
                        [Panel setCanChooseFiles:YES];
                        [Panel setCanChooseDirectories:NO];
                        [Panel setAllowsMultipleSelection:NO];

                        NSMutableArray* AllowedTypes = [NSMutableArray array];

                        if (DialogType == EDialogType::AgentFile)
                        {
                                [Panel setMessage:@"Select Agent Data File"];
                                [AllowedTypes addObject:@"json"];
                                [AllowedTypes addObject:@"public.json"];
                        }
                        else
                        {
                                [Panel setMessage:@"Select Mesh File"];
                                [AllowedTypes addObject:@"fbx"];
                                [AllowedTypes addObject:@"obj"];
                                [AllowedTypes addObject:@"udatasmith"];
                                [AllowedTypes addObject:@"ifc"];
                                [AllowedTypes addObject:@"wkt"];
                        }

                        // Disable "Deprecated" warning for this specific line so it compiles cleanly
                        #pragma clang diagnostic push
                        #pragma clang diagnostic ignored "-Wdeprecated-declarations"
                        [Panel setAllowedFileTypes:AllowedTypes];
                        #pragma clang diagnostic pop

                        [Panel beginWithCompletionHandler:^(NSInteger Result)
                        {
                                TArray<FString> SelectedFiles;

                                if (Result == NSModalResponseOK)
                                {
                                        for (NSURL* URL in [Panel URLs])
                                        {
                                                if (URL)
                                                {
                                                        NSString* Path = [URL path];
                                                        if (Path)
                                                        {
                                                                SelectedFiles.Add(FString(UTF8_TO_TCHAR([Path UTF8String])));
                                                        }
                                                }
                                        }
                                }

                                AsyncTask(ENamedThreads::GameThread, [this, SelectedFiles]()
                                {
                                        this->HandleDialogResult(SelectedFiles);
                                        this->ResetDialogState();
                                });
                        }];
                }
        });
        return;
#endif

	// ==========================================================
	// WINDOWS / LINUX IMPLEMENTATION (Portable File Dialogs)
	// ==========================================================
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	const FString InitialDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const std::string InitialDirUtf8(TCHAR_TO_UTF8(*InitialDir));

	std::vector<std::string> Filters;
	if (DialogType == EDialogType::AgentFile)
	{
		Filters = { "Agent Data", "*.json", "JSON Files", "*.json" };
		ActiveDialog = MakeUnique<pfd::open_file>("Select Agent Data File", InitialDirUtf8, Filters, pfd::opt::none);
	}
	else
	{
		Filters = {
			"Mesh Files", "*.fbx *.obj *.udatasmith *.ifc *.wkt",
			"FBX Files", "*.fbx",
			"OBJ Files", "*.obj",
			"Datasmith Files", "*.udatasmith",
			"IFC Files", "*.ifc",
			"WKT Files", "*.wkt"
		};
		ActiveDialog = MakeUnique<pfd::open_file>("Select Mesh File", InitialDirUtf8, Filters, pfd::opt::none);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			PollTimerHandle,
			this,
			&UNativeFileDialogSubsystem::PollDialog,
			0.1f,
			true);
	}
#endif
}

void UNativeFileDialogSubsystem::PollDialog()
{
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	if (!ActiveDialog.IsValid()) return;
	if (!ActiveDialog->ready(0)) return;

	std::vector<std::string> Selection = ActiveDialog->result();
	TArray<FString> SelectedFiles;
	SelectedFiles.Reserve(static_cast<int32>(Selection.size()));
	for (const std::string& FilePath : Selection)
	{
		SelectedFiles.Add(FString(UTF8_TO_TCHAR(FilePath.c_str())));
	}

	HandleDialogResult(SelectedFiles);
	ResetDialogState();
#endif
}

void UNativeFileDialogSubsystem::HandleDialogResult(const TArray<FString>& SelectedFiles)
{
	if (!OnFileSelected.IsBound()) return;

	FString AgentPath;
	FString MeshPath;
	bool bAgentSuccess = false;
	bool bMeshSuccess = false;

	if (SelectedFiles.Num() > 0)
	{
		const FString& SelectedPath = SelectedFiles[0];
		
        // Simple logic: If we asked for Agent, result is Agent.
        // We can double check extension if we want.
		if (ActiveDialogType == EDialogType::AgentFile)
		{
			AgentPath = SelectedPath;
			bAgentSuccess = AgentPath.EndsWith(".json") || AgentPath.EndsWith(".txt");
		}
		else
		{
			MeshPath = SelectedPath;
            // Basic extension check
            FString Ext = FPaths::GetExtension(MeshPath).ToLower();
			bMeshSuccess = (Ext == "fbx" || Ext == "obj" || Ext == "udatasmith" || Ext == "ifc" || Ext == "wkt");
		}
	}

	OnFileSelected.Execute(AgentPath, MeshPath, bAgentSuccess, bMeshSuccess);
}

void UNativeFileDialogSubsystem::ResetDialogState()
{
	bSelectionInProgress = false;

#if PLATFORM_WINDOWS || PLATFORM_LINUX
	if (PollTimerHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PollTimerHandle);
		}
		PollTimerHandle.Invalidate();
	}
	ActiveDialog.Reset();
#endif
}

void UNativeFileDialogSubsystem::BeginDestroy()
{
	Super::BeginDestroy();
    // Only call reset on Windows/Linux where we have state to clear
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	ResetDialogState();
#endif
}
