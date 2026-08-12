// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

/**
 * Single-line text rotated ±90° that participates in LAYOUT at its rotated size.
 *
 * A plain render-transform rotation on a TextBlock does not affect layout — the slot still
 * reserves the horizontal extent, which is why the tool-panel tabs previously faked vertical
 * text with one-letter-per-line strings. This widget reports its desired size with X/Y swapped
 * and arranges the inner text block (rotated about its center) to fill the allotted rect.
 */
class MOBIUSWIDGETS_API SRotatedText : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRotatedText)
		: _TextStyle(nullptr)
		, _RotationDegrees(-90.0f)
		{
		}
		SLATE_ATTRIBUTE(FText, Text)
		SLATE_ARGUMENT(const FTextBlockStyle*, TextStyle)
		/** -90 reads bottom-to-top (default, matches western vertical tab labels); +90 reads top-to-bottom. */
		SLATE_ARGUMENT(float, RotationDegrees)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetText(const TAttribute<FText>& InText);

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;

private:
	TSharedPtr<STextBlock> TextBlock;
};
