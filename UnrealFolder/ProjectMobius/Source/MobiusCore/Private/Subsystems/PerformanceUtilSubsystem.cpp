// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/PerformanceUtilSubsystem.h"

#include "EnumsAndStructs/AvatarScalabilityEnum.h"
#include "EnumsAndStructs/ScalabilityEnums.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMemory.h"
#include "RHI.h"
#include "Subsystems/MobiusCustomLoggerSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UserConfig/UserProjectSettings.h"

// Heavy render-feature cvar tables per render tier (task C1). Each entry documents what it gates and
// the per-tier value. Returned as "name=value" strings for ApplyConsoleCommands (-> ECVF_SetByCode).
//
// HIGH mirrors DefaultEngine.ini [/Script/Engine.RendererSettings] exactly, so applying it on capable
// hardware is a value-level no-op (only the cvar priority is bumped). LOW/MEDIUM cut the per-frame GPU
// cost. NOTE: r.RayTracing itself is ECVF_ReadOnly (startup-gated) — a runtime Set is ignored by the
// engine, so it is intentionally omitted. The effective runtime ray-tracing saving comes from disabling
// the RT *workloads*: r.Lumen.HardwareRayTracing (-> software Lumen) and r.RayTracing.Shadows. To strip
// r.RayTracing entirely a startup path (device profile / [ConsoleVariables] / command line) is required.
static TArray<FString> GetRenderTierConsoleCommands(ERenderPerformanceTier Tier)
{
	switch (Tier)
	{
	case ERpt_Low:
		// Aggressive: software Lumen kept (GI on) so heterogeneous-volume smoke stays lit; everything
		// else trimmed. (Flip r.DynamicGlobalIlluminationMethod to 0 for an even cheaper, flatter look —
		// pending in-editor smoke A/B, see PRD C1.)
		return {
			TEXT("r.Lumen.HardwareRayTracing=0"),        // HW Lumen RT -> software Lumen (largest GPU saving)
			TEXT("r.RayTracing.Shadows=0"),              // ray-traced shadows off -> rasterized CSM
			TEXT("r.DynamicGlobalIlluminationMethod=1"), // keep (software) Lumen GI for smoke lighting
			TEXT("r.ReflectionMethod=2"),                // SSR (cheap) instead of Lumen reflections
			TEXT("r.AntiAliasingMethod=2"),              // TAA (cheaper than TSR)
			TEXT("r.MSAACount=1"),                       // MSAA is inert in deferred; pin to 1
			TEXT("r.DistanceFieldAO=0"),                 // distance-field AO off
		};
	case ERpt_Medium:
		// Balanced: drop hardware ray tracing (software Lumen GI + reflections kept), AA stays TSR.
		return {
			TEXT("r.Lumen.HardwareRayTracing=0"),        // software Lumen (keeps GI/reflections, drops RT cost)
			TEXT("r.RayTracing.Shadows=0"),              // rasterized shadows
			TEXT("r.DynamicGlobalIlluminationMethod=1"), // Lumen GI
			TEXT("r.ReflectionMethod=1"),                // Lumen reflections
			TEXT("r.AntiAliasingMethod=4"),              // TSR (as shipped)
			TEXT("r.MSAACount=1"),
			TEXT("r.DistanceFieldAO=0"),
		};
	case ERpt_High:
	default:
		// Mirrors DefaultEngine.ini (capable-HW shipping values) -> value-level no-op on capable HW.
		return {
			TEXT("r.Lumen.HardwareRayTracing=1"),        // ini: r.Lumen.HardwareRayTracing=True
			TEXT("r.RayTracing.Shadows=1"),              // ini: r.RayTracing.Shadows=True
			TEXT("r.DynamicGlobalIlluminationMethod=1"), // ini: r.DynamicGlobalIlluminationMethod=1
			TEXT("r.ReflectionMethod=1"),                // ini: r.ReflectionMethod=1
			TEXT("r.AntiAliasingMethod=4"),              // ini: r.AntiAliasingMethod=4 (TSR)
			TEXT("r.MSAACount=4"),                       // ini: r.MSAACount=4
			TEXT("r.DistanceFieldAO=1"),                 // engine default (not in ini) -> true no-op on High
		};
	}
}

