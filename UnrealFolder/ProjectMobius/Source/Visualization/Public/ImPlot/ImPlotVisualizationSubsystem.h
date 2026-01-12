// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ImPlotVisualizationSubsystem.generated.h"

class SImPlotOverlay;
class SMoveableWindow;
class SWindow;
class UMobiusWidgetSubsystem;
class SWindowTitleBarWidget;

/**
 * World subsystem that owns ImPlot overlay state and data.
 */
UCLASS()
class VISUALIZATION_API UImPlotVisualizationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
        /** Initialize the subsystem and register the overlay state. */
        virtual void Initialize(FSubsystemCollectionBase& Collection) override;

        /** Clean up the overlay and any ImPlot resources. */
        virtual void Deinitialize() override;

	/**
	 * Show or hide the ImPlot overlay.
	 * @param bShow Whether the overlay should be visible.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visualization|ImPlot")
	void ShowOverlay(bool bShow);

	/**
	 * Toggle the ImPlot overlay visibility.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visualization|ImPlot")
        void ToggleOverlay();

        /**
         * Close the overlay window and remove it from the viewport.
         */
        void CloseOverlay();

	/**
	 * Set the chart title for the overlay.
	 * @param InTitle Title text to display.
	 */
        UFUNCTION(BlueprintCallable, Category = "Visualization|ImPlot")
        void SetChartTitle(const FText& InTitle);

        /**
         * Set a status message displayed above the chart title.
         * @param InMessage Status message to display.
         */
        UFUNCTION(BlueprintCallable, Category = "Visualization|ImPlot")
        void SetStatusMessage(const FText& InMessage);

	/**
	 * Set axis labels and limits for the plot.
	 * @param InXTitle X-axis label.
	 * @param InYTitle Y-axis label.
	 * @param InXMin Minimum X value.
	 * @param InXMax Maximum X value.
	 * @param InYMin Minimum Y value.
	 * @param InYMax Maximum Y value.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visualization|ImPlot")
	void SetAxisSettings(const FText& InXTitle, const FText& InYTitle, double InXMin, double InXMax, double InYMin, double InYMax);

	/**
	 * Set the plot points for the overlay.
	 * @param InPoints Points to plot.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visualization|ImPlot")
	void SetPlotPoints(const TArray<FVector2D>& InPoints);

        /**
         * Update the live data point shown on the plot.
         * @param InTimeSeconds Current time in seconds.
         * @param InCount Current data count.
         */
        UFUNCTION(BlueprintCallable, Category = "Visualization|ImPlot")
        void UpdateLiveSample(double InTimeSeconds, double InCount);

	/** Whether the overlay is currently visible. */
	bool IsOverlayVisible() const;

        /** Accessors used by the overlay widget. */
        const FText& GetChartTitle() const;
        const FText& GetStatusMessage() const;
        const FText& GetXAxisTitle() const;
        const FText& GetYAxisTitle() const;
        void GetAxisLimits(double& OutXMin, double& OutXMax, double& OutYMin, double& OutYMax) const;
        bool HasAxisSettings() const;
	const TArray<FVector2D>& GetPlotPoints() const;
        bool HasLiveSample() const;
        void GetLiveSample(double& OutTimeSeconds, double& OutCount) const;
        /**
         * Whether a live sample thickness value is available.
         */
        bool HasLiveSampleThickness() const;

        /**
         * Get the width in time units for the live sample indicator.
         * @return Live sample width in seconds.
         */
        double GetLiveSampleThickness() const;

private:
        /** Ensure the ImPlot overlay widget is created. */
        void EnsureOverlayWidget();
        /** Open a standalone Slate window for the overlay. */
        void OpenOverlayWindow();
        /** Close the standalone Slate window for the overlay. */
        void CloseOverlayWindow();
        /** Register the overlay window with the widget subsystem. */
        void RegisterMoveableWindowActivity();
        /** Unregister the overlay window from the widget subsystem. */
        void UnregisterMoveableWindowActivity();
        /**
         * Handle window close events to keep subsystem state in sync.
         * @param ClosedWindow The window that was closed.
         */
        void HandleWindowClosed(const TSharedRef<SWindow>& ClosedWindow);
        /** Invalidate the overlay to refresh its rendering. */
        void InvalidateOverlay() const;

private:
	UPROPERTY()
	bool bOverlayVisible = false;

	UPROPERTY()
	bool bHasAxisSettings = false;

        UPROPERTY()
        bool bHasLiveSample = false;

        UPROPERTY()
        bool bHasLiveSampleThickness = false;
        bool bMoveableWindowActivityRegistered = false;

	TWeakObjectPtr<UMobiusWidgetSubsystem> MoveableWindowSubsystem;
	TSharedPtr<SImPlotOverlay> OverlayWidget;
	TSharedPtr<SWindowTitleBarWidget> OverlayTitleBarWidget;
	TSharedPtr<SMoveableWindow> OverlayWindow;

        FText ChartTitle;
        FText StatusMessage;
        FText XAxisTitle;
        FText YAxisTitle;
	double XMin = 0.0;
	double XMax = 1.0;
	double YMin = 0.0;
	double YMax = 1.0;

        TArray<FVector2D> PlotPoints;
        double LiveTimeSeconds = 0.0;
        double LiveCount = 0.0;
        double LiveSampleThickness = 0.0;
};


