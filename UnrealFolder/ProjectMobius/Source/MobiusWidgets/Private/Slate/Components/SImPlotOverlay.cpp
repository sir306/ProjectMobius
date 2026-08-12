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

FReply SImPlotOverlay::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
        LocalCursorPosition = FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));

        // Unhandled on purpose: this only observes. Claiming the move would take mouse capture and fight
        // the moveable window's drag.
        return FReply::Unhandled();
}

void SImPlotOverlay::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
        SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

        // Seed it here too - entering without a subsequent move would otherwise leave the stale position.
        LocalCursorPosition = FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));

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

        // Park it offscreen so the final frame below has nothing hovered.
        LocalCursorPosition = FVector2D(-FLT_MAX, -FLT_MAX);

        // One final frame: without it the last painted image keeps ImGui's hover highlight on whichever
        // button the cursor left from.
        Invalidate(EInvalidateWidget::Paint);
}

EActiveTimerReturnType SImPlotOverlay::RepaintWhileHovered(double InCurrentTime, float InDeltaTime)
{
        // Serviced here rather than in OnPaint because the capture flushes rendering commands and
        // re-renders this widget - neither is safe from inside a paint. A timer tick is an ordinary
        // game-thread callback, and the cursor is by definition still over the chart, which is where the
        // click that raised the request came from. No-op unless something was requested.
        if (Subsystem.IsValid())
        {
                Subsystem->ServicePendingImageCopy(ChartId);
        }

        Invalidate(EInvalidateWidget::Paint);
        return EActiveTimerReturnType::Continue;
}
