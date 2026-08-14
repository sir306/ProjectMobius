// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "VerticalTextBlock.generated.h"

class SRotatedText;
class STextBlock;
class USlateWidgetStyleAsset;
class UUIThemeSubsystem;

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

	/** Explicitly set the live label colour (e.g. a ribbon button's active/inactive tab-text override).
	 *  Direct SetColorAndOpacity — bypasses the STextBlock style-copy that SetTextStyle cannot re-land. */
	void SetThemedLabelColor(FLinearColor Color);

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void OnWidgetRebuilt() override;
	virtual void BeginDestroy() override;

	/**
	 * A5 (2026-07-28): bound to UUIThemeSubsystem::OnThemeChanged so a stand-alone vertical label follows a
	 * live toggle on its own. RefreshThemedStyle() was previously reachable ONLY from the value walk, so
	 * this widget would have stopped theming entirely when A6 deletes it. Skips labels owned by a ribbon
	 * button — see the .cpp for why that is an ownership rule and not an optimisation.
	 */
	UFUNCTION()
	void HandleThemeChanged();

private:
	/** "FLOOR STATS" -> "F\nL\nO\nO\nR\n\nS\n..." for stacked mode (space becomes a blank gap line). */
	static FText BuildStackedText(const FText& InText);

	void PushTextToSlate();

	TSharedPtr<SRotatedText> RotatedText;
	TSharedPtr<STextBlock> StackedText;

	/** Weak so a torn-down game instance cannot be kept alive through this label (A5 theme bind). */
	TWeakObjectPtr<UUIThemeSubsystem> CachedThemeSubsystem;
};
