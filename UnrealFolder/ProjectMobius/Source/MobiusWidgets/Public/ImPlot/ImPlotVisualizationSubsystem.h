/**
 * ImPlot visualization subsystem interface.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ImPlotVisualizationSubsystem.generated.h"


class FPaintArgs;
struct FGeometry;
class FSlateRect;
class FSlateWindowElementList;
class FWidgetStyle;
class SWidget;

struct ImGuiContext;
struct ImPlotContext;
struct ImDrawData;
struct ImFontAtlas;
struct FSlateDynamicImageBrush;
class UTextureRenderTarget2D;

class SImPlotOverlay;
class SMoveableWindow;
class SWindow;
class UMobiusWidgetSubsystem;
class UUIThemeSubsystem;

/**
 * World subsystem that owns ImPlot overlay state and data.
 */
UCLASS()
class MOBIUSWIDGETS_API UImPlotVisualizationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
        /** Initialize the subsystem. */
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

        /** Show or hide a specific ImPlot overlay. */
        void ShowOverlayForChart(const FName& ChartId, bool bShow);

        /** Toggle a specific ImPlot overlay visibility. */
        void ToggleOverlayForChart(const FName& ChartId);

        /** Close a specific overlay window and remove it from the viewport. */
        void CloseOverlayForChart(const FName& ChartId);

        /** Set the chart title for a specific overlay. */
        void SetChartTitleForChart(const FName& ChartId, const FText& InTitle);

        /** Set axis labels and limits for a specific overlay. */
        void SetAxisSettingsForChart(const FName& ChartId, const FText& InXTitle, const FText& InYTitle, double InXMin, double InXMax, double InYMin, double InYMax);

        /** Set the plot points for a specific overlay. */
        void SetPlotPointsForChart(const FName& ChartId, const TArray<FVector2D>& InPoints);

        /** Update the live data point shown on a specific plot. */
        void UpdateLiveSampleForChart(const FName& ChartId, double InTimeSeconds, double InCount);

	/** Whether the overlay is currently visible. */
	bool IsOverlayVisible() const;

        /** Accessors used by the overlay widget. */
        const FText& GetChartTitle() const;
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

        /** Accessors used by the overlay widget for a specific chart. */
        bool IsOverlayVisibleForChart(const FName& ChartId) const;
        const FText& GetChartTitleForChart(const FName& ChartId) const;
        const FText& GetXAxisTitleForChart(const FName& ChartId) const;
        const FText& GetYAxisTitleForChart(const FName& ChartId) const;
        void GetAxisLimitsForChart(const FName& ChartId, double& OutXMin, double& OutXMax, double& OutYMin, double& OutYMax) const;
        bool HasAxisSettingsForChart(const FName& ChartId) const;
        const TArray<FVector2D>& GetPlotPointsForChart(const FName& ChartId) const;
        bool HasLiveSampleForChart(const FName& ChartId) const;
        void GetLiveSampleForChart(const FName& ChartId, double& OutTimeSeconds, double& OutCount) const;
        bool HasLiveSampleThicknessForChart(const FName& ChartId) const;
        double GetLiveSampleThicknessForChart(const FName& ChartId) const;


        int32 PaintOverlayForChart(const FName& ChartId, const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
                const FWidgetStyle& InWidgetStyle, bool bParentEnabled, const TSharedRef<const SWidget>& Widget,
                const FSimpleDelegate& OnRequestClose);

        /**
         * Render the chart offscreen and put the image on the OS clipboard, if a copy was asked for.
         *
         * Split from the click on purpose. The button/menu item only raises a flag, because the capture
         * has to FlushRenderingCommands and re-render the widget — neither is safe from inside
         * PaintOverlayForChart, which IS a Slate paint. SImPlotOverlay's hover timer calls this, so the
         * work lands on a normal tick with the cursor still over the chart.
         *
         * No-op when nothing is pending, so it is cheap to call every tick.
         */
        void ServicePendingImageCopy(const FName& ChartId);

