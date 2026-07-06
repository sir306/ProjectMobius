// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/MobiusGameMode.h"

#include "UserConfig/UserProjectSettings.h"

AMobiusGameMode::AMobiusGameMode(const FObjectInitializer& ObjectInitializer)
{
}

void AMobiusGameMode::OnConstruction(const FTransform& Transform)
{
	// Cache the settings object for Blueprint access only. Resolution/window-mode restore is the engine's
	// job (PreloadResolutionSettings before window creation, UGameEngine::Init for the rest). Re-applying
	// here ran on every GameMode spawn (including PIE) with override checks enabled, which routes through
	// DetermineGameWindowResolution: that clamps to the primary monitor and substitutes a "convenient"
	// resolution when the saved one exceeds it, and the subsequent save persisted the clobbered value on
	// dual-monitor/high-DPI machines. It also forced Windowed mode, erasing the saved window mode each launch.
	if (GEngine)
	{
		ProjectUserSettings = Cast<UUserProjectSettings>(GEngine->GetGameUserSettings());
	}

	Super::OnConstruction(Transform);
}

void AMobiusGameMode::BeginPlay()
{
	Super::BeginPlay();
}

void AMobiusGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}
