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
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "QtFileOpenSubsystem.generated.h"

// Delegate for file selection callback
DECLARE_DYNAMIC_DELEGATE_FourParams(FOnFileSelectedDelegate, const FString&, AgentFilePath, const FString&, MeshFilePath,bool, bAgentSuccess, bool, bMeshSuccess);

/**
 * 
 */
UCLASS()
class MOBIUSCORE_API UQtFileOpenSubsystem : public UWorldSubsystem
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
