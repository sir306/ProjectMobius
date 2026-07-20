// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "VerticalTextBlock.generated.h"

class SRotatedText;
class STextBlock;
class USlateWidgetStyleAsset;

UENUM(BlueprintType)
enum class EVerticalTextMode : uint8
{
	/** Upright glyphs stacked one per line, reading top-to-bottom (classic vertical signage). */
	StackedUpright,
	/** Whole line rotated; reading direction set by RotationDegrees. */
	Rotated
};

/**
 * UMG vertical text: either upright glyphs stacked per line (default — matches the tool-panel tab
 * look) or a whole line rotated ±90° that occupies its rotated size in layout. Replaces labels
 * previously faked with hand-typed one-letter-per-line strings inside ScaleBoxes (which paired
 * letters and blurred at DPI-scale changes).
 *
 * Style resolution matches ButtonWithText: an assigned SWS_* style asset wins, otherwise the
 * shared FMobiusStyle "Mobius.Text.Label" is used.
 */
UCLASS()
class MOBIUSWIDGETS_API UVerticalTextBlock : public UWidget
{
	GENERATED_BODY()

public:
	/** Text to display, reading along the vertical axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertical Text")
	FText Text;

	/** Optional FTextBlockStyle style asset; falls back to Mobius.Text.Label when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertical Text")
	TObjectPtr<USlateWidgetStyleAsset> TextStyle;

	/** Upright stacked letters (default) or rotated line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertical Text")
	EVerticalTextMode Mode = EVerticalTextMode::StackedUpright;

	/** Rotated mode only: -90 reads bottom-to-top; +90 reads top-to-bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertical Text", meta = (ClampMin = "-90.0", ClampMax = "90.0", EditCondition = "Mode == EVerticalTextMode::Rotated"))
	float RotationDegrees = -90.0f;

	UFUNCTION(BlueprintCallable, Category = "Vertical Text")
	void SetText(FText InText);

	/** Re-resolve the themed text style and re-land it on the live Slate label (theme-walk hook). */
	void RefreshThemedStyle();

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	/** "FLOOR STATS" -> "F\nL\nO\nO\nR\n\nS\n..." for stacked mode (space becomes a blank gap line). */
	static FText BuildStackedText(const FText& InText);

	void PushTextToSlate();

	TSharedPtr<SRotatedText> RotatedText;
	TSharedPtr<STextBlock> StackedText;
};
