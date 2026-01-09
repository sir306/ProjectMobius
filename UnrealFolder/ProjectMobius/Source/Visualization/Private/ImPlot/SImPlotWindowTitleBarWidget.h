// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class IWindowTitleBar;
class STextBlock;
class SWindow;
class SWindowTitleBar;

/**
 * Simple title bar wrapper for the ImPlot window.
 */
class SImPlotWindowTitleBarWidget final : public SCompoundWidget
{
public:
        /** Slate arguments for SImPlotWindowTitleBarWidget. */
        SLATE_BEGIN_ARGS(SImPlotWindowTitleBarWidget)
                : _OwnerWindow()
                , _TitleText(FText::GetEmpty())
                , _TitleTextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
                , _WindowStyle(&FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window"))
                , _TitleAlignment(HAlign_Left)
                , _ShowAppIcon(false)
        {
        }

                /** Window that owns the title bar. */
                SLATE_ARGUMENT(TSharedPtr<SWindow>, OwnerWindow)

                /** Title text shown in the bar. */
                SLATE_ATTRIBUTE(FText, TitleText)

                /** Style for the title text. */
                SLATE_STYLE_ARGUMENT(FTextBlockStyle, TitleTextStyle)

                /** Window style to apply to the title bar. */
                SLATE_STYLE_ARGUMENT(FWindowStyle, WindowStyle)

                /** Alignment for the title text. */
                SLATE_ARGUMENT(EHorizontalAlignment, TitleAlignment)

                /** Whether to show the app icon in the title bar. */
                SLATE_ARGUMENT(bool, ShowAppIcon)
        SLATE_END_ARGS()

        /** Default constructor. */
        SImPlotWindowTitleBarWidget();

        /** Destructor. */
        ~SImPlotWindowTitleBarWidget();

        /**
         * Construct the title bar widget.
         * @param InArgs Slate argument data.
         */
        void Construct(const FArguments& InArgs);
        
        /**
         * Update the title text at runtime.
         * @param InTitleText New title text.
         */
        void SetTitleText(const FText& InTitleText);
        

        /**
         * Get the underlying title bar interface.
         * @return Title bar interface pointer.
         */
        TSharedPtr<IWindowTitleBar> GetTitleBar() const;

private:
        TSharedPtr<SWindowTitleBar> TitleBarWidget;
        TSharedPtr<STextBlock> TitleTextBlock;
        FWindowStyle WindowStyle;
};