private:

        struct FImPlotOverlayState
        {
                bool bOverlayVisible = false;
                bool bHasAxisSettings = false;
                bool bHasLiveSample = false;
                bool bHasLiveSampleThickness = false;
                bool bMoveableWindowActivityRegistered = false;

                /** Raised by the "Copy chart image" button / menu item; serviced next tick. */
                bool bImageCopyRequested = false;

                /**
                 * Whether "Copy values" also writes the time column. OFF by default — whoever is reading
                 * this chart supplied the trajectory data, so the timestamps are usually the least
                 * interesting column. Toggled from the right-click menu. Per chart and per session; it is
                 * not written to UUserProjectSettings.
                 */
                bool bCopyWithTimeline = false;

                /**
                 * DPI scale of the last ON-SCREEN paint. The capture pass has no window to ask, and
                 * letting it fall back to 1.0 would re-bake SharedFontAtlas — which is shared by every
                 * chart context — and then re-bake it again on the next on-screen paint. Replaying the
                 * live scale keeps the atlas untouched.
                 */
                float LastPaintDpiScale = 1.0f;

                bool bWindowOpen = true;
                ImGuiContext* ImGuiContext = nullptr;
                ImPlotContext* ImPlotContext = nullptr;
                TWeakObjectPtr<UMobiusWidgetSubsystem> MoveableWindowSubsystem;
                TSharedPtr<SImPlotOverlay> OverlayWidget;
                TSharedPtr<SMoveableWindow> OverlayWindow;

                FText ChartTitle;
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

        /** Per-chart overlay data keyed by chart id. */
        TMap<FName, FImPlotOverlayState> OverlayStates;

        /**
         * Themed SWindow chrome shared by all chart overlay windows (D8/Q3). SWindow keeps its style by
         * pointer, so it must live at a stable address for the window's lifetime — a subsystem member,
         * NOT a TMap value (which moves on rehash). Refreshed to the current theme each OpenOverlayWindow.
         */
        FWindowStyle ChartWindowStyle;

        /** True once ChartWindowStyle has been seeded, so a failed theme lookup keeps the last good
         *  style instead of resetting the shared chrome to the CoreStyle gray (D8/Q3 harden). */
        bool bChartWindowStyleInitialized = false;

        /**
         * Theme subsystem we bound OnThemeChanged on. Weak so Deinitialize can RemoveDynamic safely: the
         * theme subsystem is a GameInstance subsystem and outlives this World subsystem across PIE stop /
         * level change. OnThemeChanged is a DYNAMIC delegate - bound/unbound by (object, UFUNCTION name),
         * so there is no FDelegateHandle to store; the weak ptr is what the unbind needs.
         */
        TWeakObjectPtr<UUIThemeSubsystem> BoundThemeSubsystem;

        /** Bind OnThemeChanged once (idempotent; theme subsystem resolved lazily on first window open). */
        void EnsureThemeChangeBinding();

        /** Re-theme the shared chart window chrome in place on a live theme toggle and repaint. */
        UFUNCTION()
        void HandleThemeChanged();

        FImPlotOverlayState& GetOrCreateOverlayState(const FName& ChartId);
        FImPlotOverlayState* FindOverlayState(const FName& ChartId);
        const FImPlotOverlayState* FindOverlayState(const FName& ChartId) const;

        /** Ensure the ImPlot overlay widget is created. */
        void EnsureOverlayWidget(FImPlotOverlayState& State, const FName& ChartId);
        /** Open a standalone Slate window for the overlay. */
        void OpenOverlayWindow(FImPlotOverlayState& State, const FName& ChartId);
        /** Close the standalone Slate window for the overlay. */
        void CloseOverlayWindow(FImPlotOverlayState& State);
        /** Register the overlay window with the widget subsystem. */
        void RegisterMoveableWindowActivity(FImPlotOverlayState& State);
        /** Unregister the overlay window from the widget subsystem. */
        void UnregisterMoveableWindowActivity(FImPlotOverlayState& State);
        /** Handle window close events to keep subsystem state in sync. */
        void HandleWindowClosed(const TSharedRef<SWindow>& ClosedWindow, FName ChartId);
        /** Invalidate the overlay to refresh its rendering. */
        void InvalidateOverlay(const FName& ChartId) const;
        void EnsureOverlayContext(FImPlotOverlayState& State);
        void DestroyOverlayContext(FImPlotOverlayState& State);
        void EnsureSharedFontAtlas(float InDpiScale = 1.0f);
        bool TryGetNearestPointForChart(const FName& ChartId, double TimeSeconds, FVector2D& OutPoint) const;
        void RenderDrawData(const ImDrawData* DrawData, const FVector2f& WindowOffset,
                float DpiScale, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

        /**
         * True only while ServicePendingImageCopy is re-rendering a chart offscreen. PaintOverlayForChart
         * reads it to leave the copy buttons out of the captured image and to size ImGui from the allotted
         * geometry instead of the live window.
         */
        bool bCapturingForImageCopy = false;

        /** Reused across captures: sized on demand, kept alive by UPROPERTY rather than left to GC. */
        UPROPERTY(Transient)
        TObjectPtr<UTextureRenderTarget2D> CaptureRenderTarget;

        /** Gamma flag CaptureRenderTarget was built with, so moving the console lever rebuilds it. */
        bool bCaptureTargetLinearGamma = true;

        ImFontAtlas* SharedFontAtlas = nullptr;
        TSharedPtr<FSlateDynamicImageBrush> SharedFontBrush;
        FName SharedFontTextureName;
        uint64 SharedFontTextureId = 0;
        /** DPI scale the shared atlas glyphs were last baked at. Glyphs rasterize at 13px * scale
         *  and draw at 13 logical units (style.FontScaleMain = 1/scale); the existing vertex upscale
         *  then maps them 1:1 to physical pixels — crisp at any OS scaling instead of bitmap-stretched. */
        float SharedFontAtlasDpiScale = 1.0f;

};

