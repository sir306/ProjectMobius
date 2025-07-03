// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/PerformanceUtilSubsystem.h"

#include "EnumsAndStructs/ScalabilityEnums.h"
#include "GameFramework/GameUserSettings.h"

UPerformanceUtilSubsystem::UPerformanceUtilSubsystem()
{
}

UPerformanceUtilSubsystem::~UPerformanceUtilSubsystem()
{
	
}

void UPerformanceUtilSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	// Get the game user setting
	GameUserSettings = GEngine->GetGameUserSettings();
	
	if (GameUserSettings)
	{
		// Log the current scalability level
		UE_LOG(LogTemp, Log, TEXT("Current Scalability Level: %i"), GameUserSettings->GetOverallScalabilityLevel());
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
}

void UPerformanceUtilSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UPerformanceUtilSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

static bool ParseCVarString(const FString& InString, FString& OutName, FString& OutValue)
{
	// Split only on the first “=”
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

void UPerformanceUtilSubsystem::ApplyScalabilityLevel(const TEnumAsByte<EScalabilitySettings> ScalabilityLevel,
	const TEnumAsByte<EScalabilityCategories> ScalabilityCategory)
{
	// check if GameUserSettings is valid
	if (!GameUserSettings)
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
		GameUserSettings->SetGlobalIlluminationQuality(ScalabilityLevel);
		break;
	case ESc_PostProcessing:
		GameUserSettings->SetPostProcessingQuality(ScalabilityLevel);
		break;
	case ESc_Shadows:
		GameUserSettings->SetShadowQuality(ScalabilityLevel);
		break;
	case ESc_Textures:
		GameUserSettings->SetTextureQuality(ScalabilityLevel);
		break;
	case ESc_Effects:
		GameUserSettings->SetVisualEffectQuality(ScalabilityLevel);
		break;
	case ESc_AntiAliasing:
		GameUserSettings->SetAntiAliasingQuality(ScalabilityLevel);
		break;
	case ESc_ViewDistance:
		GameUserSettings->SetViewDistanceQuality(ScalabilityLevel);
		break;
	case ESc_Reflections:
		GameUserSettings->SetReflectionQuality(ScalabilityLevel);
		break;
	case ESc_Shading:
		GameUserSettings->SetShadingQuality(ScalabilityLevel);
		break;
	case DefaultMax:
		break;
	default:
		break;
	}

	// Apply the changes to the game user settings
	GameUserSettings->ApplySettings(false);
}

void UPerformanceUtilSubsystem::ApplyScalabilityLevelToAll(const TEnumAsByte<EScalabilitySettings> ScalabilityLevel)
{
	// check if GameUserSettings is valid
	if (!GameUserSettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
		return;
	}

	// TODO: check if we can optimize this and reduce
	GameUserSettings->SetGlobalIlluminationQuality(ScalabilityLevel);
	GameUserSettings->SetPostProcessingQuality(ScalabilityLevel);
	GameUserSettings->SetShadowQuality(ScalabilityLevel);
	GameUserSettings->SetTextureQuality(ScalabilityLevel);
	GameUserSettings->SetVisualEffectQuality(ScalabilityLevel);
	GameUserSettings->SetAntiAliasingQuality(ScalabilityLevel);
	GameUserSettings->SetViewDistanceQuality(ScalabilityLevel);
	GameUserSettings->SetReflectionQuality(ScalabilityLevel);
	GameUserSettings->SetShadingQuality(ScalabilityLevel);
	
	// Apply the changes to the game user settings
	GameUserSettings->ApplySettings(false);
}

EScalabilitySettings UPerformanceUtilSubsystem::GetScalabilityLevel(
	const TEnumAsByte<EScalabilityCategories> ScalabilityCategory) const
{
	// check if GameUserSettings is valid
	if (!GameUserSettings)
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
		ScalabilityLevel = GameUserSettings->GetGlobalIlluminationQuality();
		break;
	case ESc_PostProcessing:
		ScalabilityLevel = GameUserSettings->GetPostProcessingQuality();
		break;
	case ESc_Shadows:
		ScalabilityLevel = GameUserSettings->GetShadowQuality();
		break;
	case ESc_Textures:
		ScalabilityLevel = GameUserSettings->GetTextureQuality();
		break;
	case ESc_Effects:
		ScalabilityLevel = GameUserSettings->GetVisualEffectQuality();
		break;
	case ESc_AntiAliasing:
		ScalabilityLevel = GameUserSettings->GetAntiAliasingQuality();
		break;
	case ESc_ViewDistance:
		ScalabilityLevel = GameUserSettings->GetViewDistanceQuality();
		break;
	case ESc_Reflections:
		ScalabilityLevel = GameUserSettings->GetReflectionQuality();
		break;
	case ESc_Shading:
		ScalabilityLevel = GameUserSettings->GetShadingQuality();
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
	// check if GameUserSettings is valid
	if (!GameUserSettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
		return FIntPoint(800, 600); // A low default resolution that should be safe for all modern devices
	}
	return GameUserSettings->GetScreenResolution();
}

TArray<FIntPoint> UPerformanceUtilSubsystem::GetSystemScreenResolutions() const
{
	// check if GameUserSettings is valid
	if (!GameUserSettings)
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
	// check if GameUserSettings is valid
	if (!GameUserSettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
		return;
	}
	GameUserSettings->SetScreenResolution(NewResolution);

	// Apply the changes to the game user settings
	GameUserSettings->ApplySettings(false);
}

void UPerformanceUtilSubsystem::UpdateGlobalScalabilitySetting(TEnumAsByte<EGlobalScalabilitySettings> NewSetting)
{
	// check if GameUserSettings is valid
	if (!GameUserSettings)
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
}

TEnumAsByte<EGlobalScalabilitySettings> UPerformanceUtilSubsystem::GetCumulativeScalabilitySetting() const
{
	TEnumAsByte<EGlobalScalabilitySettings> CumulativeSetting = EGss_Low;
	
	// Check if GameUserSettings is valid
	if (!GameUserSettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Game User Settings not found!"));
		return CumulativeSetting;
	}

	//TODO: Below can be optimized with a lambda
	
	// for each scalability category, check the current level and determine the cumulative setting

	// the first check is to set the cumulative setting to the lowest level and then we will check each category
	if (GameUserSettings->GetGlobalIlluminationQuality() != static_cast<int32>(CumulativeSetting))
	{
		CumulativeSetting = static_cast<EGlobalScalabilitySettings>(GameUserSettings->GetGlobalIlluminationQuality());
	}
	// Check Post Processing Quality - if they are not equal, then our global setting will be set to Custom
	if (GameUserSettings->GetPostProcessingQuality() != static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check Shadows Quality
	if (GameUserSettings->GetShadowQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check Texture Quality
	if (GameUserSettings->GetTextureQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check Visual Effect Quality
	if (GameUserSettings->GetVisualEffectQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check Anti-Aliasing Quality
	if (GameUserSettings->GetAntiAliasingQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check View Distance Quality
	if (GameUserSettings->GetViewDistanceQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check Reflection Quality
	if (GameUserSettings->GetReflectionQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	// Check Shading Quality
	if (GameUserSettings->GetShadingQuality() > static_cast<int32>(CumulativeSetting))
	{
		return CumulativeSetting = EGss_Custom;
	}
	
	return CumulativeSetting;
}

