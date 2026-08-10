// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "EnumsAndStructs/ScalabilityEnums.h"
#include "UserProjectSettings.generated.h"

/**
 * 
 */
UCLASS(config=ProjectUserSettings, ProjectUserConfig, Blueprintable)
class MOBIUSCORE_API UUserProjectSettings : public UGameUserSettings
{
	GENERATED_BODY()
public:
	UUserProjectSettings(const FObjectInitializer& ObjectInitializer);

	/**
	 * Persist all Config properties through UGameUserSettings::SaveSettings.
	 *
	 * Must be called on the engine's GameUserSettings singleton (GEngine->GetGameUserSettings(), which is
	 * a UUserProjectSettings — see GameUserSettingsClassName in DefaultEngine.ini). Called on any other
	 * instance it logs an error and does nothing, because saving would overwrite the shared
	 * GameUserSettings.ini section with that instance's defaults.
	 */
	UFUNCTION(BlueprintCallable, Category="UserSettings")
	void SaveMobiusSettings();

	/** Load all Config properties through UGameUserSettings::LoadSettings. Same singleton requirement as
	 *  SaveMobiusSettings. */
	void LoadMobiusSettings();

	/**
	 * Deprecated alias kept for existing Blueprint callers. Shadowed the non-virtual
	 * UObject::SaveConfig, so C++ callers got different behavior depending on pointer type.
	 */
	UFUNCTION(BlueprintCallable, Category="UserSettings", meta=(DeprecatedFunction, DeprecationMessage="Use SaveMobiusSettings instead."))
	void SaveConfig() { SaveMobiusSettings(); }

	// There is deliberately no reset-to-defaults member on this class. The ResetConfig() that used to sit here
	// assigned four named fields and was never extended as five more UPROPERTY(Config) fields were added
	// (UIScaleFactor, bHasCompletedFirstRun, AcceptedLegalNoticeVersion, bWasWindowMaximized and the two
	// sim-cache flags), so it silently reset a shrinking fraction of the class. It had no callers in C++, no
	// UFUNCTION, and no reference in Content, so that drift was never observable.
	//
	// If a reset is ever wanted, call the inherited UGameUserSettings::SetToDefaults() followed by
	// ApplySettings(). Do NOT reintroduce a hand-maintained field list (same reason as SaveMobiusSettings),
	// and do not reach for a CPF_Config reflection walk either:
	//   - the walk necessarily covers the inherited engine config properties too (resolution, fullscreen mode,
	//     vsync, ScalabilityQuality, audio quality, cached benchmark results), so it IS a full settings reset,
	//     which is what SetToDefaults() already implements correctly; and
	//   - copying from GetDefault<UUserProjectSettings>() does not reliably yield compiled-in defaults, because
	//     the CDO of a config class is itself LoadConfig'd — from this class's own config file
	//     (config=ProjectUserSettings), not the GGameUserSettingsIni that SaveSettings() explicitly writes.
	//     A reset built on it could quietly restore the current values instead of the defaults.
	//
	// Whatever replaces it must also take the IsEngineUserSettingsObject() guard, since every instance of this
	// class shares the [/Script/MobiusCore.UserProjectSettings] section of GameUserSettings.ini.

private:
	/**
	 * True when `this` is the engine's GameUserSettings singleton — the only instance whose config may be
	 * read or written, since all instances of this class share one ini section. Logs an error naming
	 * CallingFunction when it is not.
	 *
	 * Replaces a former `UGameUserSettings* ProjectUserSettings` member. That member was only ever assigned
	 * `GEngine->GetGameUserSettings()` from inside LoadMobiusSettings — i.e. it pointed at `this` — so the
	 * "copy fields onto the other settings object" code it guarded was mirroring state between an object
	 * and itself. Do not add such a member back.
	 */
	bool IsEngineUserSettingsObject(const TCHAR* CallingFunction) const;

#pragma region PRIVATE_VARIABLES
	UPROPERTY(Config)
	bool bEnableMobiusLoggerAtStartup = true;
	
	UPROPERTY(Config)
	bool bDisplayMobiusLogWindowAtStartup = false;

	/** Persisted render performance tier override (task C2). Auto = detect from hardware at startup;
	 *  Low/Medium/High pin a tier so captures reproduce identically across machines. Serialized via
	 *  SaveSettings()/LoadSettings() (GameUserSettings ini), the same path as the logger flags above. */
	UPROPERTY(Config)
	TEnumAsByte<ERenderPerformanceTier> RenderPerformanceTierOverride = ERpt_Auto;

