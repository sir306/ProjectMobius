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
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Async/Async.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"

// Define log category for file dialog operations
DEFINE_LOG_CATEGORY_STATIC(LogNativeFileDialog, Log, All);

// Include PFD for Windows/Linux
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	#ifndef _WIN32
		#define _WIN32 0
	#endif
	#ifndef _WIN64
		#define _WIN64 0
	#endif
	#ifndef __APPLE__
		#define __APPLE__ 0
	#endif
	#ifndef __EMSCRIPTEN__
		#define __EMSCRIPTEN__ 0
	#endif
	#ifndef __NX__
		#define __NX__ 0
	#endif

	#if PLATFORM_WINDOWS
		#include "Windows/AllowWindowsPlatformTypes.h"
		#if !defined(SendMessage)
			#if defined(UNICODE)
				#define SendMessage SendMessageW
			#else
				#define SendMessage SendMessageA
			#endif
		#endif
	#endif
	THIRD_PARTY_INCLUDES_START
	#if defined(_MSC_VER)
		#pragma warning(push)
		#pragma warning(disable : 4191)
	#endif
	#include "PortableFileDialogs/portable-file-dialogs.h"
	#if defined(_MSC_VER)
		#pragma warning(pop)
	#endif
	THIRD_PARTY_INCLUDES_END
	#if PLATFORM_WINDOWS
		#include "Windows/HideWindowsPlatformTypes.h"
	#endif

	#if defined(SendMessage)
		#undef SendMessage
	#endif
	#if defined(IsLoggingEnabled)
		#undef IsLoggingEnabled
	#endif
#endif

// Include AppKit for Mac
#if PLATFORM_MAC
    #include <AppKit/AppKit.h>
    #include <dispatch/dispatch.h>
#endif

#if PLATFORM_WINDOWS || PLATFORM_LINUX
UNativeFileDialogSubsystem::FNativeFileDialogState::FNativeFileDialogState()
	: ActiveDialogStorage(new TUniquePtr<pfd::open_file>())
{
}

UNativeFileDialogSubsystem::FNativeFileDialogState::~FNativeFileDialogState()
{
	delete static_cast<TUniquePtr<pfd::open_file>*>(ActiveDialogStorage);
	ActiveDialogStorage = nullptr;
}
#endif

// Helper functions (Windows/Linux only to prevent unused function warnings on Mac)
#if PLATFORM_WINDOWS || PLATFORM_LINUX
namespace
{
	static bool IsAgentFileSupported(const FString& FilePath)
	{
		const FString Extension = FPaths::GetExtension(FilePath).ToLower();
		return Extension == TEXT("json") || Extension == TEXT("h5");
	}

	static bool IsMeshFileSupported(const FString& FilePath)
	{
		const FString Extension = FPaths::GetExtension(FilePath).ToLower();
		return Extension == TEXT("fbx")
			|| Extension == TEXT("obj")
			|| Extension == TEXT("udatasmith")
			|| Extension == TEXT("ifc")
			|| Extension == TEXT("wkt")
			|| (Extension == "h5");
	}
}
#endif

namespace
{
	static const TCHAR* DefaultFilePrompt = TEXT("Click Browse to choose file");

	static bool IsDefaultFilePath(const FString& FilePath)
	{
		return FilePath.IsEmpty() || FilePath.Equals(DefaultFilePrompt);
	}

	static FString ResolveUnitTestSampleDataDir()
	{
		const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FString Candidate = FPaths::Combine(ProjectDir, TEXT("UnitTestSampleData"));
		if (FPaths::DirectoryExists(Candidate))
		{
			return Candidate;
		}

		const FString LaunchDir = FPaths::ConvertRelativePathToFull(FPaths::LaunchDir());
		Candidate = FPaths::Combine(LaunchDir, FApp::GetProjectName(), TEXT("UnitTestSampleData"));
		if (FPaths::DirectoryExists(Candidate))
		{
			return Candidate;
		}

		return FString();
	}
}

UNativeFileDialogSubsystem::UNativeFileDialogSubsystem()
{
	bSelectionInProgress = false;
	ActiveDialogType = EDialogType::MeshFile;
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	NativeDialogState = MakeUnique<FNativeFileDialogState>();
#endif
}

UNativeFileDialogSubsystem::~UNativeFileDialogSubsystem() = default;

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