UPerformanceUtilSubsystem::UPerformanceUtilSubsystem()
{
	GlobalScalabilitySetting = EGss_Epic;
	CurrentAvatarModelType = EPss_High;
	// Default to High so first construction / capable hardware matches DefaultEngine.ini until
	// InitializeRenderPerformanceTier() detects or an override pins a lower tier (tasks C1/C2).
	CurrentRenderTier = ERpt_High;
}

UPerformanceUtilSubsystem::~UPerformanceUtilSubsystem()
{
	
}

void UPerformanceUtilSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	const double InitStart = FPlatformTime::Seconds();
	UMobiusCustomLoggerSubsystem* StartupLogger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
	if (StartupLogger)
	{
		StartupLogger->EnqueueLogMessage(TEXT("PerformanceUtilSubsystem::Initialize begin"));
	}

	Super::Initialize(Collection);
	
	// Get the game user setting
	if (GEngine->GetGameUserSettings())
	{
		ProjectSettings = Cast<UUserProjectSettings>(GEngine->GetGameUserSettings());
	}
	
	
	if (ProjectSettings)
	{
		// Log the current scalability level
		UE_LOG(LogTemp, Log, TEXT("Current Scalability Level: %i"), ProjectSettings->GetOverallScalabilityLevel());
	}
	else
	{
		// A19: the on-screen red debug message that used to sit here is gone. It painted untethered engine
		// text over the app for a condition the user cannot act on (a missing engine object = a broken
		// build), it bypassed every Mobius error path, and it violates the project's no-on-screen-debug
		// rule. The log line is the record.
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
	}

	// Detect (or apply the persisted override for) the render performance tier once at startup
	// (tasks C1/C2). HIGH == today's DefaultEngine.ini values, so capable hardware is unchanged.
	InitializeRenderPerformanceTier();

	if (StartupLogger)
	{
		const double DurationMs = (FPlatformTime::Seconds() - InitStart) * 1000.0;
		StartupLogger->EnqueueTimedMessage(TEXT("PerformanceUtilSubsystem::Initialize"), DurationMs);
	}
}

void UPerformanceUtilSubsystem::Deinitialize()
{
	// Restore the shipping HIGH cvar set if this (Game/PIE) world applied a degraded tier. Cvars set
	// with ECVF_SetByCode persist process-wide, so without this a Low/Medium PIE session would leave
	// the editor viewport degraded until restart. Only fires if a non-High tier was actually applied
	// (the editor world never applies one, so CurrentRenderTier stays High there). (Task C1.)
	if (CurrentRenderTier != ERpt_High)
	{
		ApplyConsoleCommands(GetRenderTierConsoleCommands(ERpt_High));
		CurrentRenderTier = ERpt_High;
	}

	// Clean up any resources or delegates here if needed
	if (ProjectSettings)
	{
		ProjectSettings = nullptr; // Clear the pointer to the game user settings
	}
	// Ensure Timer handle is cleared if used
	if (AutoScalabilityTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(AutoScalabilityTimerHandle);
		AutoScalabilityTimerHandle.Invalidate();
	}
	//TODO: investigate the best way to clear the delegate
	// Clear the delegate -> this will ensure that we don't have any dangling pointers - but won't notify the listeners
	OnAutoScalabilityChanged.Clear();
	OnManualScalabilityChanged.Clear();
	
	
	Super::Deinitialize();
}

void UPerformanceUtilSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Calculate the current FPS based on DeltaTime
	CalculateCurrentFPS(DeltaTime);
	CheckCurrentFPS();
}

static bool ParseCVarString(const FString& InString, FString& OutName, FString& OutValue)
{
	// Split only on the first �=�
	if (!InString.Split(TEXT("="), &OutName, &OutValue))
	{
		return false;
	}

	// Trim any accidental whitespace
	OutName   = OutName.TrimStartAndEnd();
	OutValue  = OutValue.TrimStartAndEnd();
	return true;
}

void UPerformanceUtilSubsystem::ApplyCVarSetting(const FString& CVarSetting)
{
	FString Name, ValueStr;
	if (!ParseCVarString(CVarSetting, Name, ValueStr))
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid CVar format: %s"), *CVarSetting);
		return;
	}

	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name))
	{
		// Convert to int or float based on content
		if (ValueStr.Contains(TEXT(".")))
		{
			const float FloatVal = FCString::Atof(*ValueStr);
			CVar->Set(FloatVal, ECVF_SetByCode);
		}
		else
		{
			const int32 IntVal = FCString::Atoi(*ValueStr);
			CVar->Set(IntVal, ECVF_SetByCode);
		}

		UE_LOG(LogTemp, Log, TEXT("Set CVar %s = %s"), *Name, *ValueStr);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("CVar not found: %s"), *Name);
	}
}