	/** User UI scale multiplier applied on top of the automatic DPI rule (UMobiusUIScalingRule) via
	 *  UUserInterfaceSettings::ApplicationScale — the engine multiplies it after every scaling rule.
	 *  Accessibility escape hatch for beta testers on unusual monitor setups. */
	UPROPERTY(Config)
	float UIScaleFactor = 1.0f;

	/** False until the first successful launch. While false, OnStart sizes the window to the current
	 *  monitor work area instead of the fixed ini default (a fixed 1280x720 physical window is unusably
	 *  small on a 400%-scaled 4K display). */
	UPROPERTY(Config)
	bool bHasCompletedFirstRun = false;

	/** Version of the mandatory legal notice accepted by this OS user. Zero means not accepted.
	 *  This is deliberately independent of bHasCompletedFirstRun: window sizing must never imply
	 *  acceptance of the notice. Raising the code-side version displays a revised notice once. */
	UPROPERTY(Config)
	int32 AcceptedLegalNoticeVersion = 0;

	/** OS "maximized" is a window state, not a resolution — the engine neither tracks nor restores it
	 *  (stored resolution stays at the last confirmed size). Captured at shutdown, re-applied at start,
	 *  so closing the app maximized reopens it maximized like a normal desktop application. */
	UPROPERTY(Config)
	bool bWasWindowMaximized = false;

	/** UI theme choice (design doc: light = turn 4b "Windows white", dark = 7a "AutoCAD dark").
	 *  Light is the product default. Serialized via SaveSettings()/LoadSettings() like the flags above. */
	UPROPERTY(Config)
	bool bUseLightUITheme = true;

	/** Persisted mirror of the console variable mobius.SimCache.WriteOnImport (S14 control 1: "cache
	 *  imported simulations"). The CVar remains the runtime authority — this exists only because a console
	 *  variable does not survive a restart. Pushed to the CVar once at startup and on every setter call,
	 *  never from a tick or timer (see PushBoolToConsoleVariable in the .cpp for why that matters).
	 *
	 *  This default MUST match the CVar's own default. If the two disagree, the startup push silently
	 *  changes shipped behaviour for every user who has never touched the setting. */
	UPROPERTY(Config)
	bool bCacheSimulationsOnImport = true;

	/** Persisted mirror of mobius.SimCache.FastReload (S14 control 2: "reuse cache on reopen"). Same
	 *  authority model and the same must-match-the-CVar-default constraint as above. */
	UPROPERTY(Config)
	bool bReuseSimulationCacheOnReopen = true;
#pragma endregion PRIVATE_VARIABLES
	
public:
#pragma region GETTERS_AND_SETTERS
	UFUNCTION(BlueprintCallable, Category="UserSettings")
	bool GetEnableMobiusLoggerAtStartup() const { return bEnableMobiusLoggerAtStartup; }
	
	UFUNCTION(BlueprintCallable, Category="UserSettings")
	void SetEnableMobiusLoggerAtStartup(bool bEnable) { bEnableMobiusLoggerAtStartup = bEnable; }
	
	UFUNCTION(BlueprintCallable, Category="UserSettings")
	bool GetDisplayMobiusLogWindowAtStartup() const { return bDisplayMobiusLogWindowAtStartup; }
	
	UFUNCTION(BlueprintCallable, Category="UserSettings")
	void SetDisplayMobiusLogWindowAtStartup(bool bEnable) { bDisplayMobiusLogWindowAtStartup = bEnable; }

	/** Get the persisted render tier override (task C2). */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Render Tier")
	TEnumAsByte<ERenderPerformanceTier> GetRenderPerformanceTierOverride() const { return RenderPerformanceTierOverride; }

	/** Set + persist the render tier override (task C2). Persists via SaveSettings() so the Config
	 *  property is serialized through the real UObject SaveConfig path. Does NOT itself apply the tier —
	 *  UPerformanceUtilSubsystem::SetRenderPerformanceTierOverride() persists AND applies. */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Render Tier")
	void SetRenderPerformanceTierOverride(TEnumAsByte<ERenderPerformanceTier> NewOverride);

	UFUNCTION(BlueprintCallable, Category="UserSettings|Display")
	float GetUIScaleFactor() const { return UIScaleFactor; }

	/** Set, apply (UUserInterfaceSettings::ApplicationScale) and persist the user UI scale multiplier.
	 *  Clamped to [0.5, 2.0]. Takes effect on the next Slate layout pass. */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Display")
	void SetUIScaleFactor(float NewScale);

