// Fill out your copyright notice in the Description page of Project Settings.


#include "UserConfig/UserProjectSettings.h"
#include "Subsystems/MobiusCustomLoggerSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"


UUserProjectSettings::UUserProjectSettings(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
}

void UUserProjectSettings::SaveMobiusSettings()
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
					FText::FromString("UserProjectSettings::SaveMobiusSettings"),
					EMobiusErrorSeverity::Warning,
					true
				);
			}
		}
	}
	else
	{
		// GEngine not available - cannot report error properly
		UE_LOG(LogTemp, Error, TEXT("UserProjectSettings::SaveMobiusSettings - GEngine is null"));
	}
}

void UUserProjectSettings::LoadMobiusSettings()
{
	// check that we can get the GEngine
	if (GEngine)
	{
		// have we got a game user setting if not we need to make one
		if (GEngine->GetGameUserSettings() == nullptr)
		{
			UUserProjectSettings* NewSettings = NewObject<UUserProjectSettings>(GEngine, UUserProjectSettings::StaticClass());
			NewSettings->SaveMobiusSettings();
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
					FText::FromString("UserProjectSettings::LoadMobiusSettings"),
					EMobiusErrorSeverity::Warning,
					true
				);
			}
		}
	}
	else
	{
		// GEngine not available - cannot report error properly
		UE_LOG(LogTemp, Error, TEXT("UserProjectSettings::LoadMobiusSettings - GEngine is null"));
	}
}

void UUserProjectSettings::ResetConfig()
{
	bEnableMobiusLoggerAtStartup = true;
	bDisplayMobiusLogWindowAtStartup = false;
	RenderPerformanceTierOverride = ERpt_Auto;
}

void UUserProjectSettings::SetRenderPerformanceTierOverride(TEnumAsByte<ERenderPerformanceTier> NewOverride)
{
	RenderPerformanceTierOverride = NewOverride;

	// Persist via SaveSettings() -> UGameUserSettings::SaveSettings -> real UObject::SaveConfig, which
	// serializes ALL UPROPERTY(Config) fields. (The custom no-arg SaveConfig() above only hand-copies the
	// two logger flags through its GEngine fallback, so it would NOT persist this field.)
	SaveSettings();
}

void UUserProjectSettings::SetUIScaleFactor(float NewScale)
{
	UIScaleFactor = FMath::Clamp(NewScale, 0.5f, 2.0f);
	ApplyUIScaleFactorToSlate();
	SaveSettings();
}

void UUserProjectSettings::ApplyUIScaleFactorToSlate() const
{
	// The engine multiplies ApplicationScale on top of whatever the active scaling rule returns
	// (UUserInterfaceSettings::GetDPIScaleBasedOnSize), so this composes with UMobiusUIScalingRule.
	GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale = FMath::Clamp(UIScaleFactor, 0.5f, 2.0f);

	// ApplicationScale is read on every scale query, but nothing forces a re-layout if the viewport
	// size has not changed — invalidate so the new scale shows immediately rather than on next resize.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().InvalidateAllWidgets(false);
	}
}

void UUserProjectSettings::MarkFirstRunCompleted()
{
	bHasCompletedFirstRun = true;
	SaveSettings();
}

