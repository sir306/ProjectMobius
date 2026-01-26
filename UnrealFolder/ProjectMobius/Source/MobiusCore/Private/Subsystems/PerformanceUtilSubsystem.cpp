// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/PerformanceUtilSubsystem.h"

#include "EnumsAndStructs/AvatarScalabilityEnum.h"
#include "EnumsAndStructs/ScalabilityEnums.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/PlatformTime.h"
#include "Subsystems/MobiusCustomLoggerSubsystem.h"
#include "Engine/Engine.h"
#include "UserConfig/UserProjectSettings.h"

UPerformanceUtilSubsystem::UPerformanceUtilSubsystem()
{
	GlobalScalabilitySetting = EGss_Epic;
	CurrentAvatarModelType = EPss_High;
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
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
		// package build debug to screen
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Game User Settings not found!"));
		}
	}

	if (StartupLogger)
	{
		const double DurationMs = (FPlatformTime::Seconds() - InitStart) * 1000.0;
		StartupLogger->EnqueueTimedMessage(TEXT("PerformanceUtilSubsystem::Initialize"), DurationMs);
	}
}

void UPerformanceUtilSubsystem::Deinitialize()
{
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
	// Calculate the current FPS based on DeltaTime
	if (DeltaTime > 0.0f)
	{
		CurrentFPS = 1.0f / DeltaTime;
	}
	else
	{
		// Avoid division by zero
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
	// look at game user setting api - this may help
	// https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/GameFramework/UGameUserSettings?application_version=5.6
	
	// CPU Usage
	// Is it multi-core/multi-threaded?
	// If so, check the current CPU usage across all cores
	// If single core/threaded then require big optimizations
	/*
	 * Single core/threaded cpu would mean that we have to disable some features that are CPU intensive, and scale
	 * down core features where possible.
	 */

	// GPU Usage
	// GPU check the current 3D usage
	// GPU check the current VRAM usage
	
	// Memory Usage
	// If any of these are above a certain threshold, apply optimizations
}

