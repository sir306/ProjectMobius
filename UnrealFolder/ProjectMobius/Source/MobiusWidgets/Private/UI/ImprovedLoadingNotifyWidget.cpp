// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/ImprovedLoadingNotifyWidget.h"

#include "Core/MobiusWidgetSubsystem.h"
#include "UI/Components/BaseLoadingWidget.h"
#include "Components/TextBlock.h"
#include "GameInstances/ProjectMobiusGameInstance.h"

void UImprovedLoadingNotifyWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
}

void UImprovedLoadingNotifyWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Add the loading widget to the subsystem
	if (UMobiusWidgetSubsystem* MobiusWidgetSubsystem = GetWorld()->GetSubsystem<UMobiusWidgetSubsystem>())
	{
		MobiusWidgetSubsystem->AddLoadingWidget(this);
	}

	IsLoadingComplete();
}

void UImprovedLoadingNotifyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

void UImprovedLoadingNotifyWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}

void UImprovedLoadingNotifyWidget::UpdateLoadPercent(float NewLoadPercent)
{
	LoadingBarPercent = NewLoadPercent;
	UpdateLoadingWidgets();
}

void UImprovedLoadingNotifyWidget::SetIsLoadingGeometry(bool bNewIsLoadingGeometry)
{
	bIsLoadingGeometry = bNewIsLoadingGeometry;
	UpdateLoadingWidgets();
}

void UImprovedLoadingNotifyWidget::IsLoadingComplete()
{
	if (LoadingBarWidget != nullptr && LoadingBarWidget != nullptr)
	{
		// loading is complete so we can hide the widget
		if (LoadingBarPercent >= 1.0f && !bIsLoadingGeometry)
		{			
			// only update visibility if it is visible
			if (this->GetVisibility() == ESlateVisibility::SelfHitTestInvisible)
			{
				bIsLoadingComplete = true;
				UpdateGameInstanceLoadingState();
				
				// hide the loading widget
				this->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		else
		{
			// only update visibility if it is collapsed
			if (this->GetVisibility() == ESlateVisibility::Collapsed)
			{
				bIsLoadingComplete = false;
				UpdateGameInstanceLoadingState();
				
				this->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
		}
	}
}

void UImprovedLoadingNotifyWidget::ResetLoadPercent()
{
	LoadingBarPercent = 0.0f;
}

void UImprovedLoadingNotifyWidget::UpdateLoadingTitleTextWidget()
{
	// check if the text block is valid and if the title is different to the current one - to avoid unnecessary updates
	if (LoadingTitleText != nullptr)
	{
		LoadingTitleText->SetText(LoadingTitle);
	}
}

void UImprovedLoadingNotifyWidget::SetLoadingText(bool bIsLoadingBar, FText& NewLoadingText)
{
	if (bIsLoadingBar)
	{
		if (LoadingBarWidget != nullptr)
		{

				LoadingBarWidget->UpdateLoadingText(NewLoadingText);
			
		}
	}
	else
	{
		if (LoadingInfiniteWidget != nullptr)
		{

				LoadingInfiniteWidget->UpdateLoadingText(NewLoadingText);
			
		}
	}
}

void UImprovedLoadingNotifyWidget::UpdateLoadingTitleText()
{
	FText NewLoadingTitle;
	
	if (bIsLoadingGeometry && LoadingBarPercent < 1.0f)
	{
		NewLoadingTitle = FText::FromString("Loading Pedestrian Vectors and Geometry");
	}
	else if (!bIsLoadingGeometry && LoadingBarPercent < 1.0f)
	{
		NewLoadingTitle = FText::FromString("Loading Pedestrian Vectors");
	}
	else if (bIsLoadingGeometry && LoadingBarPercent >= 1.0f)
	{
		NewLoadingTitle = FText::FromString("Loading Geometry");
	}
	else
	{
		// this loading state should not happen but in case of update delays we can set it to loading complete
		NewLoadingTitle = FText::FromString("Loading Complete...");
	}

	if (!LoadingTitle.IdenticalTo(NewLoadingTitle))
	{
		LoadingTitle = NewLoadingTitle;
	}
}

void UImprovedLoadingNotifyWidget::UpdateGameInstanceLoadingState()
{
	// Get the project mobius game instance if null
	if (ProjectMobiusGameInstance == nullptr)
	{
		
		ProjectMobiusGameInstance = Cast<UProjectMobiusGameInstance, UGameInstance>(GetWorld()->GetGameInstance());
	}

	// if loading is happening then set it on the game instance if not null
	if (ProjectMobiusGameInstance)
	{
		ProjectMobiusGameInstance->SetDataLoadingState(!bIsLoadingComplete);
	}
}

void UImprovedLoadingNotifyWidget::UpdateLoadingWidgets()
{
	// update widget properties
	if (LoadingBarWidget != nullptr)
	{
		LoadingBarWidget->UpdateLoading(LoadingBarPercent);
	}
	if (LoadingInfiniteWidget != nullptr)
	{
		LoadingInfiniteWidget->UpdateLoading(bIsLoadingGeometry);
	}
	
	// set the visibility of the widgets
	SetLoadingWidgetVisibility(LoadingBarWidget, LoadingBarWidget->bIsLoading);
	SetLoadingWidgetVisibility(LoadingInfiniteWidget, bIsLoadingGeometry);

	UpdateLoadingTitleText();
	UpdateLoadingTitleTextWidget();
	IsLoadingComplete();
}

void UImprovedLoadingNotifyWidget::SetLoadingWidgetVisibility(TObjectPtr<UBaseLoadingWidget> LoadingWidget,
	bool bIsVisible)
{
	if (LoadingWidget != nullptr)
	{
		if (bIsVisible)
		{
			LoadingWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			LoadingWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}
