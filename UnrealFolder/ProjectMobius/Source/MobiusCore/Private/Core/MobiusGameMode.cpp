// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/MobiusGameMode.h"

#include "UserConfig/UserProjectSettings.h"

AMobiusGameMode::AMobiusGameMode(const FObjectInitializer& ObjectInitializer)
{
}

void AMobiusGameMode::OnConstruction(const FTransform& Transform)
{
	// Ensure we have the GameUserSetting so we can apply user preferences upon launch
	if (GEngine)
	{
		auto GameUserSettings = GEngine->GetGameUserSettings();
		
		if (GameUserSettings)
		{
			ProjectUserSettings = Cast<UUserProjectSettings>(GameUserSettings);
			
			if (ProjectUserSettings)
			{
				// Apply windowed mode by default -> till we add setting fields for this
				ProjectUserSettings->SetFullscreenMode(EWindowMode::Type::Windowed);
				// Apply the Last Screen Resolution
				ProjectUserSettings->SetScreenResolution(ProjectUserSettings->GetScreenResolution());
				// Apply all settings
				ProjectUserSettings->ApplySettings(true);
			}
		}
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
