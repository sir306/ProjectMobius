// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/ImprovedLoadingNotifyWidget.h"

#include "Core/MobiusWidgetSubsystem.h"
#include "UI/Components/BaseLoadingWidget.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Types/WidgetActiveTimerDelegate.h" // FWidgetActiveTimerDelegate (intro driver, see TickIntroAnimation)
#include "Widgets/SWidget.h"                 // SWidget::RegisterActiveTimer

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

void UImprovedLoadingNotifyWidget::ApplyMobiusTheme_Implementation()
{
	if (LoadingTitleText)
	{
		LoadingTitleText->SetColorAndOpacity(FSlateColor(GetThemeColor(EMobiusPaletteRole::LabelText)));
	}

	// The popup frame's brush is MI_LoadingOuterBackground (M_WidgetBackground, rounded 4px + 2px border)
	// with its colours BAKED DARK, so the old SetBrushColor(RibbonBg) only multiplied that near-black fill
	// and the card stayed dark in the light theme. Colour now goes through the MID params instead.
	//
	// ROOT CAUSE 2026-07-30, MEASURED IN PIE — NOT a role-collapse and NOT a failed write. The write LANDS
	// and is then DESTROYED. UIThemeSubsystem's legacy ThemeBackgroundBrush claims every brush instanced
	// from /WidgetMaterials/BackgroundMaterials/, which includes both loading MIs, and the startup ticker
	// re-runs that walk after construct without broadcasting OnThemeChanged — so it overwrote whatever this
	// function had just written: dark reverted to the MI's baked 0.006995 (#141517), light was forced to a
	// hard-coded 0.9131 (#f5f5f5). Hence the owner's "black in dark, white in light" — ONE defect, both
	// halves. Fixed by the ownership carve-out in ThemeBackgroundBrush; a theme TOGGLE always looked right
	// because ApplyTheme walks first (:785) and broadcasts second (:799), so there the owner wins.
	//
	// The card floats over the 3D VIEWPORT (CanvasPanel_0 of WBP_CompleteMobiusUI, centre-anchored), not
	// over a panel — so an earlier note here about colliding with the panel body was simply wrong.
	// PanelHeaderBg (spec-4 "header") + PanelHeaderBorder (spec-4 "hairline") are verified by capture:
	// fill #414141, rim #4a4a4a, and the card reads as a raised card against the sky. Both authoritative
	// tokens — no invented pair (see the CR item C scolding in UIThemeSubsystem.cpp:156). WellBg would be
	// wrong in kind (a well is RECESSED, this frame is raised) AND collides: WellBg dark == PanelHeaderBg
	// dark == 0.05286, which is why the inner Borders moved to InputBg (BaseLoadingWidget.cpp).
	UBaseLoadingWidget::ThemeMaterialCard(LoadingBackground,
		GetThemeColor(EMobiusPaletteRole::PanelHeaderBg), GetThemeColor(EMobiusPaletteRole::PanelHeaderBorder));
}

void UImprovedLoadingNotifyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// The §5/P6 intro used to be driven from here. It is NOT any more, and must not move back: this
	// override never runs on this widget (see TickIntroAnimation), so driving the curve from it left
	// RenderOpacity latched at the 0 that PlayIntroAnimation writes, and the loading card rendered fully
	// transparent from its first show onwards.
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
	// Both children are BindWidget (not BindWidgetOptional), so UMG refuses to compile a WBP that is
	// missing either one — this is a defensive guard, not a live null path. The second test used to
	// repeat LoadingBarWidget (copy-paste); it was always meant to be LoadingInfiniteWidget.
	if (LoadingBarWidget != nullptr && LoadingInfiniteWidget != nullptr)
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
	// SetLoadingWidgetVisibility null-checks its argument, but reading bIsLoading off the pointer does not,
	// so guard the deref here.
	if (LoadingBarWidget != nullptr)
	{
		SetLoadingWidgetVisibility(LoadingBarWidget, LoadingBarWidget->bIsLoading);
	}
	SetLoadingWidgetVisibility(LoadingInfiniteWidget, bIsLoadingGeometry);

	UpdateLoadingTitleText();
	UpdateLoadingTitleTextWidget();
	IsLoadingComplete();
}

