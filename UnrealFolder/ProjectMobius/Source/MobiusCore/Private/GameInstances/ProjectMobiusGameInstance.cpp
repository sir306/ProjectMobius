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

#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "HAL/PlatformTime.h"
#include "Subsystems/MobiusCustomLoggerSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Subsystems/WebSocketSubsystem.h"
#include "UserConfig/UserProjectSettings.h"

UProjectMobiusGameInstance::UProjectMobiusGameInstance():
	Super(),
	// TODO: Set default values for these variables -- talk to peter if particular values are needed that
	// could be set here as a default simulation
	TimeDilationScaleFactor(1.0f),
	MeshScale(1.0f),
	SimulationMovementScale(1.0f),
	// Set to 0.5f to ensure that the simulation runs at half speed for DEBUG
	PedestrianDataFilePath(TEXT("Click Browse to choose file")),
	PedestrianDataFileName(TEXT("Click Browse to choose file")),
	SimulationMeshFilePath(TEXT("Click Browse to choose file")),
	SimulationMeshFileName(TEXT("Click Browse to choose file"))
{
}

void UProjectMobiusGameInstance::Init()
{
	const double InitStartSeconds = FPlatformTime::Seconds();

	UUserProjectSettings* ProjectUserSettings = Cast<UUserProjectSettings>(GEngine->GetGameUserSettings());
	ProjectUserSettings->LoadConfig();

	// log the custom config variables
	bool bStartLoggerAtStartup = ProjectUserSettings->GetEnableMobiusLoggerAtStartup();

	bool bShowLogWindowAtStartup = ProjectUserSettings->GetDisplayMobiusLogWindowAtStartup();

	UE_LOG(LogTemp, Warning, TEXT("ProjectMobiusGameInstance::Init - bEnableMobiusLoggerAtStartup: %s"), bStartLoggerAtStartup ? TEXT("true") : TEXT("false"));
	UE_LOG(LogTemp, Log, TEXT("ProjectMobiusGameInstance::Init - bDisplayMobiusLogWindowAtStartup: %s"), bShowLogWindowAtStartup ? TEXT("true") : TEXT("false"));

	UMobiusCustomLoggerSubsystem* StartupLogger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
	if (StartupLogger)
	{
		// Control logging based on user settings
		StartupLogger->SetLoggingEnabled(bStartLoggerAtStartup);

		if (bStartLoggerAtStartup)
		{
			StartupLogger->EnqueueLogMessage(TEXT("ProjectMobiusGameInstance::Init begin"));
		}
	}

	Super::Init();

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("t.IdleWhenNotForeground"));
	if (CVar)
	{
		CVar->Set(0, ECVF_SetByCode);
	}
	CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TSR.ShadingRejection.Flickering"));
	if (CVar)
	{
		CVar->Set(0, ECVF_SetByCode);
	}
	CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mass.FullyParallel"));
	if (CVar)
	{
		CVar->Set(1, ECVF_SetByCode);
	}

	if (StartupLogger && bStartLoggerAtStartup)
	{
		const double DurationMs = (FPlatformTime::Seconds() - InitStartSeconds) * 1000.0;
		StartupLogger->EnqueueTimedMessage(TEXT("ProjectMobiusGameInstance::Init"), DurationMs);
	}

	// Initialize the log window display based on settings
	// Display cannot be enabled without logging enabled
	if (bShowLogWindowAtStartup && bStartLoggerAtStartup)
	{
		OpenLogWindow();
	}
}

void UProjectMobiusGameInstance::Shutdown()
{
	// ensure user settings are saved on shutdown
	if (UUserProjectSettings* ProjectUserSettings = Cast<UUserProjectSettings>(GEngine->GetGameUserSettings()))
	{
		ProjectUserSettings->SaveConfig();
	}
	Super::Shutdown();
}

void UProjectMobiusGameInstance::ReportError(const FText& TitleBarText, const FText& ErrorTitle,
	const FText& ErrorMessage, const FText& ErrorLocation, EMobiusErrorSeverity Severity, bool bShowPrompt)
{
	if (UMobiusUserFeedbackSubsystem* FeedbackSubsystem = GetSubsystem<UMobiusUserFeedbackSubsystem>())
	{
		FeedbackSubsystem->ReportError(TitleBarText, ErrorTitle, ErrorMessage, ErrorLocation, Severity, bShowPrompt);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("%s: %s"), *ErrorTitle.ToString(), *ErrorMessage.ToString());
	}
}

void UProjectMobiusGameInstance::SetErrorPromptsEnabled(bool bEnabled)
{
	if (UMobiusUserFeedbackSubsystem* FeedbackSubsystem = GetSubsystem<UMobiusUserFeedbackSubsystem>())
	{
		FeedbackSubsystem->SetErrorPromptsEnabled(bEnabled);
	}
}

