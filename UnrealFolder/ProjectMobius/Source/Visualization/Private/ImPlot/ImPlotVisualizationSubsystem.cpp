// Fill out your copyright notice in the Description page of Project Settings.

#include "ImPlot/ImPlotVisualizationSubsystem.h"
#include "ImPlot/SImPlotOverlay.h"
#include "ImPlot/SImPlotWindowTitleBarWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWindow.h"

void UImPlotVisualizationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UImPlotVisualizationSubsystem::Deinitialize()
{
        ShowOverlay(false);
        OverlayWidget.Reset();
        OverlayWindow.Reset();
        TitleBarWidget.Reset();
        StopWindowPolling();
        Super::Deinitialize();
}

void UImPlotVisualizationSubsystem::ShowOverlay(bool bShow)
{
        bOverlayVisible = bShow;

        if (bShow)
        {
                EnsureOverlayWidget();
                OpenOverlayWindow();
        }
        else
        {
                CloseOverlayWindow();
        }

        InvalidateOverlay();
}

void UImPlotVisualizationSubsystem::ToggleOverlay()
{
        ShowOverlay(!bOverlayVisible);
}

void UImPlotVisualizationSubsystem::CloseOverlay()
{
        ShowOverlay(false);
}

void UImPlotVisualizationSubsystem::SetChartTitle(const FText& InTitle)
{
	ChartTitle = InTitle;
	InvalidateOverlay();
}

void UImPlotVisualizationSubsystem::SetAxisSettings(const FText& InXTitle, const FText& InYTitle, double InXMin, double InXMax, double InYMin, double InYMax)
{
	XAxisTitle = InXTitle;
	YAxisTitle = InYTitle;
	XMin = InXMin;
	XMax = InXMax;
	YMin = InYMin;
	YMax = InYMax;
	bHasAxisSettings = true;

	InvalidateOverlay();
}

void UImPlotVisualizationSubsystem::SetPlotPoints(const TArray<FVector2D>& InPoints)
{
	PlotPoints = InPoints;
	InvalidateOverlay();
}

void UImPlotVisualizationSubsystem::UpdateLiveSample(double InTimeSeconds, double InCount)
{
        if (bHasLiveSample)
        {
                LiveSampleThickness = FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(InTimeSeconds - LiveTimeSeconds));
                bHasLiveSampleThickness = true;
        }
        else
        {
                LiveSampleThickness = 0.0;
                bHasLiveSampleThickness = false;
        }

        LiveTimeSeconds = InTimeSeconds;
        LiveCount = InCount;
        bHasLiveSample = true;

        InvalidateOverlay();
}

bool UImPlotVisualizationSubsystem::IsOverlayVisible() const
{
	return bOverlayVisible;
}

const FText& UImPlotVisualizationSubsystem::GetChartTitle() const
{
	return ChartTitle;
}

const FText& UImPlotVisualizationSubsystem::GetXAxisTitle() const
{
	return XAxisTitle;
}

const FText& UImPlotVisualizationSubsystem::GetYAxisTitle() const
{
	return YAxisTitle;
}

void UImPlotVisualizationSubsystem::GetAxisLimits(double& OutXMin, double& OutXMax, double& OutYMin, double& OutYMax) const
{
	OutXMin = XMin;
	OutXMax = XMax;
	OutYMin = YMin;
	OutYMax = YMax;
}

bool UImPlotVisualizationSubsystem::HasAxisSettings() const
{
	return bHasAxisSettings;
}

const TArray<FVector2D>& UImPlotVisualizationSubsystem::GetPlotPoints() const
{
	return PlotPoints;
}

bool UImPlotVisualizationSubsystem::HasLiveSample() const
{
	return bHasLiveSample;
}

void UImPlotVisualizationSubsystem::GetLiveSample(double& OutTimeSeconds, double& OutCount) const
{
        OutTimeSeconds = LiveTimeSeconds;
        OutCount = LiveCount;
}

bool UImPlotVisualizationSubsystem::HasLiveSampleThickness() const
{
        return bHasLiveSampleThickness;
}

double UImPlotVisualizationSubsystem::GetLiveSampleThickness() const
{
        return LiveSampleThickness;
}

