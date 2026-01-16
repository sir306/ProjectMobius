// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UImPlotVisualizationSubsystem;

/**
 * Slate overlay that renders ImPlot output.
 */
class MOBIUSWIDGETS_API SImPlotOverlay final : public SCompoundWidget
{
public:
        SLATE_BEGIN_ARGS(SImPlotOverlay)
                : _Subsystem()
                , _ChartId(NAME_None)
                , _OnRequestClose()
        {
        }
                /** Owning subsystem used for data access. */
                SLATE_ARGUMENT(TWeakObjectPtr<UImPlotVisualizationSubsystem>, Subsystem)

                /** Unique identifier for the overlay chart. */
                SLATE_ARGUMENT(FName, ChartId)
                /** Delegate invoked when the overlay window is closed. */
                SLATE_EVENT(FSimpleDelegate, OnRequestClose)
        SLATE_END_ARGS()

        /** Default constructor. */
        SImPlotOverlay();

        /**
         * Construct the overlay widget.
         * @param InArgs Slate argument data.
         */
        void Construct(const FArguments& InArgs);

        /**
         * Paint the ImPlot overlay.
         * @param Args Paint args.
         * @param AllottedGeometry Geometry for this widget.
         * @param MyCullingRect Culling rectangle.
         * @param OutDrawElements Draw element list.
         * @param LayerId Starting layer ID.
         * @param InWidgetStyle Widget style.
         * @param bParentEnabled Whether the parent is enabled.
         * @return The maximum layer ID used.
         */
        virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
                              FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
                              bool bParentEnabled) const override;

private:
        TWeakObjectPtr<UImPlotVisualizationSubsystem> Subsystem;
        FName ChartId;
        FSimpleDelegate OnRequestClose;
};
