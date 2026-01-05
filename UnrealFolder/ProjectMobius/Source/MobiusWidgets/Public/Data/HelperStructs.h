#pragma once
#include "CoreMinimal.h"
#include "HelperStructs.generated.h"

struct FUniformGridLayout
{
public:
	int32 Columns = 1;
	int32 Rows = 1;
	FVector2D CellPx = FVector2D(0.0f, 0.0f);
	bool bNeedsHorizontalScroll = false;
};

/**
 * Parameters the parent passes when adding this widget.
 * Lets us size/align/fill and compute fonts immediately (no popping).
 */
USTRUCT(BlueprintType)
struct FFlowSectionCounterInitParams
{
	GENERATED_BODY()

	/** Absolute pixel area this widget should fill (parent computes this). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowSectionCounter")
	FVector2D AllocatedSize = FVector2D(0, 0);

	/** Optional padding inside AllocatedSize for text layout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowSectionCounter")
	FMargin InnerPadding = FMargin(0);

	/** Title & Value content (or leave empty if the widget already has content). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowSectionCounter")
	FText TitleText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowSectionCounter")
	FText ValueText;

	/** Split of vertical space title:value (0..1). E.g. 0.55 = 55% title, 45% value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowSectionCounter")
	float TitleFraction = 0.55f;

	/** Font search bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowSectionCounter")
	int32 MinFontSize = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowSectionCounter")
	int32 MaxFontSize = 96;

	/** Safety scaling inside the box so text isn’t flush with edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowSectionCounter")
	float FitPaddingScale = 0.92f;

	/** If true, we’ll set slot rules to fill the parent (Canvas/Grid). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowSectionCounter")
	bool bFillParentSlot = true;
};