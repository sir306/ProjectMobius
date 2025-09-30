// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EnumsAndStructs/HelperStructs.h"

class UUniformGridPanel;
class UWidgetComponent;
class UWidget;
class UComboBoxString;
class UFieldAndTextWidget;
/**
 * 
 */
class MOBIUSWIDGETS_API WidgetUtilHelpers
{
public:
	WidgetUtilHelpers();
	~WidgetUtilHelpers();

	/**
	 * Clears all options in the provided combo string box
	 *
	 * @param[UComboBoxString] ComboBox The combo box to clear options from
	 */
	static void ClearComboBoxOptions(TObjectPtr<UComboBoxString> ComboBox);

	/**
	 * Populates a combo box with the provided options
	 *
	 * @param[UComboBoxString] ComboBox The combo box to populate
	 * @param[TArray<FString>] Options The options to populate the combo box with
	 * @param[FString] SelectedOption The option to select in the combo box
	 */
	static void UpdateComboBoxOptions(TObjectPtr<UComboBoxString> ComboBox, const TArray<FString>& Options, const FString& SelectedOption);

	/**
	 * Find if input string is a valid option in the combo box and if the flag to set it true then will set the combo box to that option
	 *
	 * @param[UComboBoxString] ComboBox The combo box to check
	 * @param[FString] Option The option to check for in the combo box
	 * @param[bool] bSetSelection If true, will set the combo box to the option if it exists
	 */
	static void FindAndSetComboBoxOption(TObjectPtr<UComboBoxString> ComboBox, const FString& Option, bool bSetSelection = true);

	/**
	 * Update widget text only if it differs from the new value
	 */
	static void UpdateTextIfChanged(UFieldAndTextWidget* Widget, const FText& NewText);

	/**
	 * Update widget text with a number only if it differs from the new value
	 */
	static void UpdateNumberIfChanged(UFieldAndTextWidget* Widget, int32 NewNumber);

	/**
	 * Update widget text with a float formatted to two decimals only if different
	 */
	static void UpdateFloatIfChanged(UFieldAndTextWidget* Widget, float NewFloat);

	/**
	 * Update widget text with a vector formatted to two decimals only if different
	 */
	static void UpdateVectorIfChanged(UFieldAndTextWidget* Widget, const FVector& Vec);

	
	/**
	 * Set alignment for a FieldAndTextWidget's grid slot
	 *
	 * @param[UFieldAndTextWidget] Widget Widget whose slot alignment will be set
	 * @param[EHorizontalAlignment] HAlign Desired horizontal alignment
	 * @param[EVerticalAlignment] VAlign Desired vertical alignment
	 */
	static void SetGridSlotAlignment(UWidget* Widget, EHorizontalAlignment HAlign, EVerticalAlignment VAlign);

	/** */
	static FUniformGridLayout ComputeUniformGridLayout(
		const FVector2D& DrawSizePx,
		int32 NumItems,
		const FVector2D& MinCellPx,
		int32 PreferredColsHint = 0);

	/**
	 * Given a chosen layout, apply slot positions in a UniformGrid, add children if needed.
	 * (Does not create the actual child widgets; just positions an existing array.)
	 */
	static void ApplyUniformGridLayout(
		UUniformGridPanel* Grid,
		const TArray<UWidget*>& Children,
		const FUniformGridLayout& Layout);

	/** Make child fill its grid cell (H/V Fill and no padding). */
	static void UniformGridFillCell(UWidget* Child);

	/**
	 * Distance-adaptive scaling so a WidgetComponent maintains a *target on-screen pixel height*
	 * independent of distance (approximate; assumes perspective projection).
	 *
	 * @param DesiredScreenHeightPx  e.g., 320 px tall on screen
	 * @param ReferenceWorldHeightUU height in Unreal units the widget represents at scale=1 (e.g., 100uu)
	 * @param bClamp                 optional clamping
	 * @param MinScale, MaxScale     scale clamps to avoid absurd sizes
	 */
	static void UpdateWidgetComponentScaleForScreenHeight(
		UWidgetComponent* WidgetComp,
		APlayerController* PC,
		float DesiredScreenHeightPx,
		float ReferenceWorldHeightUU,
		bool  bClamp = true,
		float MinScale = 0.25f,
		float MaxScale = 6.0f);

	/**
	 * Fast font fitting: find largest font size that fits a box (binary search).
	 * (You may already have this; included here for the grid workflow.)
	 */
	static int32 FindFittingFontSize(const FText& Text,
	                                 const struct FSlateFontInfo& BaseFont,
	                                 const FVector2D& BoxPx,
	                                 int32 MinSize,
	                                 int32 MaxSize,
	                                 float PaddingScale = 0.92f);

	/** */
	static void ApplyFontSize(class UTextBlock* TextBlock, int32 NewSize);

	static FORCEINLINE int32 CeilDiv(int32 A, int32 B) { return (A + (B - 1)) / B; }

	// Make a widget fill its parent (Canvas/Grid supported)
	static void FillParentSlot(UWidget* Widget);

	// Compute largest font size that fits a single FieldAndTextWidget into BoxPx
	static int32 FindFittingFontSizeForFieldAndText(
		class UFieldAndTextWidget* W,
		const FVector2D& BoxPx,
		int32 MinSize,
		int32 MaxSize,
		float PaddingScale = 0.92f);

	/** Measure combined Title+Field extents at a given font size (no UI mutation) */
	static FVector2D MeasureFieldAndTextAtSize(
		const FText& Title, const FText& Field,
		const FSlateFontInfo& TitleFontBase, const FSlateFontInfo& FieldFontBase,
		int32 SizePx, bool bVertical);
};
