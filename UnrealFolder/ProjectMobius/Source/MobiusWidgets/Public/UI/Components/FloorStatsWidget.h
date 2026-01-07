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

#pragma once

#include "CoreMinimal.h"
#include "UI/WidgetInterface.h"
#include "Blueprint/UserWidget.h"
#include "FloorStatsWidget.generated.h"

class UBaseButton;
class UTextBlock;
class UImPlotDataSubsystem;
/**
 *
 */
UCLASS()
class MOBIUSWIDGETS_API UFloorStatsWidget : public UUserWidget, public IWidgetInterface
{
	GENERATED_BODY()
	
#pragma region METHODS
public:
        /** Override to handle design-time setup. */
        virtual void NativePreConstruct() override;

        /** Override to bind runtime delegates and cache subsystems. */
        virtual void NativeConstruct() override;

        /** Override to unbind runtime delegates. */
        virtual void NativeDestruct() override;

        /**
         * Override to tick the widget during runtime.
         *
         * @param MyGeometry Cached geometry for this widget.
         * @param InDeltaTime Time elapsed since the last tick.
         */
        virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

        /** Override to keep design-time properties synchronized. */
        virtual void SynchronizeProperties() override;

	/**
	 * Update the agent count variable, is bound to the heatmap subsystem delegate
	 *
	 * @param[int32] InFloorNumber - The floor number to update
	 * @param[int32] AgentCount - The number of agents on the floor
	 */
        /**
         * Update the agent count variable, is bound to the heatmap subsystem delegate.
         *
         * @param InFloorNumber The floor number to update.
         * @param AgentCount The number of agents on the floor.
         */
        UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
        void UpdateFloorLiveStatCount(int32 InFloorNumber, int32 AgentCount);

        /** Build the floor label prefix text. */
        UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
        void BuildFloorText();

        /** Build and send the chart title for the ImPlot overlay. */
        void BuildImPlotChartTitle() const;

        /** Build and send axis settings for the ImPlot overlay. */
        void BuildImPlotAxisSetting();

        /** Build and send the plot points for the ImPlot overlay. */
        void BuildImPlotGraphData() const;

        /** Send all chart data to the ImPlot overlay. */
        UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
        void SendImPlotChartData();

        /** Toggle the ImPlot overlay window for the chart. */
        UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
        void ToggleImPlotOverlay();

        /** Build the cached chart data for immediate display in the ImPlot overlay. */
        UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
        void BuildDataForImPlotOverlay();

        /** Build and send the chart title for the overlay. */
        UE_DEPRECATED(5.5, "Use BuildImPlotChartTitle instead.")
        void BuildQtAppChartTitle() const;

        /** Build and send axis settings for the overlay. */
        UE_DEPRECATED(5.5, "Use BuildImPlotAxisSetting instead.")
        void BuildQtChartAxisSetting();

        /** Build and send the plot points for the overlay. */
        UE_DEPRECATED(5.5, "Use BuildImPlotGraphData instead.")
        void BuildQtChartGraphData() const;

        /** Send all chart data to the overlay. */
        UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget",
                meta = (DeprecatedFunction, DeprecationMessage = "Use SendImPlotChartData instead."))
        void SendQtAppChartData();

        /** Toggle the overlay window for the chart. */
        UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget",
                meta = (DeprecatedFunction, DeprecationMessage = "Use ToggleImPlotOverlay instead."))
        void LaunchCloseQtApp();

        /** Build the cached chart data for immediate display. */
        UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget",
                meta = (DeprecatedFunction, DeprecationMessage = "Use BuildDataForImPlotOverlay instead."))
        void BuildDataForInstantQtUI();

        /**
         * Update the current playback time for live data updates.
         *
         * @param CurrentTime The current simulation time in seconds.
         */
        UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
        void UpdateCurrentPlaybackTime(float CurrentTime);

        /** Update the live data sample sent to the overlay. */
        UFUNCTION(BlueprintCallable, Category = "Mobius|Widgets|FloorStatsWidget")
        void UpdateAgentLiveData();

        /**
         * Format the text for the floor stats label.
         *
         * @param Prefix The prefix label text.
         * @param Count The count to append.
         */
        static FText FormatTextForTextBlock(const FText& Prefix, int32 Count);
	
#pragma endregion METHODS

#pragma region PROPERTYS

        /** Pointer to the ImPlot data subsystem. */
        UPROPERTY()
        TObjectPtr<UImPlotDataSubsystem> ImPlotDataSubsystem;

        /** pointer to the time subsystem */
        UPROPERTY()
        TObjectPtr<class UTimeDilationSubSystem> TimeDilationSubSystem;

	/** Current Active agent text */
	UPROPERTY()
	FText FloorPrefixText;

	/** Current Active agent text block */
	UPROPERTY(meta= (BindWidget))
	TObjectPtr<UTextBlock> FloorTextBlock;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UBaseButton> CurrentFloorBtn;

	/** Floor Number */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Mobius|Widgets|FloorStatsWidget", Meta = (ExposeOnSpawn=true))
	int32 FloorNumber = -1;

	/** Current Live Agent Count */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Widgets|FloorStatsWidget")
	int32 CurrentLiveAgentCount = 0;

	/** Is this a between floor widget counter */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Mobius|Widgets|FloorStatsWidget", Meta = (ExposeOnSpawn=true))
	bool bIsBetweenFloorWidget = false;

private:
        /** Cached plot points for the in-engine ImPlot overlay. */
        TArray<FVector2D> ImPlotPoints;

	/** The min number of agents to send to the Qt UI */
	int32 MinAgentCountToSend = 0;
	
	int32 LastSentTimeInt = -1;
	int32 LastSentCount = -1;

	bool bCheckingSameData = false;
	
#pragma endregion PROPERTYS
	
};
