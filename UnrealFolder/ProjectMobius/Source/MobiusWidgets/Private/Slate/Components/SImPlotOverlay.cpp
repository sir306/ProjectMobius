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

void SImPlotOverlay::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
        SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

        // Enter/leave rather than a permanent timer: the cost is only paid while the user is actually
        // pointing at a chart, and it needs no mouse capture, so Slate's routing is untouched (returning
        // Handled from OnMouseButtonDown here would fight the moveable window's drag).
        if (!RepaintTimerHandle.IsValid())
        {
                RepaintTimerHandle = RegisterActiveTimer(0.0f,
                        FWidgetActiveTimerDelegate::CreateSP(this, &SImPlotOverlay::RepaintWhileHovered));
        }
}

void SImPlotOverlay::OnMouseLeave(const FPointerEvent& MouseEvent)
{
        SCompoundWidget::OnMouseLeave(MouseEvent);

        if (const TSharedPtr<FActiveTimerHandle> Handle = RepaintTimerHandle.Pin())
        {
                UnRegisterActiveTimer(Handle.ToSharedRef());
        }
        RepaintTimerHandle.Reset();

        // One final frame: without it the last painted image keeps ImGui's hover highlight on whichever
        // button the cursor left from.
        Invalidate(EInvalidateWidget::Paint);
}

EActiveTimerReturnType SImPlotOverlay::RepaintWhileHovered(double InCurrentTime, float InDeltaTime)
{
        Invalidate(EInvalidateWidget::Paint);
        return EActiveTimerReturnType::Continue;
}
