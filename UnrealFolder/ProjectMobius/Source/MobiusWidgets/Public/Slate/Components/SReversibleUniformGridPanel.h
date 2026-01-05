// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Layout/SUniformGridPanel.h"

/**
 * A UniformGridPanel that can reverse the visual column order within each row.
 * When bReverseOrder is true, columns are mirrored horizontally.
 */
class MOBIUSWIDGETS_API SReversibleUniformGridPanel : public SUniformGridPanel
{
public:
	// Reuse the base types so SNew(...) works with the same fluent API
	using FArguments = SUniformGridPanel::FArguments;
	using FSlot      = SUniformGridPanel::FSlot;
	using SUniformGridPanel::Slot;

	// --- Public API ----------------------------------------------------------
	/** Read current resolved value (evaluated now). */
	bool GetReverseOrder() const { return ReverseOrder.Get(false); }

	/** Set a literal value at runtime. Triggers a visual refresh if it changes. */
	void SetReverseOrder(bool bInReverse);

	/** Bind to a dynamic attribute (e.g., project settings, editor toggle). */
	void SetReverseOrder(TAttribute<bool> InAttr);
	
	/** 
	 * Arrange children, reversing column positions within each row when enabled.
	 * This creates a true horizontal mirror effect where column positions are flipped.
	 */
	virtual void OnArrangeChildren(
		const FGeometry& AllottedGeometry,
		FArrangedChildren& ArrangedChildren
	) const override;
	
private:
	/** Bindable flag; allows dynamic updates via attributes or direct setter. */
	TAttribute<bool> ReverseOrder;
	
	/** 
	 * Helper struct to track children by row for reversing.
	 * We group by Y position since we can't access slot data in const context.
	 */
	struct FRowInfo
	{
		TArray<int32> ChildIndices;
		int32 MaxColumn = 0;
	};
};