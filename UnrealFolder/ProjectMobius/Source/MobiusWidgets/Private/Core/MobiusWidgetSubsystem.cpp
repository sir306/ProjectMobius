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

#include "Core/MobiusWidgetSubsystem.h"

#include "UI/ImprovedLoadingNotifyWidget.h"
#include "UI/LoadingNotifyWidget.h"
#include "ErrorHandling/ErrorWindowWidget.h"
#include "Components/PanelWidget.h"
#include "Subsystems/LoadingSubsystem.h"

UMobiusWidgetSubsystem::UMobiusWidgetSubsystem(): ErrorWidget(nullptr), LoadingNotifyWidget(nullptr)
{
}

void UMobiusWidgetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// add the loading subsystem dependency to the collection and then bind our methods to its delegates
	if (ULoadingSubsystem* LoadingSubsystem = Collection.InitializeDependency<ULoadingSubsystem>())
	{
		// ensure that it is not already bound to the delegates
		// OnLoadingPercentChanged
		LoadingSubsystem->OnLoadingPercentChanged.RemoveDynamic(this, &UMobiusWidgetSubsystem::UpdateLoadPercent);
		// OnLoadingTextChanged
		LoadingSubsystem->OnLoadingTextChanged.RemoveDynamic(this, &UMobiusWidgetSubsystem::SetLoadingText);
		// OnLoadingUnknownDurationChanged
		LoadingSubsystem->OnLoadingUnknownDurationChanged.RemoveDynamic(this, &UMobiusWidgetSubsystem::UpdateLoadingInfiniteWidget);
		
		
		// bind the loading percent changed delegate
		LoadingSubsystem->OnLoadingPercentChanged.AddDynamic(this, &UMobiusWidgetSubsystem::UpdateLoadPercent);
		// bind the loading text changed delegate
		LoadingSubsystem->OnLoadingTextChanged.AddDynamic(this, &UMobiusWidgetSubsystem::SetLoadingText);
		// OnLoadingUnknownDurationChanged
		LoadingSubsystem->OnLoadingUnknownDurationChanged.AddDynamic(this, &UMobiusWidgetSubsystem::UpdateLoadingInfiniteWidget);
	}
	
	Super::Initialize(Collection);
	
}

void UMobiusWidgetSubsystem::Deinitialize()
{
	// Unbind all bound delegates from the loading subsystem
	if (ULoadingSubsystem* LoadingSubsystem = GetWorld()->GetSubsystem<ULoadingSubsystem>())
	{
		// OnLoadingPercentChanged
		LoadingSubsystem->OnLoadingPercentChanged.RemoveDynamic(this, &UMobiusWidgetSubsystem::UpdateLoadPercent);

		// OnLoadingTextChanged
		LoadingSubsystem->OnLoadingTextChanged.RemoveDynamic(this, &UMobiusWidgetSubsystem::SetLoadingText);

		// OnLoadingUnknownDurationChanged
		LoadingSubsystem->OnLoadingUnknownDurationChanged.RemoveDynamic(this, &UMobiusWidgetSubsystem::UpdateLoadingInfiniteWidget);
	}
	
	Super::Deinitialize();
}

void UMobiusWidgetSubsystem::AddErrorWidget(UErrorWindowWidget* NewErrorWidget)
{
	// Set the error widget
	ErrorWidget = NewErrorWidget;
}

UErrorWindowWidget* UMobiusWidgetSubsystem::GetErrorWidget() const
{
	return ErrorWidget;
}

void UMobiusWidgetSubsystem::DisplayErrorWidget(const FText& TitleBarText, const FText& ErrorTitle,
	const FText& ErrorMessage, const FText& ErrorLocation)
{
	if(ErrorWidget == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Error Widget is null, cannot display error"));
		return;
	}
	// Set the variables and display the window
	ErrorWidget->SetTitleBarText(TitleBarText);
	ErrorWidget->SetErrorTitleText(ErrorTitle);
	ErrorWidget->SetErrorMessageText(ErrorMessage);
	ErrorWidget->SetErrorLocationText(ErrorLocation);
	ErrorWidget->ShowErrorWindow();
}

