// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UImPlotVisualizationSubsystem;
struct ImGuiContext;
struct ImPlotContext;
struct ImDrawData;
struct FSlateDynamicImageBrush;

/**
 * Slate overlay that renders ImPlot output.
 */
class SImPlotOverlay final : public SCompoundWidget
{
public:
        SLATE_BEGIN_ARGS(SImPlotOverlay)
                : _Subsystem()
                , _OnRequestClose()
        {
        }
        /** Owning subsystem used for data access. */
        SLATE_ARGUMENT(TWeakObjectPtr<UImPlotVisualizationSubsystem>, Subsystem)
        /** Delegate invoked when the overlay window is closed. */
        SLATE_EVENT(FSimpleDelegate, OnRequestClose)
        SLATE_END_ARGS()

	/** Default constructor. */
	SImPlotOverlay();

	/** Destructor. */
	~SImPlotOverlay();

        /**
         * Construct the overlay widget.
         * @param InArgs Slate argument data.
         */
        void Construct(const FArguments& InArgs);

        /**
         * Reset the ImPlot window open state so it can be shown again.
         */
        void ResetWindowState();

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
        /**
         * Find the plot point nearest to the requested time.
         * @param TimeSeconds Time value in seconds.
         * @param OutPoint Nearest point result.
         * @return True if a point was found.
         */
        bool TryGetNearestPoint(double TimeSeconds, FVector2D& OutPoint) const;

        void BuildFontAtlas();
        void RenderDrawData(const ImDrawData* DrawData, const FVector2f& WindowOffset,
                            FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

private:
        TWeakObjectPtr<UImPlotVisualizationSubsystem> Subsystem;
        FSimpleDelegate OnRequestClose;
        mutable ImGuiContext* ImGuiContext = nullptr;
        mutable ImPlotContext* ImPlotContext = nullptr;
        TSharedPtr<FSlateDynamicImageBrush> FontBrush;
        FSlateResourceHandle FontResourceHandle;
        FName FontTextureName;
        uint64 FontTextureId = 0;
        mutable bool bWindowOpen = true;
};
