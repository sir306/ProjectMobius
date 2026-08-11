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
#include "TimerManager.h"
#include "UnrealClient.h"
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
	SimulationMeshFileName(TEXT("Click Browse to choose file")),
	BRiskSmvFilePath(TEXT("Click Browse to choose file")),
	BRiskSmvFileName(TEXT("Click Browse to choose file"))
{
}

void UProjectMobiusGameInstance::Init()
{
	const double InitStartSeconds = FPlatformTime::Seconds();

	// Mirror UUserProjectSettings' own defaults, so a missing settings object behaves like a fresh config
	// rather than silently turning startup logging off — the log is how the miss below gets noticed.
	bool bStartLoggerAtStartup = true;
	bool bShowLogWindowAtStartup = false;

	// Guarded the same way as the OnStart() acquisitions below: the cast returns null if
	// GameUserSettingsClassName in DefaultEngine.ini stops naming /Script/MobiusCore.UserProjectSettings,
	// and an unguarded dereference here is a crash on the first statement of Init. The rest of Init must
	// still run, so guard the settings-dependent block rather than returning.
	UUserProjectSettings* ProjectUserSettings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr);
	if (ProjectUserSettings)
	{
		ProjectUserSettings->LoadMobiusSettings();

		// Push the persisted user UI-scale multiplier into Slate (composes with UMobiusUIScalingRule).
		ProjectUserSettings->ApplyUIScaleFactorToSlate();

		// Push the persisted sim-cache preferences into their console variables (S14). Console variables do not
		// survive a restart, so without this the user's choice would apply only in the session that made it.
		ProjectUserSettings->ApplySimCacheSettingsToCVars();

		// log the custom config variables
		bStartLoggerAtStartup = ProjectUserSettings->GetEnableMobiusLoggerAtStartup();

		bShowLogWindowAtStartup = ProjectUserSettings->GetDisplayMobiusLogWindowAtStartup();
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("ProjectMobiusGameInstance::Init: game user settings are not a UUserProjectSettings ")
			TEXT("(GEngine %s). Check GameUserSettingsClassName in DefaultEngine.ini still names ")
			TEXT("/Script/MobiusCore.UserProjectSettings — no persisted Mobius setting is applied this session."),
			GEngine ? TEXT("valid") : TEXT("null"));
	}

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

void UProjectMobiusGameInstance::OnStart()
{
	Super::OnStart();

	// Standalone game only — PIE and editor windows are owned by the editor.
	if (GIsEditor)
	{
		return;
	}

	// Snapshot the persisted maximize flag BEFORE resize-event tracking starts overwriting it —
	// boot resize events fire while the window is still un-maximized and would clear it before the
	// restore timers below read it.
	bool bReopenMaximized = false;
	if (const UUserProjectSettings* LoadedSettings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr))
	{
		bReopenMaximized = LoadedSettings->WasWindowMaximizedAtLastShutdown();
	}

	// Track the game window's maximized state as it changes. A shutdown-time query cannot work:
	// by GameInstance::Shutdown the OS window is already gone (verified — flag stayed False when
	// the app was closed maximized). Maximize/restore always fire a viewport resize, so the flag
	// stays current for the shutdown save.
	ViewportResizedHandle = FViewport::ViewportResizedEvent.AddUObject(this, &UProjectMobiusGameInstance::HandleGameViewportResized);

	// Defer window sizing one tick: issued directly from OnStart, the resolution request races the
	// engine's own initial window sizing and loses — values persist to ini but the window stays at
	// the boot default (verified in automated test; window only picked the size up on the NEXT
	// launch). One tick later the viewport pipeline is settled and the resize applies live.
	GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UUserProjectSettings* ProjectUserSettings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr);
		if (!ProjectUserSettings)
		{
			return;
		}

		// First-run window sizing: the ini default (1280x720 physical) is unusably small on
		// high-DPI monitors (e.g. 4K at 400% OS scaling). Size the window to ~85% of the current
		// monitor's work area once, then let the user's own choice persist thereafter.
		if (!ProjectUserSettings->HasCompletedFirstRun())
		{
			const FIntPoint WorkArea = ProjectUserSettings->ClampResolutionToCurrentMonitor(FIntPoint(MAX_int32, MAX_int32));
			const FIntPoint FirstRunResolution(
				FMath::RoundToInt(WorkArea.X * 0.85f),
				FMath::RoundToInt(WorkArea.Y * 0.85f));

			ProjectUserSettings->ApplyMobiusDisplaySettings(FirstRunResolution, EWindowMode::Windowed);
			ProjectUserSettings->MarkFirstRunCompleted();
		}
	}));

	// Reopen maximized if the app was closed maximized (OS window state, invisible to resolution
	// settings). Deferred past boot settle: the engine's startup resolution-apply lands during the
	// first ticks and un-maximizes a window maximized too early (verified — next-tick restore was
	// undone). Retry once in case a slow first frame pushes that even later.
	if (bReopenMaximized)
	{
		FTimerHandle RestoreMaximizedHandle;
		GetTimerManager().SetTimer(RestoreMaximizedHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (const UUserProjectSettings* Settings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr))
			{
				Settings->MaximizeGameWindow();
			}

			FTimerHandle RetryHandle;
			GetTimerManager().SetTimer(RetryHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (const UUserProjectSettings* RetrySettings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr))
				{
					RetrySettings->MaximizeGameWindow();
				}
			}), 1.25f, false);
		}), 0.75f, false);
	}

}

void UProjectMobiusGameInstance::HandleGameViewportResized(FViewport* Viewport, uint32 /*Unused*/)
{
	if (UUserProjectSettings* ProjectUserSettings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr))
	{
		ProjectUserSettings->CaptureWindowMaximizedState();
	}
}

void UProjectMobiusGameInstance::Shutdown()
{
	if (ViewportResizedHandle.IsValid())
	{
		FViewport::ViewportResizedEvent.Remove(ViewportResizedHandle);
		ViewportResizedHandle.Reset();
	}

	// ensure user settings are saved on shutdown (maximized flag was kept current by the
	// viewport-resize tracking above — the window itself is already destroyed at this point)
	if (UUserProjectSettings* ProjectUserSettings = Cast<UUserProjectSettings>(GEngine->GetGameUserSettings()))
	{
		ProjectUserSettings->SaveMobiusSettings();
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
	PedestrianDataFilePath = NewPedestrianDataFilePath;
	OnPedestrianVectorFileChanged.Broadcast(NewPedestrianDataFilePath);
	OnPedestrianVectorFileUpdated.Broadcast();
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

void UProjectMobiusGameInstance::SetBRiskSmvFilePath(const FString& NewBRiskSmvFilePath)
{
	if (BRiskSmvFilePath != NewBRiskSmvFilePath)
	{
		BRiskSmvFilePath = NewBRiskSmvFilePath;

		// Derive the display name from the full path.
		BRiskSmvFileName = FPaths::GetCleanFilename(NewBRiskSmvFilePath);

		// Notify UBRiskDataSubsystem (and any other listeners) so they auto-load.
		OnBRiskFileChanged.Broadcast();
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