void UImprovedLoadingNotifyWidget::PlayIntroAnimation()
{
	// The curve owns its clock through the active timer of the owning Slate widget, so it needs the
	// cached SWidget. If the tree isn't built yet, skip the flourish — and, critically, return BEFORE
	// touching the pose. Zeroing opacity with no driver to raise it again is the whole defect below.
	const TSharedPtr<SWidget> Safe = GetCachedWidget();
	if (!Safe.IsValid() || !IntroCurve.IsInitialized())
	{
		return;
	}

	// Start from the hidden pose so there is no full-size opaque first frame, then play forward.
	SetRenderOpacity(0.0f);
	SetRenderScale(FVector2D(0.97f, 0.97f));
	IntroAnimation.Play(Safe.ToSharedRef());

	// Drive the curve from an ACTIVE TIMER on the cached SWidget, never from NativeTick — see the long
	// note in TickIntroAnimation for why NativeTick cannot work here. Guarded so a second show while the
	// first is still fading does not stack a second timer; Play() above already restarted the curve.
	if (!IntroTickerHandle.IsValid())
	{
		IntroTickerHandle = Safe->RegisterActiveTimer(0.0f,
			FWidgetActiveTimerDelegate::CreateUObject(this, &UImprovedLoadingNotifyWidget::TickIntroAnimation));
	}
}

EActiveTimerReturnType UImprovedLoadingNotifyWidget::TickIntroAnimation(double InCurrentTime, float InDeltaTime)
{
	// WHY AN ACTIVE TIMER AND NOT NativeTick (measured in PIE 2026-08-03, do not "simplify" this back):
	//
	// NativeTick never runs on this widget. TickFrequency is Auto, and UUserWidget::UpdateCanTick only
	// enables ticking for Auto when the Blueprint implements Tick, a UMG WidgetAnimation is playing, or a
	// latent action is pending. WBP_ImprovedLoadingNotify's Event Tick node is an UNCONNECTED STUB that
	// compiles to nothing, and an FCurveSequence is not a UMG WidgetAnimation — so NeedsTick() stays false
	// and SObjectWidget::Tick skips NativeTick entirely.
	//
	// FCurveSequence::Play does register a Slate active timer of its own, which is what made the original
	// "mirrors SMoveableWindow's OpenAnimation" reasoning look sound. It is sound — for a raw SWidget,
	// whose Tick is not gated that way. For a UUserWidget that timer pumps Slate without ever reaching
	// NativeTick, so PlayIntroAnimation's SetRenderOpacity(0) had no writer to undo it: the card latched
	// fully transparent on its first show and stayed invisible for the rest of the session. Visibility
	// cycled correctly the whole time, which is why it read as a theming or data bug rather than a pose one.
	//
	// The invariant this function keeps is NOT "the intro plays" — it is "no path leaves the card below
	// full opacity". Hence the unconditional settle at T=1 on the terminal frame: a load that completes
	// inside the 150ms intro collapses the card mid-flight, and the next show must not inherit a partial
	// fade.
	const bool bPlaying = IntroAnimation.IsPlaying();
	const float T = bPlaying ? FMath::Clamp(IntroCurve.GetLerp(), 0.0f, 1.0f) : 1.0f;
	SetRenderOpacity(T);
	const float Scale = 0.97f + 0.03f * T; // .97 -> 1.0, centred (RenderTransformPivot default 0.5,0.5)
	SetRenderScale(FVector2D(Scale, Scale));

	if (bPlaying)
	{
		return EActiveTimerReturnType::Continue;
	}

	// Stop rather than linger: a 0-second timer left registered holds the owning window in Slate's
	// must-tick set every frame, which is the exact cost SErrorWindowWidget's bootstrap-timer comment
	// calls out. Clearing the handle re-arms the guard in PlayIntroAnimation for the next show.
	IntroTickerHandle.Reset();
	return EActiveTimerReturnType::Stop;
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
