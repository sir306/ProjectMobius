// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 Nicholas R. Harding
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

#include "LoadingNotifyWidget.h"

#include "MobiusWidgetSubsystem.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "GameInstances/ProjectMobiusGameInstance.h"

void ULoadingNotifyWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
}

void ULoadingNotifyWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Add the loading widget to the subsystem
	if (UMobiusWidgetSubsystem* MobiusWidgetSubsystem = GetWorld()->GetSubsystem<UMobiusWidgetSubsystem>())
	{
		MobiusWidgetSubsystem->AddLoadingWidget(this);
	}
}

void ULoadingNotifyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

void ULoadingNotifyWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// Bind and styling
	if(LoadingText)
	{
		
	}
	if(LoadedAmount)
	{
		
	}
	if(LoadingTitle)
	{
		
	}
	if(LoadingBar)
	{
		//TODO: Fix this - this binding doesn't work as expected but can be bound in the blueprint
		// Bind the loading bar to the load percent value
		//LoadingBar->SetPercent(LoadPercent);
		//LoadingBar->PercentDelegate.BindUFunction(this, "GetLoadPercent");
	}
	if(LoadingNotifyOverlay)
	{
		
	}
}

void ULoadingNotifyWidget::UpdateLoadPercent(float NewLoadPercent)
{
	LoadPercent = NewLoadPercent;

	// Update the loaded amount text
	if (LoadedAmount)
	{
		LoadedAmount->SetText(FText::AsPercent(LoadPercent));
	}
	
	IsLoadingComplete(); // check if we have finished loading
}

void ULoadingNotifyWidget::IsLoadingComplete()
{
	// Get the project mobius game instance if null
	if (ProjectMobiusGameInstance == nullptr)
	{
		
		ProjectMobiusGameInstance = Cast<UProjectMobiusGameInstance, UGameInstance>(GetWorld()->GetGameInstance());
	}
	
	// if load percent is 100% (1 == 100) then hide the widget
	if (LoadPercent >= 1.0f)
	{
		// if loading is happening then set it on the game instance if not null
		if (ProjectMobiusGameInstance)
		{
			ProjectMobiusGameInstance->SetDataLoadingState(false);
		}
		SetVisibility(ESlateVisibility::Hidden);
	}
	else
	{
		// if loading is happening then set it on the game instance if not null
		if (ProjectMobiusGameInstance)
		{
			ProjectMobiusGameInstance->SetDataLoadingState(true);
		}
		SetVisibility(ESlateVisibility::Visible);
	}
}

void ULoadingNotifyWidget::ResetLoadPercent()
{
	LoadPercent = 0.0f;
	IsLoadingComplete(); // check to make it visible again
}

void ULoadingNotifyWidget::SetLoadingTextAndTitle(const FString NewLoadingText, const FString NewLoadingTitle)
{
	// check if the text block is valid
	if (LoadingText)
	{
		LoadingText->SetText(FText::FromString(NewLoadingText));
	}
	// check if the text block is valid
	if (LoadingTitle)
	{
		LoadingTitle->SetText(FText::FromString(NewLoadingTitle));
	}
}
