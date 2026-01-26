// Fill out your copyright notice in the Description page of Project Settings.


#include "UserConfig/UserProjectSettings.h"
#include "Subsystems/MobiusCustomLoggerSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"


UUserProjectSettings::UUserProjectSettings(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
}

void UUserProjectSettings::SaveConfig()
{
	if (ProjectUserSettings != nullptr)
	{
		ProjectUserSettings->SaveSettings();
	}
	else if (GEngine)
	{
		auto TempSettings = Cast<UUserProjectSettings>(GEngine->GetGameUserSettings());
		if (TempSettings != nullptr)
		{
			TempSettings->bEnableMobiusLoggerAtStartup = bEnableMobiusLoggerAtStartup;
			TempSettings->bDisplayMobiusLogWindowAtStartup = bDisplayMobiusLogWindowAtStartup;
			TempSettings->SaveSettings();
		}
		else
		{
			// Use MobiusUserFeedbackSubsystem for error reporting
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World())
				{
					World = Context.World();
					break;
				}
			}

			if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(World))
			{
				Feedback->ReportError(
					FText::FromString("Settings Error"),
					FText::FromString("Failed to Save Settings"),
					FText::FromString("Could not access game user settings."),
					FText::FromString("UserProjectSettings::SaveConfig"),
					EMobiusErrorSeverity::Warning,
					true
				);
			}
		}
	}
	else
	{
		// GEngine not available - cannot report error properly
		UE_LOG(LogTemp, Error, TEXT("UserProjectSettings::SaveConfig - GEngine is null"));
	}
}

void UUserProjectSettings::LoadConfig()
{
	// check that we can get the GEngine
	if (GEngine)
	{
		// have we got a game user setting if not we need to make one
		if (GEngine->GetGameUserSettings() == nullptr)
		{
			UUserProjectSettings* NewSettings = NewObject<UUserProjectSettings>(GEngine, UUserProjectSettings::StaticClass());
			NewSettings->SaveConfig();
		}
		ProjectUserSettings = Cast<UUserProjectSettings>(GEngine->GetGameUserSettings());
		if (ProjectUserSettings != nullptr)
		{
			ProjectUserSettings->LoadSettings();
		}
		else
		{
			// Use MobiusUserFeedbackSubsystem for error reporting
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World())
				{
					World = Context.World();
					break;
				}
			}

			if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(World))
			{
				Feedback->ReportError(
					FText::FromString("Settings Error"),
					FText::FromString("Failed to Load Settings"),
					FText::FromString("Could not access game user settings."),
					FText::FromString("UserProjectSettings::LoadConfig"),
					EMobiusErrorSeverity::Warning,
					true
				);
			}
		}
	}
	else
	{
		// GEngine not available - cannot report error properly
		UE_LOG(LogTemp, Error, TEXT("UserProjectSettings::LoadConfig - GEngine is null"));
	}
}

void UUserProjectSettings::ResetConfig()
{
	bEnableMobiusLoggerAtStartup = true;
	bDisplayMobiusLogWindowAtStartup = false;
}

void UUserProjectSettings::EnableMobiusLogger(bool bEnable)
{
	// Update the startup setting
	bEnableMobiusLoggerAtStartup = bEnable;

	// Notify the logger subsystem immediately
	if (UMobiusCustomLoggerSubsystem* Logger = GEngine ?
		GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr)
	{
		Logger->SetLoggingEnabled(bEnable);

		if (bEnable)
		{
			Logger->EnqueueLogMessage(TEXT("UserProjectSettings: Mobius logger enabled via runtime control"));
		}
	}

	// If disabling logger, also close the log window
	if (!bEnable)
	{
		ShowMobiusLogWindow(false);
	}
}

void UUserProjectSettings::ShowMobiusLogWindow(bool bShow)
{
	// Update the startup setting
	bDisplayMobiusLogWindowAtStartup = bShow;

	// Get the world context from GEngine
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				World = Context.World();
				break;
			}
		}
	}

	// Notify the user feedback subsystem immediately
	if (UMobiusUserFeedbackSubsystem* Feedback =
		UMobiusUserFeedbackSubsystem::Get(World))
	{
		if (bShow)
		{
			// Cannot show window if logger is disabled
			if (!IsMobiusLoggerEnabled())
			{
				// Log warning
				if (UMobiusCustomLoggerSubsystem* Logger = GEngine ?
					GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr)
				{
					Logger->EnqueueLogMessage(TEXT("UserProjectSettings: Cannot show log window - logger is disabled"));
				}
				return;
			}

			Feedback->RequestLogWindowOpen();
		}
		else
		{
			Feedback->RequestLogWindowClose();
		}
	}
}

bool UUserProjectSettings::IsMobiusLoggerEnabled() const
{
	if (UMobiusCustomLoggerSubsystem* Logger = GEngine ?
		GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr)
	{
		return Logger->IsLoggingEnabled();
	}
	return false;
}

bool UUserProjectSettings::IsMobiusLogWindowVisible() const
{
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				World = Context.World();
				break;
			}
		}
	}

	if (UMobiusUserFeedbackSubsystem* Feedback =
		UMobiusUserFeedbackSubsystem::Get(World))
	{
		return Feedback->IsLogWindowOpen();
	}
	return false;
}