void UMobiusWidgetSubsystem::SetErrorTitleBarText(const FText& TitleBarText)
{
	if (ErrorWidget)
	{
		ErrorWidget->SetTitleBarText(TitleBarText);
	}
}

void UMobiusWidgetSubsystem::SetErrorTitleText(const FText& ErrorTitle)
{
	if (ErrorWidget)
	{
		ErrorWidget->SetErrorTitleText(ErrorTitle);
	}
}

void UMobiusWidgetSubsystem::SetErrorMessageText(const FText& ErrorMessage)
{
	if (ErrorWidget)
	{
		ErrorWidget->SetErrorMessageText(ErrorMessage);
	}
}

void UMobiusWidgetSubsystem::SetErrorLocationText(const FText& ErrorLocation)
{
	if (ErrorWidget)
	{
		ErrorWidget->SetErrorLocationText(ErrorLocation);
	}
}

void UMobiusWidgetSubsystem::AddLoadingWidget(UImprovedLoadingNotifyWidget* NewLoadingWidget)
{
	// set the loading widget
	LoadingNotifyWidget = NewLoadingWidget;

	// Set the position of the loading widget to the center of the screen
	//UWidgetLayoutLibrary::SlotAsCanvasSlot(LoadingNotifyWidget)->SetPosition(GetCenterPosForWidgetPanel(LoadingNotifyWidget->LoadingNotifyOverlay));

}

UImprovedLoadingNotifyWidget* UMobiusWidgetSubsystem::GetLoadingWidget() const
{
	return LoadingNotifyWidget;
}

void UMobiusWidgetSubsystem::UpdateLoadPercent(float NewLoadPercent)
{
	// check if the loading widget is null
	if(LoadingNotifyWidget == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Loading Widget is null, cannot update load percent"));
		return;
	}
	
	// Update the load percent
	LoadingNotifyWidget->UpdateLoadPercent(NewLoadPercent);

	// log the new load percent
	//UE_LOG(LogTemp, Warning, TEXT("New Load Percent: %f"), NewLoadPercent);
}

void UMobiusWidgetSubsystem::SetLoadingText(bool bIsLoadingBar, FString NewLoadingText) 
{
	// check if the loading widget is null
	if(LoadingNotifyWidget == nullptr)
    {
        UE_LOG(LogTemp, Warning, TEXT("Loading Widget is null, cannot update load percent"));
        return;
    }
	FText LoadingText = FText::FromString(NewLoadingText);
	LoadingNotifyWidget->SetLoadingText(bIsLoadingBar, LoadingText);
}

void UMobiusWidgetSubsystem::UpdateLoadingInfiniteWidget(bool bIsLoading, FString NewLoadingText)
{
	FText LoadingText = FText::FromString(NewLoadingText);
	LoadingNotifyWidget->SetLoadingText(false, LoadingText);
	LoadingNotifyWidget->SetIsLoadingGeometry(bIsLoading);
}

FVector2D UMobiusWidgetSubsystem::GetCenterPosition(UUserWidget* Widget)
{
	// Get widget size
	FVector2D WidgetSize = Widget->GetDesiredSize();

	// Get Viewport Size
	FVector2D ViewportSize = FVector2D(GEngine->GameViewport->Viewport->GetSizeXY());

	// Calculate the center position for this widget
	FVector2D WidgetPosition = FVector2D(ViewportSize.X / 2 - WidgetSize.X / 2, ViewportSize.Y / 2 - WidgetSize.Y / 2);

	return WidgetPosition;
}

FVector2D UMobiusWidgetSubsystem::GetCenterPosForWidgetPanel(UPanelWidget* WidgetPanel)
{
	// Get widget size
	FVector2D WidgetSize = WidgetPanel->GetDesiredSize();

	// Get Viewport Size
	FVector2D ViewportSize = FVector2D(GEngine->GameViewport->Viewport->GetSizeXY());

	// Calculate the center position for this widget
	FVector2D WidgetPosition = FVector2D(ViewportSize.X / 2 - WidgetSize.X / 2, ViewportSize.Y / 2 - WidgetSize.Y / 2);

	return WidgetPosition;
}