void UPerformanceUtilSubsystem::ApplyConsoleCommands(const TArray<FString>& CommandArray)
{
	for (const FString& Cmd : CommandArray)
	{
		if (Cmd.IsEmpty())
			continue;

		ApplyCVarSetting(Cmd);
	}
}

//TODO: This method is simplistic and only calculates FPS based on DeltaTime.
//We may want to implement a more robust FPS calculation that averages over multiple frames or uses a more complex
//algorithm to account for frame drops and spikes.
void UPerformanceUtilSubsystem::CalculateCurrentFPS(float DeltaTime)
{
	if (DeltaTime > 0.0f)
	{
		CurrentFPS = 1.0f / DeltaTime;
		SmoothedFPS = FMath::Lerp(SmoothedFPS, CurrentFPS, 0.1f);
	}
}

void UPerformanceUtilSubsystem::ApplyScalabilityLevel(const TEnumAsByte<EScalabilitySettings> ScalabilityLevel,
                                                      const TEnumAsByte<EScalabilityCategories> ScalabilityCategory)
{
	const double ApplyStart = FPlatformTime::Seconds();
	UMobiusCustomLoggerSubsystem* StartupLogger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
	if (StartupLogger)
	{
		StartupLogger->EnqueueLogMessage(FString::Printf(TEXT("ApplyScalabilityLevel -> Level:%d Category:%d"), static_cast<int32>(ScalabilityLevel.GetValue()), static_cast<int32>(ScalabilityCategory.GetValue())));
	}

	// check if ProjectSettings is valid
	if (!ProjectSettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
		return;
	}
	
	switch (ScalabilityCategory)
	{
	case ESc_Resolution:
		// We don't set resolution here, as it is handled separately
		break;
	case ESc_GlobalIllumination:
		ProjectSettings->SetGlobalIlluminationQuality(ScalabilityLevel);
		break;
	case ESc_PostProcessing:
		ProjectSettings->SetPostProcessingQuality(ScalabilityLevel);
		break;
	case ESc_Shadows:
		ProjectSettings->SetShadowQuality(ScalabilityLevel);
		break;
	case ESc_Textures:
		ProjectSettings->SetTextureQuality(ScalabilityLevel);
		break;
	case ESc_Effects:
		ProjectSettings->SetVisualEffectQuality(ScalabilityLevel);
		break;
	case ESc_AntiAliasing:
		ProjectSettings->SetAntiAliasingQuality(ScalabilityLevel);
		break;
	case ESc_ViewDistance:
		ProjectSettings->SetViewDistanceQuality(ScalabilityLevel);
		break;
	case ESc_Reflections:
		ProjectSettings->SetReflectionQuality(ScalabilityLevel);
		break;
	case ESc_Shading:
		ProjectSettings->SetShadingQuality(ScalabilityLevel);
		break;
	case DefaultMax:
		break;
	default:
		break;
	}

	// Apply the changes to the game user settings
	ProjectSettings->ApplySettings(false);

	// Save settings to persist scalability changes to config file
	ProjectSettings->SaveSettings();

	// TODO: Extract private update method so we can update settings automatically or by users
	//OnManualScalabilityChanged.Broadcast();

	if (StartupLogger)
	{
		const double DurationMs = (FPlatformTime::Seconds() - ApplyStart) * 1000.0;
		StartupLogger->EnqueueTimedMessage(TEXT("PerformanceUtilSubsystem::ApplyScalabilityLevel"), DurationMs);
	}
}

