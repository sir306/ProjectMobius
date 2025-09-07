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

#include "Subsystems/QtFileOpenSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HttpManager.h"
#include "Misc/OutputDeviceNull.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"


UQtFileOpenSubsystem::UQtFileOpenSubsystem():
	QtProcessID(0)
{
	bSelectionInProgress = false;
	PollTimerHandle.Invalidate();
}

void UQtFileOpenSubsystem::LaunchQtDialogApp()
{
	const FString QtAppPath = ResolveQtAppExecutablePath();
	const FString QtAppArgs = BuildQtArgs();

	if (!FPaths::FileExists(QtAppPath))
	{
		UE_LOG(LogTemp, Error, TEXT("[QtFileOpenSubsystem] Qt app not found at: %s"), *QtAppPath);
		return;
	}

	// CreateProc notes:
	// - Use 'true' for bLaunchDetached so the app survives if Unreal reloads modules.
	// - Keep priority = 0 (normal).
	// - Leave WorkingDir = nullptr (inherits).
	QtProcessHandle = FPlatformProcess::CreateProc(
		*QtAppPath,          // Executable binary (on macOS: Contents/MacOS/OpenFileTCP)
		*QtAppArgs,          // Arguments (quoted paths safe)
		true,                // bLaunchDetached
		false,               // bLaunchHidden
		false,               // bLaunchReallyHidden
		&QtProcessID,        // Out Process ID
		0,                   // Priority
		nullptr,             // WorkingDir
		nullptr              // Pipe (unused)
	);

	if (QtProcessHandle.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("[QtFileOpenSubsystem] Launched Qt app: %s (pid=%u)"), *QtAppPath, QtProcessID);
		// Give the Qt app a moment to start its HTTP server
		FPlatformProcess::Sleep(0.5f);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[QtFileOpenSubsystem] Failed to launch Qt app: %s"), *QtAppPath);
	}
}

bool UQtFileOpenSubsystem::IsQtAppRunning()
{
	if (!QtProcessHandle.IsValid())
		return false;
        
	return FPlatformProcess::IsProcRunning(QtProcessHandle);
}

void UQtFileOpenSubsystem::RequestFileDialogFromQt(FOnFileSelectedDelegate OnFileSelectedCallback, FString HttpAddress)
{
	if (!IsQtAppRunning())
	{
		UE_LOG(LogTemp, Warning, TEXT("Qt app not running. Launching now..."));
		LaunchQtDialogApp();
	}
	else
	{
		// If its already running, we need to 
	}
    
	// Store the callback for later use
	OnFileSelected = OnFileSelectedCallback;

	// create the HTTP request to open the file dialog
	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();

	// Set the URL to the Qt app's HTTP server
	FString QtAppURL = FString::Printf(TEXT("http://127.0.0.1:8080/")) + HttpAddress;
    
	Request->SetURL(QtAppURL);
	Request->SetVerb(TEXT("GET"));

	Request->OnProcessRequestComplete().BindUObject(this, &UQtFileOpenSubsystem::OnFileDialogRequested);
	Request->ProcessRequest();
}

void UQtFileOpenSubsystem::OnFileDialogRequested(FHttpRequestPtr RequestPtr, FHttpResponsePtr Response, bool bSuccess)
{
	if (bSuccess && Response.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("File dialog opened. Awaiting user selection..."));
		bSelectionInProgress = true;
        
		// Start polling for the result
		GetWorld()->GetTimerManager().SetTimer(
			PollTimerHandle,
			this,
			&UQtFileOpenSubsystem::PollSelectedFileFromQt,
			0.5f, // Poll every half second
			true  // Looping
		);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to open Qt file dialog. Server may not be running."));
        
		// Try to relaunch the Qt app
		LaunchQtDialogApp();
        
		// Notify of failure
		if (OnFileSelected.IsBound())
			OnFileSelected.Execute(TEXT(""),TEXT(""),false,false);
	}
}

void UQtFileOpenSubsystem::PollSelectedFileFromQt()
{
	if (!bSelectionInProgress)
		return;
        
	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(TEXT("http://127.0.0.1:8080/getFileResult"));
	Request->SetVerb(TEXT("GET"));

	Request->OnProcessRequestComplete().BindUObject(this, &UQtFileOpenSubsystem::OnFilePollComplete);
	Request->ProcessRequest();
}