	/** Push the persisted UIScaleFactor into UUserInterfaceSettings::ApplicationScale without saving.
	 *  Called once at startup (GameInstance::Init). */
	void ApplyUIScaleFactorToSlate() const;

	UFUNCTION(BlueprintCallable, Category="UserSettings|Display")
	bool GetUseLightUITheme() const { return bUseLightUITheme; }

	/** Set + persist the UI theme choice. Does NOT itself repaint the UI —
	 *  UUIThemeSubsystem::SetTheme() persists AND applies. */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Display")
	void SetUseLightUITheme(bool bLight);

	/** Persisted "cache imported simulations" preference (S14 control 1). */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Sim Cache")
	bool GetCacheSimulationsOnImport() const { return bCacheSimulationsOnImport; }

	/** Set the preference, push it to mobius.SimCache.WriteOnImport and persist (S14 control 1). Takes
	 *  effect on the NEXT import — it does not retro-cache an already-loaded dataset. */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Sim Cache")
	void SetCacheSimulationsOnImport(bool bEnable);

	/** Persisted "reuse cache on reopen" preference (S14 control 2). */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Sim Cache")
	bool GetReuseSimulationCacheOnReopen() const { return bReuseSimulationCacheOnReopen; }

	/** Set the preference, push it to mobius.SimCache.FastReload and persist (S14 control 2). Takes effect
	 *  on the next file open. */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Sim Cache")
	void SetReuseSimulationCacheOnReopen(bool bEnable);

	/** Push both persisted sim-cache preferences into their console variables WITHOUT saving. Called once
	 *  at startup (GameInstance::Init), mirroring ApplyUIScaleFactorToSlate. */
	void ApplySimCacheSettingsToCVars() const;

	UFUNCTION(BlueprintCallable, Category="UserSettings|Display")
	bool HasCompletedFirstRun() const { return bHasCompletedFirstRun; }

	void MarkFirstRunCompleted();

	/** True only when this OS user's saved settings accept the current packaged-app legal notice. */
	bool HasAcceptedCurrentLegalNotice() const;

	/** Records acceptance of the current notice and flushes it to GameUserSettings.ini immediately. */
	void AcceptCurrentLegalNotice();

	/** Capture the game window's maximized state (kept current via viewport-resize events; the
	 *  window is already gone by shutdown). Does not save by itself. */
	void CaptureWindowMaximizedState();

	/** Loaded persisted value. Snapshot this BEFORE resize-event tracking starts overwriting it. */
	bool WasWindowMaximizedAtLastShutdown() const { return bWasWindowMaximized; }

	/** Maximize the game window (windowed mode only). Used to restore last session's state. */
	void MaximizeGameWindow() const;

	/**
	 * Apply a resolution + window mode the safe way: bypasses the engine's command-line override path
	 * (DetermineGameWindowResolution), which clamps to the PRIMARY monitor and rewrites oversized saved
	 * resolutions — the source of the settings-corruption bug on dual-monitor rigs. Confirms and persists.
	 */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Display")
	void ApplyMobiusDisplaySettings(FIntPoint NewResolution, EWindowMode::Type NewWindowMode);

	/** Clamp a desired resolution to the work area of the monitor the game window is currently on
	 *  (the engine's own clamp only ever considers the primary monitor). */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Display")
	FIntPoint ClampResolutionToCurrentMonitor(FIntPoint DesiredResolution) const;

	/** Common 16:9/16:10 resolutions that fit the monitor the game window is currently on. */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Display")
	TArray<FIntPoint> GetSupportedResolutionsForCurrentMonitor() const;
#pragma endregion GETTERS_AND_SETTERS

public:
#pragma region RUNTIME_CONTROL
	/** Enable or disable the Mobius logger at runtime. Updates setting and notifies subsystem immediately. */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Logger")
	void EnableMobiusLogger(bool bEnable);

	/** Show or hide the Mobius log window at runtime. Updates setting and notifies subsystem immediately. */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Logger")
	void ShowMobiusLogWindow(bool bShow);

	/** Get current runtime state of logger (may differ from startup setting). */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Logger")
	bool IsMobiusLoggerEnabled() const;

	/** Get current runtime state of log window (may differ from startup setting). */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Logger")
	bool IsMobiusLogWindowVisible() const;
#pragma endregion RUNTIME_CONTROL

};
