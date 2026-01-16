/**
 * ImPlot data subsystem interface.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ImPlotDataSubsystem.generated.h"

class UImPlotVisualizationSubsystem;

/**
 * World subsystem that receives plot data and forwards it to the ImPlot overlay.
 */
UCLASS()
class VISUALIZATION_API UImPlotDataSubsystem : public UWorldSubsystem
{
        GENERATED_BODY()

public:
        /** Initialize the subsystem and resolve dependencies. */
        virtual void Initialize(FSubsystemCollectionBase& Collection) override;

        /** Clean up cached references. */
        virtual void Deinitialize() override;

        /**
         * Set the chart title for the ImPlot overlay.
         * @param InTitle Title text to display.
         */
        void SetChartTitle(const FText& InTitle);

        /**
         * Set axis labels and limits for the plot.
         * @param InXTitle X-axis label.
         * @param InYTitle Y-axis label.
         * @param InXMin Minimum X value.
         * @param InXMax Maximum X value.
         * @param InYMin Minimum Y value.
         * @param InYMax Maximum Y value.
         */
        void SetAxisSettings(const FText& InXTitle, const FText& InYTitle, double InXMin, double InXMax, double InYMin, double InYMax);

        /**
         * Set the plot points for the overlay.
         * @param InPoints Points to plot.
         */
        void SetPlotPoints(const TArray<FVector2D>& InPoints);

        /**
         * Update the live data point shown on the plot.
         * @param InTimeSeconds Current time in seconds.
         * @param InCount Current data count.
         */
        void UpdateLiveSample(double InTimeSeconds, double InCount);

        /**
         * Toggle the visibility of the ImPlot overlay window.
         */
        void ToggleOverlay();

        /** Set the chart title for a specific overlay. */
        void SetChartTitleForChart(const FName& ChartId, const FText& InTitle);

        /** Set axis labels and limits for a specific overlay. */
        void SetAxisSettingsForChart(const FName& ChartId, const FText& InXTitle, const FText& InYTitle, double InXMin, double InXMax, double InYMin, double InYMax);

        /** Set the plot points for a specific overlay. */
        void SetPlotPointsForChart(const FName& ChartId, const TArray<FVector2D>& InPoints);

        /** Update the live data point shown on a specific plot. */
        void UpdateLiveSampleForChart(const FName& ChartId, double InTimeSeconds, double InCount);

        /** Toggle the visibility of a specific ImPlot overlay window. */
        void ToggleOverlayForChart(const FName& ChartId);

private:
        /** Cached ImPlot visualization subsystem. */
        UPROPERTY()
        TObjectPtr<UImPlotVisualizationSubsystem> ImPlotSubsystem;
};