void UImPlotVisualizationSubsystem::EnsureOverlayWidget()
{
        if (!OverlayWidget.IsValid())
        {
                OverlayWidget = SNew(SImPlotOverlay)
                        .Subsystem(this)
                        .OnRequestClose(FSimpleDelegate::CreateUObject(this, &UImPlotVisualizationSubsystem::CloseOverlay));
        }
}

void UImPlotVisualizationSubsystem::OpenOverlayWindow()
{
        if (!OverlayWidget.IsValid() || !FSlateApplication::IsInitialized())
        {
                return;
        }

        /** TODO: 
         * need to pick up on slate events that when the window is moved or resized we pause the simulation and unpause
         * this is so that the simulation doesn't continue running as the window for the simulation freezes while the other is being moved
         * 
         * below is a list of relevant delegates from SWindow.h that we can hook into and address the issue:
         * 
         * Sets the delegate to execute right after the window has been moved
         * SLATECORE_API void SetOnWindowMoved( const FOnWindowMoved& InDelegate);
         * 
         * @return Returns true if the window is currently morphing to a new position, shape and/or opacity 
         * SLATECORE_API bool IsMorphing() const;
         *
         * @return Returns true if the window is currently morphing and is morphing by size
         * SLATECORE_API bool IsMorphingSize() const;
         * 
         * 
         * Possible things to investigate that may help with this:
         * 
         * @return true if this is a vanilla window, or one being used for some special purpose: e.g. tooltip or menu 
         * SLATECORE_API bool IsRegularWindow() const;
         * 
         * @return True if we expect the window size to change frequently. See description of bSizeWillChangeOften member variable.
         * bool SizeWillChangeOften() const
         *  
         * SLATE_ARGUMENT( EWindowType, Type )
         *  
         * Use the default os look for the border of the window  
         * SLATE_ARGUMENT( bool, UseOSWindowBorder )
         * 
         * 
         *  
         *         **/
        
        //
        if (!OverlayWindow.IsValid())
        {
                SAssignNew(OverlayWindow, SWindow)
                        .Title(FText::FromString(TEXT("UE Plot Overlay")))
                        .SizingRule(ESizingRule::UserSized)
                        .FocusWhenFirstShown(false)
                        .ActivationPolicy(EWindowActivationPolicy::Never)
                        .SupportsMaximize(true)
                        .SupportsMinimize(true)
                        .IsTopmostWindow(false)
                        .CreateTitleBar(true)
                        .HasCloseButton(true)
                        .AutoCenter(EAutoCenter::PreferredWorkArea)
                        .UseOSWindowBorder(false)
                        .ClientSize(FVector2D(640.0f, 420.0f));

                OverlayWindow->SetContent(OverlayWidget.ToSharedRef());
                OverlayWindow->SetOnWindowClosed(FOnWindowClosed::CreateUObject(this, &UImPlotVisualizationSubsystem::HandleWindowClosed));
                OverlayWindow->SetOnWindowMoved(FOnWindowMoved::CreateUObject(this, &UImPlotVisualizationSubsystem::HandleWindowMoved));

                const FWindowStyle WindowStyle = FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
                SAssignNew(TitleBarWidget, SImPlotWindowTitleBarWidget)
                        .OwnerWindow(OverlayWindow)
                        .TitleText(FText::FromString(TEXT("UE Plot Overlay")))
                        .TitleTextStyle(&WindowStyle.TitleTextStyle)
                        .WindowStyle(&WindowStyle)
                        .TitleAlignment(HAlign_Left)
                        .ShowAppIcon(false);
                if (TitleBarWidget.IsValid())
                {
                        OverlayWindow->SetTitleBar(TitleBarWidget->GetTitleBar());
                }

                TSharedPtr<SWindow> ParentWindow;
                if (GEngine && GEngine->GameViewport)
                {
                        ParentWindow = GEngine->GameViewport->GetWindow();
                }
                if (ParentWindow.IsValid())
                {
                        FSlateApplication::Get().AddWindowAsNativeChild(OverlayWindow.ToSharedRef(), ParentWindow.ToSharedRef());
                }
                else
                {
                        FSlateApplication::Get().AddWindow(OverlayWindow.ToSharedRef());
                }

                StartWindowPolling();
        }
        else
        {
                OverlayWindow->BringToFront(true);
        }
}

