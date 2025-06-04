// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "QtFileOpenSubsystem.generated.h"

// Delegate for file selection callback
DECLARE_DYNAMIC_DELEGATE_FourParams(FOnFileSelectedDelegate, const FString&, AgentFilePath, const FString&, MeshFilePath,bool, bAgentSuccess, bool, bMeshSuccess);

/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UQtFileOpenSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	///TODO: only need to call different get functions and configure the qt app to set file get path
	UQtFileOpenSubsystem();

	/** Launch the Qt file dialog application */
	UFUNCTION(BlueprintCallable, Category = "File Dialog")
	void LaunchQtDialogApp();
    
	/** Request a file selection dialog, with callback when file is selected */
	UFUNCTION(Category = "File Dialog")
	void RequestFileDialogFromQt(FOnFileSelectedDelegate OnFileSelectedCallback, FString HttpAddress = TEXT("openMeshFile"));
    
	/** Manually poll for selected file (called automatically by timer) */
	UFUNCTION(BlueprintCallable, Category = "File Dialog")
	void PollSelectedFileFromQt();
    
	/** Request the Qt application to shut down */
	UFUNCTION(BlueprintCallable, Category = "File Dialog")
	void RequestQuitQtApp();
    
	/** Check if the Qt application is running */
	UFUNCTION(BlueprintPure, Category = "File Dialog")
	bool IsQtAppRunning();

	FOnFileSelectedDelegate OnFileSelected;
protected:
	virtual void BeginDestroy() override;

private:
	void OnFileDialogRequested(FHttpRequestPtr RequestPtr, FHttpResponsePtr Response, bool bSuccess);
	void OnFilePollComplete(FHttpRequestPtr RequestPtr, FHttpResponsePtr Response, bool bSuccess);

private:
	bool bSelectionInProgress;
	FTimerHandle PollTimerHandle;
    
	FProcHandle QtProcessHandle;
	uint32 QtProcessID;
};
