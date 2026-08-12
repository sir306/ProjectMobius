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
#include "Subsystems/WorldSubsystem.h" // Added for UWorldSubsystem
#include "NativeFileDialogSubsystem.generated.h"

// Delegate for file selection callback
DECLARE_DYNAMIC_DELEGATE_FourParams(FOnFileSelectedDelegate, const FString&, AgentFilePath, const FString&, MeshFilePath,bool, bAgentSuccess, bool, bMeshSuccess);

// Delegate for dialog error callback
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnDialogErrorDelegate, const FString&, ErrorTitle, const FString&, ErrorMessage);

/**
 * Dedicated delegate for the B-Risk SMV file dialog.
 * Simpler than FOnFileSelectedDelegate since only a single file path is needed.
 */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnBRiskFileSelectedDelegate, const FString&, SmvFilePath, bool, bSuccess);

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
	~UNativeFileDialogSubsystem();

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

	/**
	 * Request a file dialog filtered to B-Risk SMV manifests (*.smv).
	 * The callback receives the absolute path and a success flag.
	 * Typical usage:
	 *   FOnBRiskFileSelectedDelegate Del;
	 *   Del.BindDynamic(this, &UMyWidget::OnBRiskFileChosen);
	 *   FileDialogSubsystem->RequestBRiskFileDialog(Del);
	 *
	 * @param OnFileSelectedCallback Callback executed when the dialog completes.
	 */
	UFUNCTION(BlueprintCallable, Category = "File Dialog")
	void RequestBRiskFileDialog(FOnBRiskFileSelectedDelegate OnFileSelectedCallback);

	UPROPERTY()
	FOnFileSelectedDelegate OnFileSelected;

	/** Dedicated B-Risk file-selected callback. */
	UPROPERTY()
	FOnBRiskFileSelectedDelegate OnBRiskFileSelected;

	/** Delegate invoked when the dialog fails to open or encounters an error. */
	UPROPERTY()
	FOnDialogErrorDelegate OnDialogError;

	/**
	 * Report a dialog error - logs the error and fires the OnDialogError delegate.
	 * @param ErrorTitle Short title describing the error.
	 * @param ErrorMessage Detailed error message.
	 */
	void ReportDialogError(const FString& ErrorTitle, const FString& ErrorMessage);

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
		MeshFile,
		BRiskFile    ///< B-Risk SMV manifest (.smv)
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

	/**
	 * Resolve the initial directory for the file dialog.
	 * @return Absolute path used to seed the file picker.
	 */
	FString ResolveInitialDialogDirectory() const;

	/**
	 * Cache the last directory chosen by the user.
	 * @param SelectedPath File path selected by the dialog.
	 */
	void UpdateLastDialogDirectory(const FString& SelectedPath);

private:
	UPROPERTY()
	bool bSelectionInProgress;

	UPROPERTY()
	FTimerHandle PollTimerHandle;

	EDialogType ActiveDialogType;

	FString LastDialogDirectory;

#if PLATFORM_WINDOWS || PLATFORM_LINUX
	struct FNativeFileDialogState
	{
		FNativeFileDialogState();
		~FNativeFileDialogState();

		void* ActiveDialogStorage = nullptr;
	};
	TUniquePtr<FNativeFileDialogState> NativeDialogState;
#endif
};
