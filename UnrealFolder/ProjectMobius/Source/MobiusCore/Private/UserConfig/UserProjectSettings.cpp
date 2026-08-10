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
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobiusUserSettings, Log, All);

namespace
{
	/** Console-variable names for the two persisted sim-cache preferences (S14). Held as strings rather
	 *  than as references to the TAutoConsoleVariable objects on purpose: those live in an anonymous
	 *  namespace inside ProjectMobius/Private/SimData/SimDiskCache.cpp, and MobiusCore sits BELOW
	 *  ProjectMobius in the module graph, so the symbols are unreachable from here by design. Lookup by
	 *  name is the sanctioned seam across that boundary, not a workaround for a missing include. */
	const TCHAR* const GSimCacheWriteOnImportCVarName = TEXT("mobius.SimCache.WriteOnImport");
	const TCHAR* const GSimCacheFastReloadCVarName    = TEXT("mobius.SimCache.FastReload");

	/**
	 * Push a bool into an int32 console variable found by name.
	 *
	 * Deliberately uses IConsoleVariable::Set's DEFAULT priority (ECVF_SetByCode) — the same priority the
	 * automation tests use when they set these CVars directly. Equal priority means last-writer-wins, so a
	 * test that sets a CVar after startup takes effect as it always did. Raising the priority here
	 * (SetByConsole) would make those test writes silently no-op; lowering it (SetByProjectSetting) would
	 * make the USER's toggle silently no-op for the rest of any session in which a test had run. Both
	 * failure modes are invisible at the call site, which is why the choice is spelled out.
	 *
	 * A missing CVar is a warning, not an error: MobiusCore must keep working when ProjectMobius is not
	 * loaded (tooling and MobiusCore-only targets), and in that case there is no cache to configure.
	 */
	void PushBoolToConsoleVariable(const TCHAR* CVarName, bool bValue)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName))
		{
			CVar->Set(bValue ? 1 : 0);
		}
		else
		{
			UE_LOG(LogMobiusUserSettings, Warning,
				TEXT("[SimCache] console variable '%s' not found; persisted preference not applied"), CVarName);
		}
	}
}


UUserProjectSettings::UUserProjectSettings(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
}

bool UUserProjectSettings::IsEngineUserSettingsObject(const TCHAR* CallingFunction) const
{
	// There is only ever ONE UUserProjectSettings that matters: the engine's GameUserSettings singleton.
	// DefaultEngine.ini sets GameUserSettingsClassName=/Script/MobiusCore.UserProjectSettings, so the object
	// UEngine::CreateGameUserSettings news up IS of this class, and every caller in the project reaches it
	// through GEngine->GetGameUserSettings(). This class therefore does not — and must not — mirror state
	// into a second object.
	//
	// The check exists because the inherited SaveSettings() writes to the SHARED
	// [/Script/MobiusCore.UserProjectSettings] section of GameUserSettings.ini. Calling it on a stray
	// instance (a NewObject, the CDO, a Blueprint-constructed copy) would overwrite every persisted
	// preference of the real settings object with that stray's constructor defaults — including
	// AcceptedLegalNoticeVersion, i.e. it would re-prompt the legal notice. Fail loudly instead of
	// corrupting the file.
	if (!GEngine)
	{
		// No engine means no singleton to compare against and no session worth persisting.
		UE_LOG(LogMobiusUserSettings, Error, TEXT("%s: GEngine is null; settings not touched"), CallingFunction);
		return false;
	}

	// GetGameUserSettings() creates the singleton on demand and cannot return null, so this is a plain
	// identity test, not a null guard.
	if (this != GEngine->GetGameUserSettings())
	{
		UE_LOG(LogMobiusUserSettings, Error,
			TEXT("%s: called on '%s', which is not the engine's GameUserSettings object. ")
			TEXT("Reach settings via GEngine->GetGameUserSettings() instead; refusing to touch %s."),
			CallingFunction, *GetPathName(), TEXT("GameUserSettings.ini"));
		return false;
	}

	return true;
}

