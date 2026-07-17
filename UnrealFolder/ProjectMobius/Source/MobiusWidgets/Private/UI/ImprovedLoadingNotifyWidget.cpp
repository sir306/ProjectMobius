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

	// §5/P6 intro curve: 150ms, ease-out. Built once; (re)played on each show (PlayIntroAnimation).
	if (!IntroCurve.IsInitialized())
	{
		IntroCurve = IntroAnimation.AddCurve(0.0f, 0.15f, ECurveEaseFunction::CubicOut);
	}

	IsLoadingComplete();
}

void UImprovedLoadingNotifyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// §5/P6: drive the entrance fade+scale from the curve while it plays (see PlayIntroAnimation). The
	// active timer guarantees a final tick at Lerp=1, so the popup settles fully opaque at 1.0 scale.
	if (IntroAnimation.IsPlaying())
	{
		const float T = FMath::Clamp(IntroCurve.GetLerp(), 0.0f, 1.0f);
		SetRenderOpacity(T);
		const float Scale = 0.97f + 0.03f * T; // .97 -> 1.0, centred (RenderTransformPivot default 0.5,0.5)
		SetRenderScale(FVector2D(Scale, Scale));
	}
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

				// §5/P6: this is the show path (Collapsed -> visible) — play the entrance intro.
				PlayIntroAnimation();
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

void UImprovedLoadingNotifyWidget::PlayIntroAnimation()
{
	// The curve owns its clock through the active timer of the owning Slate widget, so it needs the
	// cached SWidget. If the tree isn't built yet, skip the flourish (no visual regression).
	const TSharedPtr<SWidget> Safe = GetCachedWidget();
	if (!Safe.IsValid() || !IntroCurve.IsInitialized())
	{
		return;
	}

	// Start from the hidden pose so there is no full-size opaque first frame, then play forward.
	SetRenderOpacity(0.0f);
	SetRenderScale(FVector2D(0.97f, 0.97f));
	IntroAnimation.Play(Safe.ToSharedRef());
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
