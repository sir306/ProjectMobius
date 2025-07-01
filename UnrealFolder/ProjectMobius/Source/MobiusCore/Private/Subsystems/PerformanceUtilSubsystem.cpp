// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/PerformanceUtilSubsystem.h"

UPerformanceUtilSubsystem::UPerformanceUtilSubsystem()
{
}

UPerformanceUtilSubsystem::~UPerformanceUtilSubsystem()
{
	
}

void UPerformanceUtilSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
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

