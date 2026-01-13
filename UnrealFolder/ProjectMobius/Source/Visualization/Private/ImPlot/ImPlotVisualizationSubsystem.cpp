// Fill out your copyright notice in the Description page of Project Settings.

#include "ImPlot/ImPlotVisualizationSubsystem.h"
#include "ImPlot/SImPlotOverlay.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Slate/Components/SWindowTitleBarWidget.h"
#include "Core/MobiusWidgetSubsystem.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

void UImPlotVisualizationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UImPlotVisualizationSubsystem::Deinitialize()
{
	ShowOverlay(false);
	OverlayWidget.Reset();
	OverlayWindow.Reset();
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

void UImPlotVisualizationSubsystem::SetStatusMessage(const FText& InMessage)
{
        if (StatusMessage.EqualTo(InMessage))
        {
                return;
        }
        StatusMessage = InMessage;
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

const FText& UImPlotVisualizationSubsystem::GetStatusMessage() const
{
        return StatusMessage;
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

	if (!OverlayWindow.IsValid())
	{
		const FText WindowTitle = FText::FromString(TEXT("UE Plot Overlay"));

		SAssignNew(OverlayWindow, SMoveableWindow)
			.Title(WindowTitle)
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
			.ClientSize(FVector2D(640.0f, 420.0f))
			.WindowPanelContent(OverlayWidget)
			.OnStatusMessage(FOnMoveableWindowStatusMessage::CreateUObject(this, &UImPlotVisualizationSubsystem::SetStatusMessage));

		// Don't Set Content on windows as this sets the content for titlebar, and the window area
		//OverlayWindow->SetContent(OverlayWidget.ToSharedRef());
		OverlayWindow->SetOnWindowClosed(FOnWindowClosed::CreateUObject(this, &UImPlotVisualizationSubsystem::HandleWindowClosed));

		FSlateApplication::Get().AddWindow(OverlayWindow.ToSharedRef());
		RegisterMoveableWindowActivity();
	}
        else
        {
                OverlayWindow->BringToFront(true);
        }
}

void UImPlotVisualizationSubsystem::CloseOverlayWindow()
{
	UnregisterMoveableWindowActivity();
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
		bOverlayVisible = false;
		UnregisterMoveableWindowActivity();
	}
}

void UImPlotVisualizationSubsystem::InvalidateOverlay() const
{
        if (OverlayWidget.IsValid())
        {
                OverlayWidget->Invalidate(EInvalidateWidget::Paint);
        }
}

void UImPlotVisualizationSubsystem::RegisterMoveableWindowActivity()
{
        if (bMoveableWindowActivityRegistered)
        {
                return;
        }

        if (UWorld* World = GetWorld())
        {
                if (UMobiusWidgetSubsystem* WidgetSubsystem = World->GetSubsystem<UMobiusWidgetSubsystem>())
                {
                        MoveableWindowSubsystem = WidgetSubsystem;
                        WidgetSubsystem->RegisterMoveableWindowActivity();
                        bMoveableWindowActivityRegistered = true;
                }
        }
}

void UImPlotVisualizationSubsystem::UnregisterMoveableWindowActivity()
{
        if (!bMoveableWindowActivityRegistered)
        {
                return;
        }

        if (MoveableWindowSubsystem.IsValid())
        {
                MoveableWindowSubsystem->UnregisterMoveableWindowActivity();
                MoveableWindowSubsystem.Reset();
        }
        else if (UWorld* World = GetWorld())
        {
                if (UMobiusWidgetSubsystem* WidgetSubsystem = World->GetSubsystem<UMobiusWidgetSubsystem>())
                {
                        WidgetSubsystem->UnregisterMoveableWindowActivity();
                }
        }

        bMoveableWindowActivityRegistered = false;
}