void UPerformanceUtilSubsystem::ApplyScalabilityLevelToAll(const TEnumAsByte<EScalabilitySettings> ScalabilityLevel)
{
	const double ApplyStart = FPlatformTime::Seconds();
	UMobiusCustomLoggerSubsystem* StartupLogger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
	if (StartupLogger)
	{
		StartupLogger->EnqueueLogMessage(FString::Printf(TEXT("ApplyScalabilityLevelToAll -> Level:%d"), static_cast<int32>(ScalabilityLevel.GetValue())));
	}

	// check if ProjectSettings is valid
	if (!ProjectSettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
		return;
	}

	// TODO: check if we can optimize this and reduce
	ProjectSettings->SetGlobalIlluminationQuality(ScalabilityLevel);
	ProjectSettings->SetPostProcessingQuality(ScalabilityLevel);
	ProjectSettings->SetShadowQuality(ScalabilityLevel);
	ProjectSettings->SetTextureQuality(ScalabilityLevel);
	ProjectSettings->SetVisualEffectQuality(ScalabilityLevel);
	ProjectSettings->SetAntiAliasingQuality(ScalabilityLevel);
	ProjectSettings->SetViewDistanceQuality(ScalabilityLevel);
	ProjectSettings->SetReflectionQuality(ScalabilityLevel);
	ProjectSettings->SetShadingQuality(ScalabilityLevel);

	// Apply the changes to the game user settings
	ProjectSettings->ApplySettings(false);

	// Save settings to persist scalability changes to config file
	ProjectSettings->SaveSettings();

	// TODO: Extract private update method so we can update settings automatically or by users
	//OnManualScalabilityChanged.Broadcast();

	if (StartupLogger)
	{
		const double DurationMs = (FPlatformTime::Seconds() - ApplyStart) * 1000.0;
		StartupLogger->EnqueueTimedMessage(TEXT("PerformanceUtilSubsystem::ApplyScalabilityLevelToAll"), DurationMs);
	}
}

EScalabilitySettings UPerformanceUtilSubsystem::GetScalabilityLevel(
	const TEnumAsByte<EScalabilityCategories> ScalabilityCategory) const
{
	// check if ProjectSettings is valid
	if (!ProjectSettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
		return EScalabilitySettings::ESsl_Default;
	}
	int32 ScalabilityLevel = 0;
	
	// Get the current scalability level for the specified category
	switch (ScalabilityCategory)
	{
	case ESc_Resolution:
		// We don't set resolution here, as it is handled separately
		break;
	case ESc_GlobalIllumination:
		ScalabilityLevel = ProjectSettings->GetGlobalIlluminationQuality();
		break;
	case ESc_PostProcessing:
		ScalabilityLevel = ProjectSettings->GetPostProcessingQuality();
		break;
	case ESc_Shadows:
		ScalabilityLevel = ProjectSettings->GetShadowQuality();
		break;
	case ESc_Textures:
		ScalabilityLevel = ProjectSettings->GetTextureQuality();
		break;
	case ESc_Effects:
		ScalabilityLevel = ProjectSettings->GetVisualEffectQuality();
		break;
	case ESc_AntiAliasing:
		ScalabilityLevel = ProjectSettings->GetAntiAliasingQuality();
		break;
	case ESc_ViewDistance:
		ScalabilityLevel = ProjectSettings->GetViewDistanceQuality();
		break;
	case ESc_Reflections:
		ScalabilityLevel = ProjectSettings->GetReflectionQuality();
		break;
	case ESc_Shading:
		ScalabilityLevel = ProjectSettings->GetShadingQuality();
		break;
	case DefaultMax:
		break;
	default:
		break;
	}

	const EScalabilitySettings ScalabilitySettings = static_cast<EScalabilitySettings>(ScalabilityLevel);
	return ScalabilitySettings;
}

FIntPoint UPerformanceUtilSubsystem::GetCurrentScreenResolution() const
{
	// check if ProjectSettings is valid
	if (!ProjectSettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
		return FIntPoint(800, 600); // A low default resolution that should be safe for all modern devices
	}
	return ProjectSettings->GetScreenResolution();
}

TArray<FIntPoint> UPerformanceUtilSubsystem::GetSystemScreenResolutions() const
{
	// check if ProjectSettings is valid
	if (!ProjectSettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
		return TArray<FIntPoint>(); // Return an empty array if not found
	}

	// Using RHI we can get the available resolutions - TODO: we may want to look at refresh rate as well
	FScreenResolutionArray AvailableResolutions;
	RHIGetAvailableResolutions(AvailableResolutions, true);

	TArray<FIntPoint> Resolutions;

	// Convert FScreenResolutionRHI to FIntPoint
	for (const FScreenResolutionRHI& Resolution : AvailableResolutions)
	{
		Resolutions.Add(FIntPoint(Resolution.Width, Resolution.Height));
	}
	
	return Resolutions;
}

