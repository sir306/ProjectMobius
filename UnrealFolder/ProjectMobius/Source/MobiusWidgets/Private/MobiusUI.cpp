// Fill out your copyright notice in the Description page of Project Settings.


#include "MobiusUI.h"
#include "Engine/GameEngine.h"
#include "EngineGlobals.h"
#include "SlateCore.h"
#include "Framework/Application/NavigationConfig.h"

void UMobiusUI::NativePreConstruct()
{
	Super::NativePreConstruct();

	// remove the default slate config bindings as we don't want them as it interferes with custom input bindings
	FNavigationConfig& NavigationConfig = *FSlateApplication::Get().GetNavigationConfig();

	// TODO:
	// this removal is temporary as doing so makes the UI less accessible for users without mouse inputs and
	// it is not good practice or fair to remove accessibility features
	NavigationConfig.bTabNavigation = false;
	NavigationConfig.bKeyNavigation = false;
	NavigationConfig.bAnalogNavigation = false;
	NavigationConfig.KeyEventRules.Empty();
	NavigationConfig.KeyActionRules.Empty();
}

void UMobiusUI::NativeConstruct()
{
	Super::NativeConstruct();
}

void UMobiusUI::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

void UMobiusUI::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}

void UMobiusUI::MinimizeGameWindow()
{
#if !WITH_EDITOR
	// Get the game engine
	if(UGameEngine* gameEngine = Cast<UGameEngine>(GEngine))
	{
		// find the game viewport window
		TSharedPtr<SWindow> windowPtr = gameEngine->GameViewportWindow.Pin();

		// get the slate window ptr
		SWindow *window = windowPtr.Get();

		// Check if the window is valid
		if (window)
		{
			// Minimize the window
			window->Minimize();
		}
	}

#endif
}