void UQtFileOpenSubsystem::OnFilePollComplete(FHttpRequestPtr RequestPtr, FHttpResponsePtr Response, bool bSuccess)
{
	if (bSuccess && Response.IsValid())
	{
		FString ResponseString = Response->GetContentAsString();

		// Check for pending state
		if (ResponseString.Contains("SELECTION_PENDING"))
		{
			return;
		}

		if (ResponseString == "NO_SELECTION")
		{
			// clear the timer and reset state
			GetWorld()->GetTimerManager().ClearTimer(PollTimerHandle);
			bSelectionInProgress = false;

			// user cancelled
			UE_LOG(LogTemp, Error, TEXT("User Cancelled => Response: %s"), *ResponseString);
			OnFileSelected.Execute(TEXT(""),TEXT(""),false,false);
			return;
		}
        
		// Parse as JSON
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseString);

		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to parse JSON response from Qt dialog: %s"), *ResponseString);
			GetWorld()->GetTimerManager().ClearTimer(PollTimerHandle);
			bSelectionInProgress = false;

			if (OnFileSelected.IsBound())
				OnFileSelected.Execute(TEXT(""),TEXT(""),false,false);
			return;
		}

		GetWorld()->GetTimerManager().ClearTimer(PollTimerHandle);
		bSelectionInProgress = false;

		FString AgentPath = JsonObject->GetStringField(TEXT("agentFile"));
		FString MeshPath  = JsonObject->GetStringField(TEXT("meshFile"));
        
		bool bAgentSuccess = !AgentPath.IsEmpty() && AgentPath.EndsWith(TEXT(".json"));
		bool bMeshSuccess = !MeshPath.IsEmpty() && (MeshPath.EndsWith(TEXT(".fbx")) || MeshPath.EndsWith(TEXT(".obj")) || MeshPath.EndsWith(TEXT(".udatasmith")) || MeshPath.EndsWith(TEXT(".ifc")) || MeshPath.EndsWith(TEXT(".wkt")));


		if (OnFileSelected.IsBound())
		{
			OnFileSelected.Execute(AgentPath, MeshPath, bAgentSuccess,bMeshSuccess);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to poll selected file from Qt app."));
        
		// Stop polling on communication error
		GetWorld()->GetTimerManager().ClearTimer(PollTimerHandle);
		bSelectionInProgress = false;
        
		// Notify of failure
		if (OnFileSelected.IsBound())
			OnFileSelected.Execute(TEXT(""),TEXT(""),false,false);
	}
}

FString UQtFileOpenSubsystem::ResolveQtAppExecutablePath() const
{
	// Base folder shared by both platforms
	const FString ToolsDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/QT_Apps/OpenFileTCP/build"));

#if PLATFORM_WINDOWS
	// .../build/WindowsExe/OpenFileTCP.exe
	return FPaths::Combine(ToolsDir, TEXT("WindowsExe/OpenFileTCP.exe"));
#elif PLATFORM_MAC
	// macdeployqt produces:
	// .../build/MacExe/OpenFileTCP.app/Contents/MacOS/OpenFileTCP
	const FString AppBundleBin = FPaths::Combine(ToolsDir, TEXT("MacExe/OpenFileTCP.app/Contents/MacOS/OpenFileTCP"));
	return AppBundleBin;
#else
	// Fallback (not officially supported in your CMake)
	return FPaths::Combine(ToolsDir, TEXT("Deploy/OpenFileTCP"));
#endif
}

FString UQtFileOpenSubsystem::BuildQtArgs() const
{
	// Use project root as initial directory (quote it for safety)
	const FString InitialDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString QuotedInitialDir = FString::Printf(TEXT("\"%s\""), *InitialDir);

	// Add more args as your Qt app grows (port, endpoints, presets, etc.)
	return FString::Printf(TEXT("--initialDir=%s"), *QuotedInitialDir);
}

void UQtFileOpenSubsystem::RequestQuitQtApp()
{
	if (!IsQtAppRunning())
		return;
        
	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(TEXT("http://127.0.0.1:8080/quit"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("text/plain"));

	Request->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr RequestPtr, FHttpResponsePtr Response, bool bSuccess)
	{
		if (bSuccess && Response.IsValid() && QtProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(QtProcessHandle))
		{
			UE_LOG(LogTemp, Log, TEXT("Qt app shutdown request successful: %s"), *Response->GetContentAsString());
            
			// Clean up the process handle
			FPlatformProcess::CloseProc(QtProcessHandle);
			QtProcessHandle.Reset();
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to send shutdown request to Qt app."));
		}
	});

	Request->ProcessRequest();
}

void UQtFileOpenSubsystem::BeginDestroy()
{
	Super::BeginDestroy();
    
	// Make sure we clean up the Qt app when this object is destroyed
	if (IsQtAppRunning())
	{
		RequestQuitQtApp();
	}
}
