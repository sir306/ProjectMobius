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

        /**
         * Start repainting while the cursor is inside the overlay.
         *
         * ImGui is IMMEDIATE mode with POLLED input: PaintOverlayForChart reads IO.MousePos and
         * IO.MouseDown[] out of FSlateApplication once per paint, and ImGui infers a click from the
         * MouseDown false->true->false transition ACROSS FRAMES. This widget is otherwise repainted only
         * when the subsystem invalidates it on a data change, so on a paused or static chart there are no
         * frames between the press and the release - ImGui never observes the transition and every
         * interactive element silently does nothing. That is invisible until something in the overlay
         * takes a click, which the S6 clipboard buttons are the first thing to do. Hover-only elements
         * (the existing nearest-point tooltip) have always had the same dependency.
         */
        virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

        /** Stop the hover repaint and paint once more, so ImGui can clear its hover/active state. */
        virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

        /**
         * Track the cursor in this widget's LOCAL space.
         *
         * Taking it here rather than polling FSlateApplication::GetCursorPos() in the paint is the whole
         * point. Slate geometry does not use one absolute space: the FGeometry handed to OnPaint is in
         * WINDOW space (which is why RenderDrawData names its offset WindowOffset), while GetCursorPos()
         * returns DESKTOP coordinates. Feeding one into the other's AbsoluteToLocal leaves an error equal
         * to the window's position on screen — correct only with the window against the top-left corner.
         * A pointer event and the geometry delivered with it are always in the same space, and the LOCAL
         * result is identical whichever space they came from, so this never has to be reasoned about.
         */
        virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

        /** Cursor position in this widget's local space, as of the last pointer event over it. */
        FVector2D GetLocalCursorPosition() const { return LocalCursorPosition; }

private:
        /** Active-timer body: one repaint per tick for as long as the cursor is inside. */
        EActiveTimerReturnType RepaintWhileHovered(double InCurrentTime, float InDeltaTime);

        TWeakObjectPtr<UImPlotVisualizationSubsystem> Subsystem;
        FName ChartId;
        FSimpleDelegate OnRequestClose;

        /** Non-null only while hovered. Weak because Slate owns the handle. */
        TWeakPtr<FActiveTimerHandle> RepaintTimerHandle;

        /** Last known cursor position in local space. Far offscreen until a pointer event says otherwise,
         *  so ImGui treats nothing as hovered rather than parking the cursor on the top-left control. */
        FVector2D LocalCursorPosition = FVector2D(-FLT_MAX, -FLT_MAX);
};