void UProjectMobiusGameInstance::OpenLogWindow()
{
	// Can only display log window if logging is enabled
	UMobiusCustomLoggerSubsystem* Logger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
	if (!Logger || !Logger->IsLoggingEnabled())
	{
		UE_LOG(LogTemp, Warning, TEXT("Cannot open log window: Mobius logger is not enabled"));
		return;
	}

	if (UMobiusUserFeedbackSubsystem* FeedbackSubsystem = GetSubsystem<UMobiusUserFeedbackSubsystem>())
	{
		FeedbackSubsystem->RequestLogWindowOpen();
	}
}

void UProjectMobiusGameInstance::CloseLogWindow()
{
	if (UMobiusUserFeedbackSubsystem* FeedbackSubsystem = GetSubsystem<UMobiusUserFeedbackSubsystem>())
	{
		FeedbackSubsystem->RequestLogWindowClose();
	}
}

void UProjectMobiusGameInstance::SetLogWindowEnabled(bool bEnabled)
{
	// If trying to enable the window, verify that logging is enabled
	if (bEnabled)
	{
		UMobiusCustomLoggerSubsystem* Logger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
		if (!Logger || !Logger->IsLoggingEnabled())
		{
			UE_LOG(LogTemp, Warning, TEXT("Cannot enable log window: Mobius logger is not enabled"));
			return;
		}
	}

	if (UMobiusUserFeedbackSubsystem* FeedbackSubsystem = GetSubsystem<UMobiusUserFeedbackSubsystem>())
	{
		FeedbackSubsystem->SetLogWindowEnabled(bEnabled);
	}
}

void UProjectMobiusGameInstance::SetPedestrianDataFilePath(const FString& NewPedestrianDataFilePath)
{
	// We only need to update and broadcast if the file path has changed
	if(PedestrianDataFileName != NewPedestrianDataFilePath)
	{
		PedestrianDataFilePath = NewPedestrianDataFilePath;
		OnPedestrianVectorFileChanged.Broadcast(NewPedestrianDataFilePath); // Broadcast the new pedestrian vector file
		OnPedestrianVectorFileUpdated.Broadcast();
	}
}

void UProjectMobiusGameInstance::SetPedestrianDataFileName(const FString& NewPedestrianDataFileName)
{
	PedestrianDataFileName = NewPedestrianDataFileName;
}

void UProjectMobiusGameInstance::SetSimulationMeshFilePath(const FString& NewSimulationMeshFilePath)
{
	if(SimulationMeshFilePath != NewSimulationMeshFilePath)
	{
		SimulationMeshFilePath = NewSimulationMeshFilePath;

		// Update SimulationMeshFileName
		SimulationMeshFileName = FPaths::GetCleanFilename(NewSimulationMeshFilePath);

		// Broadcast that the mesh file has changed		
		OnMeshFileChanged.Broadcast();
	}
}

void UProjectMobiusGameInstance::SetSimulationMeshFileName(const FString& NewSimulationMeshFileName)
{
	SimulationMeshFileName = NewSimulationMeshFileName;
}

void UProjectMobiusGameInstance::SetTimeDilationScaleFactor(const float NewTimeDilationScaleFactor)
{
	TimeDilationScaleFactor = NewTimeDilationScaleFactor;

	// Notify all listeners that the time dilation scale factor has changed
	OnTimeDilationScaleFactorChanged.Broadcast();
}

void UProjectMobiusGameInstance::SetMeshScale(const float NewMeshScale)
{
}

void UProjectMobiusGameInstance::SetSimulationMovementScale(const float NewSimulationMovementScale)
{
}

void UProjectMobiusGameInstance::SetGlobalScale(const float NewGlobalScale)
{
}

void UProjectMobiusGameInstance::SetDataLoadingState(const bool bNewLoadingState)
{
	if(bNewLoadingState != bIsDataBeingLoaded)
	{
		bIsDataBeingLoaded = bNewLoadingState;
		OnDataLoading.Broadcast(bIsDataBeingLoaded);
	}
}

void UProjectMobiusGameInstance::SetMobiusLoggerEnabled(bool bEnabled)
{
	UMobiusCustomLoggerSubsystem* Logger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
	if (Logger)
	{
		Logger->SetLoggingEnabled(bEnabled);

		if (bEnabled)
		{
			UE_LOG(LogTemp, Log, TEXT("Mobius logger enabled"));
		}
		else
		{
			// Disable the log window display when disabling the logger
			SetLogWindowEnabled(false);
			UE_LOG(LogTemp, Log, TEXT("Mobius logger disabled"));
		}
	}
}

bool UProjectMobiusGameInstance::IsMobiusLoggerEnabled() const
{
	const UMobiusCustomLoggerSubsystem* Logger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
	return Logger ? Logger->IsLoggingEnabled() : false;
}
