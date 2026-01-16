// Fill out your copyright notice in the Description page of Project Settings.

#include "Slate/Components/SImPlotOverlay.h"
#include "ImPlot/ImPlotVisualizationSubsystem.h"
#include "Widgets/SNullWidget.h"

SImPlotOverlay::SImPlotOverlay()
{
}

void SImPlotOverlay::Construct(const FArguments& InArgs)
{
        Subsystem = InArgs._Subsystem;
        ChartId = InArgs._ChartId;
        OnRequestClose = InArgs._OnRequestClose;

        ChildSlot
        [
                SNullWidget::NullWidget
        ];
}

int32 SImPlotOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
                              FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
                              bool bParentEnabled) const
{
        if (!Subsystem.IsValid())
        {
                return LayerId;
        }

        return Subsystem->PaintOverlayForChart(ChartId, Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId,
                InWidgetStyle, bParentEnabled, AsShared(), OnRequestClose);
}
