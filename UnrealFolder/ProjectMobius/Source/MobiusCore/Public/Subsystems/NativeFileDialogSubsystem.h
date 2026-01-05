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

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"

#ifndef __APPLE__
#define __APPLE__ 0
#endif

#ifndef _WIN32
#define _WIN32 0
#endif

#ifndef _WIN64
#define _WIN64 0
#endif

#ifndef __EMSCRIPTEN__
#define __EMSCRIPTEN__ 0
#endif

#ifndef __NX__
#define __NX__ 0
#endif

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4191)
#endif
#include "PortableFileDialogs/portable-file-dialogs.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#include "NativeFileDialogSubsystem.generated.h"

// Delegate for file selection callback
DECLARE_DYNAMIC_DELEGATE_FourParams(FOnFileSelectedDelegate, const FString&, AgentFilePath, const FString&, MeshFilePath,bool, bAgentSuccess, bool, bMeshSuccess);

/**
 * 
 */
UCLASS()
class MOBIUSCORE_API UNativeFileDialogSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Default constructor. */
	UNativeFileDialogSubsystem();

	/**
	 * Initialize the subsystem.
	 * @param Collection Subsystem collection for dependencies.
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 * Deinitialize the subsystem and stop any pending dialogs.
	 */
	virtual void Deinitialize() override;

	/**
	 * Request the agent data file dialog.
	 * @param OnFileSelectedCallback Callback executed when the dialog completes.
	 */
	UFUNCTION(BlueprintCallable, Category = "File Dialog")
	void RequestAgentFileDialog(FOnFileSelectedDelegate OnFileSelectedCallback);

	/**
	 * Request the mesh file dialog.
	 * @param OnFileSelectedCallback Callback executed when the dialog completes.
	 */
	UFUNCTION(BlueprintCallable, Category = "File Dialog")
	void RequestMeshFileDialog(FOnFileSelectedDelegate OnFileSelectedCallback);

	UPROPERTY()
	FOnFileSelectedDelegate OnFileSelected;

protected:
	/**
	 * Release any pending dialogs and timers.
	 */
	virtual void BeginDestroy() override;

private:
	/** Dialog type used to interpret results. */
	enum class EDialogType : uint8
	{
		AgentFile,
		MeshFile
	};

	/**
	 * Create a dialog and begin polling for completion.
	 * @param DialogType Dialog type to show.
	 * @param OnFileSelectedCallback Callback executed when the dialog completes.
	 */
	void StartDialog(EDialogType DialogType, FOnFileSelectedDelegate OnFileSelectedCallback);

	/**
	 * Poll the async dialog state and emit the callback once ready.
	 */
	void PollDialog();

	/**
	 * Convert dialog results into agent/mesh outputs and fire the delegate.
	 * @param SelectedFiles Files chosen by the user.
	 */
	void HandleDialogResult(const TArray<FString>& SelectedFiles);

	/**
	 * Clear any pending dialog state.
	 */
	void ResetDialogState();

private:
	UPROPERTY()
	bool bSelectionInProgress;

	UPROPERTY()
	FTimerHandle PollTimerHandle;

	EDialogType ActiveDialogType;

	TUniquePtr<pfd::open_file> ActiveDialog;
};
