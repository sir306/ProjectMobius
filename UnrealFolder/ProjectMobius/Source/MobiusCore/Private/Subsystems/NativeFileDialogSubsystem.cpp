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
#include "Subsystems/MobiusUserFeedbackSubsystem.h"

// Define log category for file dialog operations
DEFINE_LOG_CATEGORY_STATIC(LogNativeFileDialog, Log, All);

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
#endif
	ResetDialogState();
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
	UE_LOG(LogNativeFileDialog, Log, TEXT("Starting Mac file dialog. Type: %s"),
		DialogType == EDialogType::AgentFile ? TEXT("AgentFile") : TEXT("MeshFile"));

	TWeakObjectPtr<UNativeFileDialogSubsystem> WeakThis(this);
	const EDialogType DialogTypeCopy = DialogType;
	dispatch_async(dispatch_get_main_queue(), ^{
		@autoreleasepool
		{
			UE_LOG(LogNativeFileDialog, Log, TEXT("Creating NSOpenPanel on main queue"));

			NSOpenPanel* Panel = [NSOpenPanel openPanel];
			if (Panel == nil)
			{
				UE_LOG(LogNativeFileDialog, Error, TEXT("Failed to create NSOpenPanel"));
				AsyncTask(ENamedThreads::GameThread, [WeakThis]()
				{
					if (WeakThis.IsValid())
					{
						WeakThis->ReportDialogError(
							TEXT("Dialog Creation Failed"),
							TEXT("Could not create the file selection dialog. This may be a system resource issue."));
					}
				});
				return;
			}

			[Panel setCanChooseFiles:YES];
			[Panel setCanChooseDirectories:NO];
			[Panel setAllowsMultipleSelection:NO];

			NSMutableArray* AllowedTypes = [NSMutableArray array];

			if (DialogTypeCopy == EDialogType::AgentFile)
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

			// Window acquisition fallback chain
			NSWindow* TargetWindow = nil;
			const char* WindowMethod = "none";

			// Attempt 1: Key window (current focused window)
			TargetWindow = [NSApp keyWindow];
			if (TargetWindow != nil)
			{
				WindowMethod = "keyWindow";
			}

			// Attempt 2: Main window
			if (TargetWindow == nil)
			{
				TargetWindow = [NSApp mainWindow];
				if (TargetWindow != nil)
				{
					WindowMethod = "mainWindow";
				}
			}

			// Attempt 3: First ordered window
			if (TargetWindow == nil)
			{
				NSArray<NSWindow*>* OrderedWindows = [NSApp orderedWindows];
				if (OrderedWindows.count > 0)
				{
					TargetWindow = OrderedWindows[0];
					WindowMethod = "orderedWindows[0]";
				}
			}

			UE_LOG(LogNativeFileDialog, Log, TEXT("Window acquisition method: %s, Window valid: %s"),
				UTF8_TO_TCHAR(WindowMethod),
				TargetWindow != nil ? TEXT("Yes") : TEXT("No"));

			if (TargetWindow != nil)
			{
				// Show as sheet attached to window
				UE_LOG(LogNativeFileDialog, Log, TEXT("Presenting dialog as sheet modal"));
				[Panel beginSheetModalForWindow:TargetWindow completionHandler:^(NSInteger Result)
				{
					UE_LOG(LogNativeFileDialog, Log, TEXT("Sheet dialog completed with result: %ld"), (long)Result);

					// Retain the URLs array to safely pass to game thread
					NSArray<NSURL*>* URLs = (Result == NSModalResponseOK) ? [[Panel URLs] retain] : nil;

					AsyncTask(ENamedThreads::GameThread, [WeakThis, URLs]()
					{
						// Do all Unreal allocations on game thread to avoid memory corruption
						TArray<FString> SelectedFiles;

						if (URLs != nil)
						{
							for (NSURL* URL in URLs)
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
							[URLs release];
						}

						UE_LOG(LogNativeFileDialog, Log, TEXT("Processing %d selected files"), SelectedFiles.Num());

						if (!WeakThis.IsValid())
						{
							UE_LOG(LogNativeFileDialog, Warning, TEXT("Subsystem no longer valid, discarding results"));
							return;
						}
						WeakThis->HandleDialogResult(SelectedFiles);
						WeakThis->ResetDialogState();
					});
				}];
			}
			else
			{
				// No window available - use runModal as reliable fallback
				UE_LOG(LogNativeFileDialog, Warning, TEXT("No window available for sheet. Using runModal fallback."));

				NSInteger Result = [Panel runModal];
				UE_LOG(LogNativeFileDialog, Log, TEXT("runModal completed with result: %ld"), (long)Result);

				// Process result immediately since runModal is synchronous
				NSArray<NSURL*>* URLs = (Result == NSModalResponseOK) ? [Panel URLs] : nil;

				// Copy paths before leaving autorelease pool
				NSMutableArray<NSString*>* PathStrings = [NSMutableArray array];
				if (URLs != nil)
				{
					for (NSURL* URL in URLs)
					{
						if (URL)
						{
							NSString* Path = [URL path];
							if (Path)
							{
								[PathStrings addObject:[Path copy]];
							}
						}
					}
				}

				// Retain paths array to pass to game thread
				[PathStrings retain];

				AsyncTask(ENamedThreads::GameThread, [WeakThis, PathStrings]()
				{
					TArray<FString> SelectedFiles;

					for (NSString* Path in PathStrings)
					{
						SelectedFiles.Add(FString(UTF8_TO_TCHAR([Path UTF8String])));
						[Path release];
					}
					[PathStrings release];

					UE_LOG(LogNativeFileDialog, Log, TEXT("Processing %d selected files from runModal"), SelectedFiles.Num());

					if (!WeakThis.IsValid())
					{
						UE_LOG(LogNativeFileDialog, Warning, TEXT("Subsystem no longer valid, discarding results"));
						return;
					}
					WeakThis->HandleDialogResult(SelectedFiles);
					WeakThis->ResetDialogState();
				});
			}
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

void UNativeFileDialogSubsystem::ReportDialogError(const FString& ErrorTitle, const FString& ErrorMessage)
{
	if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
	{
		Feedback->ReportError(
			FText::FromString("File Dialog Error"),
			FText::FromString(ErrorTitle),
			FText::FromString(ErrorMessage),
			FText::FromString("NativeFileDialogSubsystem"));
	}
	UE_LOG(LogNativeFileDialog, Error, TEXT("File Dialog Error: %s - %s"), *ErrorTitle, *ErrorMessage);

	// Fire error delegate if bound
	if (OnDialogError.IsBound())
	{
		OnDialogError.Execute(ErrorTitle, ErrorMessage);
	}

	// Reset dialog state since we encountered an error
	ResetDialogState();
}

void UNativeFileDialogSubsystem::ResetDialogState()
{
	bSelectionInProgress = false;
	OnFileSelected.Unbind();
	OnDialogError.Unbind();

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