void UPerformanceUtilSubsystem::UpdateScreenResolutions(FIntPoint NewResolution)
{
	// check if ProjectSettings is valid
	if (!ProjectSettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
		return;
	}
	ProjectSettings->SetScreenResolution(NewResolution);

	// Apply the changes to the game user settings
	ProjectSettings->ApplySettings(false);

	// Save settings to persist resolution change to config file
	ProjectSettings->SaveSettings();

	// TODO: Extract private update method so we can update settings automatically or by users
	//OnManualScalabilityChanged.Broadcast();
}

void UPerformanceUtilSubsystem::UpdateGlobalScalabilitySetting(TEnumAsByte<EGlobalScalabilitySettings> NewSetting)
{
	const double UpdateStart = FPlatformTime::Seconds();
	UMobiusCustomLoggerSubsystem* StartupLogger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
	if (StartupLogger)
	{
		StartupLogger->EnqueueLogMessage(FString::Printf(TEXT("UpdateGlobalScalabilitySetting -> %d"), static_cast<int32>(NewSetting.GetValue())));
	}

	// check if ProjectSettings is valid
	if (!ProjectSettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
		return;
	}
	
	if (GlobalScalabilitySetting == NewSetting)
	{
		// No change needed
		return;
	}
	
	switch (NewSetting)
	{
	case EGss_Low:
		ApplyScalabilityLevelToAll(ESsl_Low);
		break;
	case EGss_Medium:
		ApplyScalabilityLevelToAll(ESsl_Medium);
		break;
	case EGss_High:
		ApplyScalabilityLevelToAll(ESsl_High);
		break;
	case EGss_Epic:
		ApplyScalabilityLevelToAll(ESsl_Epic);
		break;
	case EGss_Custom:
		// Custom settings are not predefined, so we do not apply any specific settings here.
		break;
	case EGss_Default:
		// Default case does not require any action
		break;
	default: break;
	}

	GlobalScalabilitySetting = NewSetting;

	// TODO: Extract private update method so we can update settings automatically or by users
	//OnManualScalabilityChanged.Broadcast();

	if (StartupLogger)
	{
		const double DurationMs = (FPlatformTime::Seconds() - UpdateStart) * 1000.0;
		StartupLogger->EnqueueTimedMessage(TEXT("PerformanceUtilSubsystem::UpdateGlobalScalabilitySetting"), DurationMs);
	}
}

