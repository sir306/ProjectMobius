// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

/**
 * Title bar center content for the ImPlot window.
 */
class MOBIUSWIDGETS_API SImPlotWindowTitleBarWidget final : public SCompoundWidget
{
public:
        /** Slate arguments for SImPlotWindowTitleBarWidget. */
        SLATE_BEGIN_ARGS(SImPlotWindowTitleBarWidget)
                : _TitleText(FText::GetEmpty())
                , _TitleTextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
        {
        }

                /** Title text shown in the bar. */
                SLATE_ATTRIBUTE(FText, TitleText)

                /** Style for the title text. */
                SLATE_STYLE_ARGUMENT(FTextBlockStyle, TitleTextStyle)
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

private:
        TSharedPtr<STextBlock> TitleTextBlock;
};

