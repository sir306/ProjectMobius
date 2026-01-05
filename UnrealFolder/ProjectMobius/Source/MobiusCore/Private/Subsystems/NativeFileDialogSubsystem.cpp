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

UNativeFileDialogSubsystem::UNativeFileDialogSubsystem()
{
	bSelectionInProgress = false;
	PollTimerHandle.Invalidate();
	ActiveDialogType = EDialogType::MeshFile;
}

void UNativeFileDialogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FCoreDelegates::OnEnginePreExit.AddUObject(this, &UNativeFileDialogSubsystem::ResetDialogState);
	FCoreDelegates::OnExit.AddUObject(this, &UNativeFileDialogSubsystem::ResetDialogState);
	FCoreDelegates::OnHandleSystemError.AddUObject(this, &UNativeFileDialogSubsystem::ResetDialogState);
}

void UNativeFileDialogSubsystem::Deinitialize()
{
	FCoreDelegates::OnEnginePreExit.RemoveAll(this);
	FCoreDelegates::OnExit.RemoveAll(this);
	FCoreDelegates::OnHandleSystemError.RemoveAll(this);

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
}

void UNativeFileDialogSubsystem::PollDialog()
{
	if (!ActiveDialog.IsValid())
	{
		return;
	}

	if (!ActiveDialog->ready(0))
	{
		return;
	}

	std::vector<std::string> Selection = ActiveDialog->result();
	TArray<FString> SelectedFiles;
	SelectedFiles.Reserve(static_cast<int32>(Selection.size()));
	for (const std::string& FilePath : Selection)
	{
		SelectedFiles.Add(FString(UTF8_TO_TCHAR(FilePath.c_str())));
	}

	HandleDialogResult(SelectedFiles);
	ResetDialogState();
}

void UNativeFileDialogSubsystem::HandleDialogResult(const TArray<FString>& SelectedFiles)
{
	if (!OnFileSelected.IsBound())
	{
		return;
	}

	FString AgentPath;
	FString MeshPath;
	bool bAgentSuccess = false;
	bool bMeshSuccess = false;

	if (SelectedFiles.Num() > 0)
	{
		const FString& SelectedPath = SelectedFiles[0];
		if (ActiveDialogType == EDialogType::AgentFile)
		{
			AgentPath = SelectedPath;
			bAgentSuccess = IsAgentFileSupported(AgentPath);
		}
		else
		{
			MeshPath = SelectedPath;
			bMeshSuccess = IsMeshFileSupported(MeshPath);
		}
	}

	OnFileSelected.Execute(AgentPath, MeshPath, bAgentSuccess, bMeshSuccess);
}

void UNativeFileDialogSubsystem::ResetDialogState()
{
	bSelectionInProgress = false;

	if (PollTimerHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PollTimerHandle);
		}
		PollTimerHandle.Invalidate();
	}

	ActiveDialog.Reset();
}

void UNativeFileDialogSubsystem::BeginDestroy()
{
	Super::BeginDestroy();

	ResetDialogState();
}