void UNativeFileDialogSubsystem::RequestBRiskFileDialog(FOnBRiskFileSelectedDelegate OnFileSelectedCallback)
{
	// Store the B-Risk-specific delegate and instruct StartDialog to open a
	// .smv-filtered picker.  A dummy FOnFileSelectedDelegate is passed because
	// StartDialog's signature requires it, but it will not be executed for
	// BRiskFile dialogs – OnBRiskFileSelected is used instead.
	OnBRiskFileSelected = OnFileSelectedCallback;
	StartDialog(EDialogType::BRiskFile, FOnFileSelectedDelegate());
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
	const FString InitialDir = ResolveInitialDialogDirectory();

	// ==========================================================
	// MAC NATIVE IMPLEMENTATION (NSOpenPanel)
	// ==========================================================
#if PLATFORM_MAC
	UE_LOG(LogNativeFileDialog, Log, TEXT("Starting Mac file dialog. Type: %s"),
		DialogType == EDialogType::AgentFile ? TEXT("AgentFile") : TEXT("MeshFile"));

	TWeakObjectPtr<UNativeFileDialogSubsystem> WeakThis(this);
	const EDialogType DialogTypeCopy = DialogType;
	const FString InitialDirCopy = InitialDir;
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

			if (!InitialDirCopy.IsEmpty())
			{
				FString NormalizedDir = InitialDirCopy;
				FPaths::NormalizeDirectoryName(NormalizedDir);
				NSString* InitialDirString = [NSString stringWithUTF8String:TCHAR_TO_UTF8(*NormalizedDir)];
				if (InitialDirString != nil)
				{
					NSURL* InitialDirUrl = [NSURL fileURLWithPath:InitialDirString];
					if (InitialDirUrl != nil)
					{
						[Panel setDirectoryURL:InitialDirUrl];
					}
				}
			}

			NSMutableArray* AllowedTypes = [NSMutableArray array];

			if (DialogTypeCopy == EDialogType::AgentFile)
			{
				[Panel setMessage:@"Select Agent Data File"];
				[AllowedTypes addObject:@"json"];
				[AllowedTypes addObject:@"public.json"];
				[AllowedTypes addObject:@"h5"];
			}
			else if (DialogTypeCopy == EDialogType::BRiskFile)
			{
				[Panel setMessage:@"Select B-Risk Scenario File"];
				[AllowedTypes addObject:@"smv"];
			}
			else
			{
				[Panel setMessage:@"Select Mesh File"];
				[AllowedTypes addObject:@"fbx"];
				[AllowedTypes addObject:@"obj"];
				[AllowedTypes addObject:@"udatasmith"];
				[AllowedTypes addObject:@"ifc"];
				[AllowedTypes addObject:@"wkt"];
				[AllowedTypes addObject:@"h5"];
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
	const std::string InitialDirUtf8(TCHAR_TO_UTF8(*InitialDir));

	// log InitialDirUtf8
	UE_LOG(LogNativeFileDialog, Log, TEXT("Starting Windows/Linux file dialog. Type: %s, InitialDir: %s"),
		DialogType == EDialogType::AgentFile ? TEXT("AgentFile") : TEXT("MeshFile"),
		*InitialDir);

	std::vector<std::string> Filters;
	if (DialogType == EDialogType::AgentFile)
	{
		Filters = { "Agent Data", "*.json *.h5", "JSON Files", "*.json", "HDF5 Files", "*.h5" };
		*static_cast<TUniquePtr<pfd::open_file>*>(NativeDialogState->ActiveDialogStorage) =
			MakeUnique<pfd::open_file>(
				"Select Agent Data File",
				InitialDirUtf8,
				Filters,
				pfd::opt::none);
	}
	else if (DialogType == EDialogType::BRiskFile)
	{
		Filters = { "B-Risk Scenario", "*.smv", "SMV Files", "*.smv" };
		*static_cast<TUniquePtr<pfd::open_file>*>(NativeDialogState->ActiveDialogStorage) =
			MakeUnique<pfd::open_file>(
				"Select B-Risk Scenario File",
				InitialDirUtf8,
				Filters,
				pfd::opt::none);
	}
	else
	{
		Filters = {
			"Mesh Files", "*.fbx *.obj *.udatasmith *.ifc *.wkt *.h5",
			"FBX Files", "*.fbx",
			"OBJ Files", "*.obj",
			"Datasmith Files", "*.udatasmith",
			"IFC Files", "*.ifc",
			"WKT Files", "*.wkt",
			"HDF5 Files", "*.h5"
		};
		*static_cast<TUniquePtr<pfd::open_file>*>(NativeDialogState->ActiveDialogStorage) =
			MakeUnique<pfd::open_file>(
				"Select Mesh File",
				InitialDirUtf8,
				Filters,
				pfd::opt::none);
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

FString UNativeFileDialogSubsystem::ResolveInitialDialogDirectory() const
{
	if (!LastDialogDirectory.IsEmpty())
	{
		const FString LastDir = FPaths::ConvertRelativePathToFull(LastDialogDirectory);
		if (FPaths::DirectoryExists(LastDir))
		{
			return LastDir;
		}
	}

	FString PedestrianPath;
	FString MeshPath;
	if (const UWorld* World = GetWorld())
	{
		if (const UProjectMobiusGameInstance* GameInstance = Cast<UProjectMobiusGameInstance>(World->GetGameInstance()))
		{
			PedestrianPath = GameInstance->GetPedestrianDataFilePath();
			MeshPath = GameInstance->GetSimulationMeshFilePath();
		}
	}

	const bool bPedDefault = IsDefaultFilePath(PedestrianPath);
	const bool bMeshDefault = IsDefaultFilePath(MeshPath);

	if (!bPedDefault)
	{
		const FString PedDir = FPaths::ConvertRelativePathToFull(FPaths::GetPath(PedestrianPath));
		if (FPaths::DirectoryExists(PedDir))
		{
			return PedDir;
		}
	}

	if (!bMeshDefault)
	{
		const FString MeshDir = FPaths::ConvertRelativePathToFull(FPaths::GetPath(MeshPath));
		if (FPaths::DirectoryExists(MeshDir))
		{
			return MeshDir;
		}
	}

	if (bPedDefault && bMeshDefault)
	{
		const FString TestDataDir = ResolveUnitTestSampleDataDir();
		if (!TestDataDir.IsEmpty())
		{
			return TestDataDir;
		}
	}

	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
}

void UNativeFileDialogSubsystem::UpdateLastDialogDirectory(const FString& SelectedPath)
{
	FString SelectedDir = FPaths::ConvertRelativePathToFull(FPaths::GetPath(SelectedPath));
	FPaths::NormalizeDirectoryName(SelectedDir);
	if (!SelectedDir.IsEmpty() && FPaths::DirectoryExists(SelectedDir))
	{
		LastDialogDirectory = SelectedDir;
	}
}

void UNativeFileDialogSubsystem::PollDialog()
{
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	if (!NativeDialogState.IsValid() || !NativeDialogState->ActiveDialogStorage) return;

	TUniquePtr<pfd::open_file>& ActiveDialog =
		*static_cast<TUniquePtr<pfd::open_file>*>(NativeDialogState->ActiveDialogStorage);
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
	if (SelectedFiles.Num() > 0)
	{
		const FString& SelectedPath = SelectedFiles[0];
		UpdateLastDialogDirectory(SelectedPath);

		// ------------------------------------------------------------------
		// B-Risk SMV dialog – fire the dedicated B-Risk delegate.
		// ------------------------------------------------------------------
		if (ActiveDialogType == EDialogType::BRiskFile)
		{
			const bool bSuccess =
				FPaths::GetExtension(SelectedPath).ToLower() == TEXT("smv");

			if (OnBRiskFileSelected.IsBound())
			{
				OnBRiskFileSelected.Execute(SelectedPath, bSuccess);
			}
			return;
		}

		// ------------------------------------------------------------------
		// Agent / Mesh dialogs – fire the shared FOnFileSelectedDelegate.
		// ------------------------------------------------------------------
		if (!OnFileSelected.IsBound()) return;

		FString AgentPath;
		FString MeshPath;
		bool bAgentSuccess = false;
		bool bMeshSuccess  = false;

        // Simple logic: If we asked for Agent, result is Agent.
        // We can double check extension if we want.
		if (ActiveDialogType == EDialogType::AgentFile)
		{
			AgentPath    = SelectedPath;
			bAgentSuccess = AgentPath.EndsWith(".json") || AgentPath.EndsWith(".txt") || AgentPath.EndsWith(".h5");
		}
		else
		{
			MeshPath = SelectedPath;
            // Basic extension check
            FString Ext = FPaths::GetExtension(MeshPath).ToLower();
			bMeshSuccess = (Ext == "fbx" || Ext == "obj" || Ext == "udatasmith" || Ext == "ifc" || Ext == "wkt") || (Ext == "h5");
		}

		OnFileSelected.Execute(AgentPath, MeshPath, bAgentSuccess, bMeshSuccess);
	}
	else
	{
		// No file selected (user cancelled).
		if (ActiveDialogType == EDialogType::BRiskFile)
		{
			if (OnBRiskFileSelected.IsBound())
			{
				OnBRiskFileSelected.Execute(FString(), false);
			}
		}
		else if (OnFileSelected.IsBound())
		{
			OnFileSelected.Execute(FString(), FString(), false, false);
		}
	}
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
	OnBRiskFileSelected.Unbind();
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
	if (NativeDialogState.IsValid() && NativeDialogState->ActiveDialogStorage)
	{
		static_cast<TUniquePtr<pfd::open_file>*>(NativeDialogState->ActiveDialogStorage)->Reset();
	}
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
