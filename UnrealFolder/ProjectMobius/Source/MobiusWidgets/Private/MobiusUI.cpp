// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.  
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,  
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL  
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR  
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING  
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS  
 * IN THE SOFTWARE.
 */

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