void UImPlotVisualizationSubsystem::CloseOverlayWindow()
{
        StopWindowPolling();
        if (!OverlayWindow.IsValid() || !FSlateApplication::IsInitialized())
        {
                OverlayWindow.Reset();
                return;
        }

        FSlateApplication::Get().RequestDestroyWindow(OverlayWindow.ToSharedRef());
        OverlayWindow.Reset();
}

void UImPlotVisualizationSubsystem::HandleWindowClosed(const TSharedRef<SWindow>& ClosedWindow)
{
        if (OverlayWindow == ClosedWindow)
        {
                OverlayWindow.Reset();
                TitleBarWidget.Reset();
                bOverlayVisible = false;
                StopWindowPolling();
                ResumeAfterWindowInteraction();
        }
}

void UImPlotVisualizationSubsystem::HandleWindowMoved(const TSharedRef<SWindow>& MovedWindow)
{
        if (OverlayWindow == MovedWindow)
        {
                PauseForWindowInteraction();
                LastInteractionSeconds = FPlatformTime::Seconds();
        }
}

bool UImPlotVisualizationSubsystem::TickOverlayWindow(float DeltaTime)
{
        if (!OverlayWindow.IsValid())
        {
                return false;
        }

        const FVector2D CurrentPosition = FVector2D(OverlayWindow->GetPositionInScreen());
        const FVector2D CurrentSize = FVector2D(OverlayWindow->GetSizeInScreen());
        if (!bHasLastWindowRect)
        {
                LastWindowPosition = CurrentPosition;
                LastWindowSize = CurrentSize;
                bHasLastWindowRect = true;
                return true;
        }

        const bool bMoved = !CurrentPosition.Equals(LastWindowPosition, 0.1f);
        const bool bResized = !CurrentSize.Equals(LastWindowSize, 0.1f);
        if (bMoved || bResized)
        {
                PauseForWindowInteraction();
                LastWindowPosition = CurrentPosition;
                LastWindowSize = CurrentSize;
                LastInteractionSeconds = FPlatformTime::Seconds();
        }

        if (bIsInteractionPaused)
        {
                const double NowSeconds = FPlatformTime::Seconds();
                if (NowSeconds - LastInteractionSeconds >= 0.25)
                {
                        ResumeAfterWindowInteraction();
                }
        }

        return true;
}

void UImPlotVisualizationSubsystem::StartWindowPolling()
{
        if (WindowPollHandle.IsValid())
        {
                return;
        }

        if (OverlayWindow.IsValid())
        {
                LastWindowPosition = FVector2D(OverlayWindow->GetPositionInScreen());
                LastWindowSize = FVector2D(OverlayWindow->GetSizeInScreen());
                bHasLastWindowRect = true;
        }

        WindowPollHandle = FTSTicker::GetCoreTicker().AddTicker(
                FTickerDelegate::CreateUObject(this, &UImPlotVisualizationSubsystem::TickOverlayWindow));
}

void UImPlotVisualizationSubsystem::StopWindowPolling()
{
        if (!WindowPollHandle.IsValid())
        {
                return;
        }

        FTSTicker::GetCoreTicker().RemoveTicker(WindowPollHandle);
        WindowPollHandle.Reset();
        bHasLastWindowRect = false;
}

void UImPlotVisualizationSubsystem::PauseForWindowInteraction()
{
        if (!bIsInteractionPaused)
        {
                bIsInteractionPaused = true;
                if (UWorld* World = GetWorld())
                {
                        bWasPausedBeforeInteraction = World->IsPaused();
                        if (!bWasPausedBeforeInteraction)
                        {
                                UGameplayStatics::SetGamePaused(World, true);
                        }
                }
        }
}

void UImPlotVisualizationSubsystem::ResumeAfterWindowInteraction()
{
        if (!bIsInteractionPaused)
        {
                return;
        }

        bIsInteractionPaused = false;
        if (UWorld* World = GetWorld())
        {
                if (!bWasPausedBeforeInteraction)
                {
                        UGameplayStatics::SetGamePaused(World, false);
                }
        }
}

void UImPlotVisualizationSubsystem::InvalidateOverlay() const
{
        if (OverlayWidget.IsValid())
	{
		OverlayWidget->Invalidate(EInvalidateWidget::Paint);
	}
}