TEnumAsByte<EGlobalScalabilitySettings> UPerformanceUtilSubsystem::GetCumulativeScalabilitySetting() const
{
	TEnumAsByte<EGlobalScalabilitySettings> CumulativeSetting = EGss_Low;
	
	// Check if ProjectSettings is valid
	if (!ProjectSettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
		return CumulativeSetting;
	}

	//TODO: Below can be optimized with a lambda
	
	// for each scalability category, check the current level and determine the cumulative setting

	// the first check is to set the cumulative setting to the lowest level and then we will check each category
	if (ProjectSettings->GetGlobalIlluminationQuality() != static_cast<int32>(CumulativeSetting))
	{
		CumulativeSetting = static_cast<EGlobalScalabilitySettings>(ProjectSettings->GetGlobalIlluminationQuality());
	}
	// Check Post Processing Quality - if they are not equal, then our global setting will be set to Custom
	if (ProjectSettings->GetPostProcessingQuality() != static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check Shadows Quality
	if (ProjectSettings->GetShadowQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check Texture Quality
	if (ProjectSettings->GetTextureQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check Visual Effect Quality
	if (ProjectSettings->GetVisualEffectQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check Anti-Aliasing Quality
	if (ProjectSettings->GetAntiAliasingQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check View Distance Quality
	if (ProjectSettings->GetViewDistanceQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check Reflection Quality
	if (ProjectSettings->GetReflectionQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check Shading Quality
	if (ProjectSettings->GetShadingQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	
	return CumulativeSetting;
}

EPedestrianScalabilitySettings UPerformanceUtilSubsystem::GetCurrentPedestrianAvatarType() const
{
	return CurrentAvatarModelType;
}

void UPerformanceUtilSubsystem::SetCurrentPedestrianAvatarType(EPedestrianScalabilitySettings NewAvatarModelType)
{
	CurrentAvatarModelType = NewAvatarModelType;
	
	// Notify listeners that the avatar model type has changed
	OnManualScalabilityChanged.Broadcast();
}

void UPerformanceUtilSubsystem::ApplyOptimizationsBasedOnFPS()
{
	const double ApplyStart = FPlatformTime::Seconds();
	UMobiusCustomLoggerSubsystem* StartupLogger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
	if (StartupLogger)
	{
		StartupLogger->EnqueueLogMessage(TEXT("ApplyOptimizationsBasedOnFPS triggered"));
	}

	// Clear the timer handle if it is valid
	if (AutoScalabilityTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(AutoScalabilityTimerHandle);
		AutoScalabilityTimerHandle.Invalidate();
	}

	// Start applying Hardware optimizations
	CheckHardwareUsageAndApplyOptimizations();// TODO: current method is a stub, we need to implement it

	// Do other optimizations based



	// set the avatar model type to low spec
	CurrentAvatarModelType = EPss_Low;

	// Notify listeners that auto scalability has changed
	OnAutoScalabilityChanged.Broadcast();

	if (StartupLogger)
	{
		const double DurationMs = (FPlatformTime::Seconds() - ApplyStart) * 1000.0;
		StartupLogger->EnqueueTimedMessage(TEXT("PerformanceUtilSubsystem::ApplyOptimizationsBasedOnFPS"), DurationMs);
	}
}

//TODO: Our check for FPS is simplistic and only checks if the FPS is below a threshold. - we may want a range tolerance as well
void UPerformanceUtilSubsystem::CheckCurrentFPS()
{
	if (CurrentFPS <= LowFPSThreshold)
	{
		if (!AutoScalabilityTimerHandle.IsValid())
		{
			// Start the timer to trigger auto scalability adjustments
			GetWorld()->GetTimerManager().SetTimer(AutoScalabilityTimerHandle, this,
			&UPerformanceUtilSubsystem::ApplyOptimizationsBasedOnFPS, 10.0f, false);

			if (UMobiusCustomLoggerSubsystem* StartupLogger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr)
			{
				StartupLogger->EnqueueLogMessage(TEXT("Low FPS detected, auto-optimization timer started"));
			}
		}
	}
	else
	{
		// if we have hit the low FPS threshold and the timer is valid, we clear it, provided the FPS has improved by our tolerance
		if (AutoScalabilityTimerHandle.IsValid() && CurrentFPS > LowFPSThreshold + 5.0f)// Adding a tolerance of 5 FPS
		{
			GetWorld()->GetTimerManager().ClearTimer(AutoScalabilityTimerHandle);
			AutoScalabilityTimerHandle.Invalidate();

			if (UMobiusCustomLoggerSubsystem* StartupLogger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr)
			{
				StartupLogger->EnqueueLogMessage(TEXT("FPS recovered, auto-optimization timer cleared"));
			}
		}
	}
}

void UPerformanceUtilSubsystem::CheckHardwareUsageAndApplyOptimizations()
{
	// Intentionally a no-op for the render tier. C1 is startup hardware-detect (InitializeRenderPerformanceTier)
	// plus the C2 override only — NOT FPS-driven runtime degradation.
	//
	// A naive "on sustained low FPS, step the tier down" here would break C1's own pass criterion
	// ("HIGH == today's cvars, identical render on capable HW"): a transient <15 FPS dip on capable
	// hardware (e.g. a heavy spawn / Niagara burst with a large crowd) would silently — and, with no
	// step-up path, permanently — drop the session below HIGH. It is also not world-gated like the
	// startup path, so it could degrade the editor viewport. Correct runtime degradation needs
	// hysteresis + step-up recovery + Game/PIE gating + explicit opt-in, so it belongs in its own task.
	//
	// (The pre-existing avatar-representation downscale in the caller ApplyOptimizationsBasedOnFPS is
	// unchanged by C1.)
}

ERenderPerformanceTier UPerformanceUtilSubsystem::DetectHardwareRenderTier() const
{
	// Heuristic auto-detect (task C1). Thresholds are deliberately conservative and tunable. Signals:
	//  - hardware ray-tracing support (a weak/old GPU without it should never run the RT path),
	//  - dedicated VRAM (the GPU's onboard video memory),
	//  - total system RAM.
	const bool bSupportsHardwareRT = GRHISupportsRayTracing;

	// Dedicated VRAM (bytes) reported by the active RHI adapter. Per RHIStats.h this is -1 when the RHI
	// could not determine it (e.g. some non-DXGI driver paths) — that is "unknown", NOT a small GPU — so
	// we must not down-tier on VRAM alone in that case. On the project's primary Win64/D3D12 target DXGI
	// populates this reliably.
	FTextureMemoryStats TexMemStats;
	RHIGetTextureMemoryStats(TexMemStats);
	const int64 DedicatedVramBytes = TexMemStats.DedicatedVideoMemory;
	const bool bVramKnown = DedicatedVramBytes >= 0;

	// Total physical system RAM (bytes).
	const uint64 TotalPhysicalBytes = FPlatformMemory::GetStats().TotalPhysical;

	constexpr int64 GiB = 1024LL * 1024LL * 1024LL;

	// LOW: no HW ray tracing, or a (known) memory-constrained GPU, or a memory-constrained system.
	if (!bSupportsHardwareRT || (bVramKnown && DedicatedVramBytes < 3 * GiB) || TotalPhysicalBytes < 8ULL * GiB)
	{
		return ERpt_Low;
	}

	// HIGH: HW ray tracing + ample (known) VRAM + ample system RAM.
	if (bVramKnown && DedicatedVramBytes >= 8 * GiB && TotalPhysicalBytes >= 16ULL * GiB)
	{
		return ERpt_High;
	}

	// Everything in between.
	return ERpt_Medium;
}

void UPerformanceUtilSubsystem::ApplyRenderPerformanceTier(TEnumAsByte<ERenderPerformanceTier> Tier)
{
	// Resolve Auto defensively (callers normally pass a concrete tier).
	const ERenderPerformanceTier ResolvedTier = (Tier.GetValue() == ERpt_Auto) ? DetectHardwareRenderTier() : Tier.GetValue();

	ApplyConsoleCommands(GetRenderTierConsoleCommands(ResolvedTier));
	CurrentRenderTier = ResolvedTier;

	if (UMobiusCustomLoggerSubsystem* Logger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr)
	{
		Logger->EnqueueLogMessage(FString::Printf(TEXT("ApplyRenderPerformanceTier -> tier %d"), static_cast<int32>(ResolvedTier)));
	}
	// Intentionally does NOT broadcast OnAutoScalabilityChanged: the render tier is render-only and
	// must not trigger the avatar-representation downscale that delegate drives.
}

void UPerformanceUtilSubsystem::InitializeRenderPerformanceTier()
{
	// Apply only in real game / PIE worlds — never the editor world — so the developer's editor viewport
	// render settings are left untouched at startup (task C1). The C2 override setter applies in any
	// world, so a tier can still be forced for in-editor testing.
	const UWorld* World = GetWorld();
	if (!World || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
	{
		return;
	}

	// Read the persisted override (task C2). Auto -> hardware detect.
	ERenderPerformanceTier Override = ERpt_Auto;
	if (const UUserProjectSettings* UserSettings = Cast<UUserProjectSettings>(ProjectSettings))
	{
		Override = UserSettings->GetRenderPerformanceTierOverride();
	}

	const ERenderPerformanceTier TierToApply = (Override == ERpt_Auto) ? DetectHardwareRenderTier() : Override;
	ApplyRenderPerformanceTier(TierToApply);
}

void UPerformanceUtilSubsystem::SetRenderPerformanceTierOverride(TEnumAsByte<ERenderPerformanceTier> NewOverride)
{
	// Persist the override (task C2) ...
	if (UUserProjectSettings* UserSettings = Cast<UUserProjectSettings>(ProjectSettings))
	{
		UserSettings->SetRenderPerformanceTierOverride(NewOverride); // writes UPROPERTY(Config) + SaveSettings()
	}

	// ... then apply immediately (Auto re-runs detection).
	const ERenderPerformanceTier TierToApply = (NewOverride.GetValue() == ERpt_Auto) ? DetectHardwareRenderTier() : NewOverride.GetValue();
	ApplyRenderPerformanceTier(TierToApply);
}

TEnumAsByte<ERenderPerformanceTier> UPerformanceUtilSubsystem::GetCurrentRenderTier() const
{
	return CurrentRenderTier;
}