void UUserProjectSettings::CaptureWindowMaximizedState()
{
	if (FSlateApplication::IsInitialized() && GEngine && GEngine->GameViewport)
	{
		if (const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
		{
			bWasWindowMaximized = Window->IsWindowMaximized();
		}
	}
}

void UUserProjectSettings::MaximizeGameWindow() const
{
	if (GetFullscreenMode() == EWindowMode::Windowed
		&& FSlateApplication::IsInitialized() && GEngine && GEngine->GameViewport)
	{
		if (const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
		{
			if (!Window->IsWindowMaximized())
			{
				Window->Maximize();
			}
		}
	}
}

namespace
{
	/** Work area (physical px) of the monitor the game window currently occupies; falls back to the
	 *  primary display work area when no game window exists yet. */
	FSlateRect GetCurrentMonitorWorkArea()
	{
		if (FSlateApplication::IsInitialized())
		{
			if (GEngine && GEngine->GameViewport)
			{
				if (const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
				{
					return FSlateApplication::Get().GetWorkArea(Window->GetRectInScreen());
				}
			}

			FDisplayMetrics Metrics;
			FSlateApplication::Get().GetInitialDisplayMetrics(Metrics);
			return FSlateRect(
				Metrics.PrimaryDisplayWorkAreaRect.Left, Metrics.PrimaryDisplayWorkAreaRect.Top,
				Metrics.PrimaryDisplayWorkAreaRect.Right, Metrics.PrimaryDisplayWorkAreaRect.Bottom);
		}

		return FSlateRect(0.f, 0.f, 1280.f, 720.f);
	}
}

void UUserProjectSettings::ApplyMobiusDisplaySettings(FIntPoint NewResolution, EWindowMode::Type NewWindowMode)
{
	// Windowed sizes must fit the monitor; fullscreen-type modes take the requested size as-is.
	const FIntPoint Resolution = (NewWindowMode == EWindowMode::Windowed)
		? ClampResolutionToCurrentMonitor(NewResolution)
		: NewResolution;

	SetScreenResolution(Resolution);
	SetFullscreenMode(NewWindowMode);

	// bCheckForCommandLineOverrides=false is the load-bearing part: true routes through
	// UGameEngine::DetermineGameWindowResolution, which clamps to the PRIMARY monitor and replaces
	// oversized saved resolutions with a canned "convenient" one — then the save below would persist
	// that clobbered value. That was the settings-corruption ratchet on dual-monitor rigs.
	ApplySettings(false);
	ConfirmVideoMode();

	// The r.SetRes request above does not reliably reshape the live window in this app (verified:
	// values persisted and restored on the NEXT launch, but the running window never moved — the
	// long-standing "resolution set but window unchanged" bug). Resize the Slate window directly;
	// client size and resolution are both physical pixels here.
	if (NewWindowMode == EWindowMode::Windowed && GEngine && GEngine->GameViewport)
	{
		if (const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
		{
			if (!Window->IsWindowMaximized())
			{
				Window->Resize(FVector2D(Resolution.X, Resolution.Y));
			}
		}
	}

	SaveSettings();
}

FIntPoint UUserProjectSettings::ClampResolutionToCurrentMonitor(FIntPoint DesiredResolution) const
{
	const FSlateRect WorkArea = GetCurrentMonitorWorkArea();
	const FIntPoint WorkSize(
		FMath::TruncToInt(WorkArea.GetSize().X),
		FMath::TruncToInt(WorkArea.GetSize().Y));

	return FIntPoint(
		FMath::Clamp(DesiredResolution.X, 640, FMath::Max(640, WorkSize.X)),
		FMath::Clamp(DesiredResolution.Y, 480, FMath::Max(480, WorkSize.Y)));
}

TArray<FIntPoint> UUserProjectSettings::GetSupportedResolutionsForCurrentMonitor() const
{
	static const FIntPoint CommonResolutions[] = {
		{1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {1920, 1200},
		{2560, 1440}, {2560, 1600}, {3440, 1440}, {3840, 2160}};

	const FSlateRect WorkArea = GetCurrentMonitorWorkArea();
	const FVector2D WorkSize = WorkArea.GetSize();

	TArray<FIntPoint> Result;
	for (const FIntPoint& Candidate : CommonResolutions)
	{
		if (Candidate.X <= WorkSize.X && Candidate.Y <= WorkSize.Y)
		{
			Result.Add(Candidate);
		}
	}

	// Always offer the full work area itself (covers monitors between/above the canned sizes).
	const FIntPoint WorkAreaResolution(FMath::TruncToInt(WorkSize.X), FMath::TruncToInt(WorkSize.Y));
	Result.AddUnique(WorkAreaResolution);

	return Result;
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