void UUserProjectSettings::SaveMobiusSettings()
{
	if (!IsEngineUserSettingsObject(TEXT("UUserProjectSettings::SaveMobiusSettings")))
	{
		return;
	}

	// The inherited SaveSettings() serializes EVERY UPROPERTY(Config) on this class via
	// SaveConfig(CPF_Config, *GGameUserSettingsIni). That is the whole job.
	//
	// This function used to hold a hand-written fallback that copied two named fields onto the object
	// returned by GEngine->GetGameUserSettings() before saving it. That list could only ever go stale —
	// RenderPerformanceTierOverride, UIScaleFactor, bHasCompletedFirstRun, AcceptedLegalNoticeVersion,
	// bWasWindowMaximized, bUseLightUITheme and the two sim-cache flags were all added afterwards and none
	// were added to it. Do not reintroduce a field list here: adding a UPROPERTY(Config) must be sufficient.
	SaveSettings();
}

void UUserProjectSettings::LoadMobiusSettings()
{
	if (!IsEngineUserSettingsObject(TEXT("UUserProjectSettings::LoadMobiusSettings")))
	{
		return;
	}

	// Mirror of the above: LoadSettings() reads every UPROPERTY(Config) back out of GameUserSettings.ini.
	// The old "create one if GetGameUserSettings() returns null" branch here was unreachable (the getter
	// creates on demand) and, had it run, would have written and then dropped an object that was never
	// installed on GEngine.
	LoadSettings();
}

void UUserProjectSettings::SetUseLightUITheme(const bool bLight)
{
	bUseLightUITheme = bLight;

	// Persist via SaveSettings() -> UGameUserSettings::SaveSettings -> real UObject::SaveConfig, which
	// serializes ALL UPROPERTY(Config) fields (same reasoning as SetRenderPerformanceTierOverride below).
	SaveSettings();
}

void UUserProjectSettings::SetRenderPerformanceTierOverride(TEnumAsByte<ERenderPerformanceTier> NewOverride)
{
	RenderPerformanceTierOverride = NewOverride;

	// Persist via SaveSettings() -> UGameUserSettings::SaveSettings -> real UObject::SaveConfig, which
	// serializes ALL UPROPERTY(Config) fields. SaveMobiusSettings() now resolves to exactly this call, so
	// either is correct; the direct call keeps the setter independent of that wrapper.
	SaveSettings();
}

void UUserProjectSettings::SetUIScaleFactor(float NewScale)
{
	UIScaleFactor = FMath::Clamp(NewScale, 0.5f, 2.0f);
	ApplyUIScaleFactorToSlate();
	SaveSettings();
}

void UUserProjectSettings::SetCacheSimulationsOnImport(bool bEnable)
{
	bCacheSimulationsOnImport = bEnable;
	PushBoolToConsoleVariable(GSimCacheWriteOnImportCVarName, bEnable);
	SaveSettings();
}

void UUserProjectSettings::SetReuseSimulationCacheOnReopen(bool bEnable)
{
	bReuseSimulationCacheOnReopen = bEnable;
	PushBoolToConsoleVariable(GSimCacheFastReloadCVarName, bEnable);
	SaveSettings();
}

void UUserProjectSettings::ApplySimCacheSettingsToCVars() const
{
	// Startup push, called once from ProjectMobiusGameInstance::Init immediately after LoadMobiusSettings()
	// — the same place and for the same reason as ApplyUIScaleFactorToSlate. A console variable does not
	// persist across launches, so without this the saved preference would be honoured only in the session
	// that set it. Deliberately NOT on a tick or timer; see PushBoolToConsoleVariable for why.
	PushBoolToConsoleVariable(GSimCacheWriteOnImportCVarName, bCacheSimulationsOnImport);
	PushBoolToConsoleVariable(GSimCacheFastReloadCVarName, bReuseSimulationCacheOnReopen);
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

bool UUserProjectSettings::HasAcceptedCurrentLegalNotice() const
{
	// Increment this when the shown notice changes materially. Existing users will then be prompted once.
	constexpr int32 CurrentLegalNoticeVersion = 1;
	return AcceptedLegalNoticeVersion >= CurrentLegalNoticeVersion;
}

void UUserProjectSettings::AcceptCurrentLegalNotice()
{
	constexpr int32 CurrentLegalNoticeVersion = 1;
	AcceptedLegalNoticeVersion = CurrentLegalNoticeVersion;
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
