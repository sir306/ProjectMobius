// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/CustomSlateComponents/SReversibleUniformGridPanel.h"

void SReversibleUniformGridPanel::SetReverseOrder(bool bInReverse)
{
	const bool bOld = ReverseOrder.Get(false);
	ReverseOrder.Set(bInReverse);
	if (bOld != bInReverse)
	{
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

void SReversibleUniformGridPanel::SetReverseOrder(TAttribute<bool> InAttr)
{
	const bool bOld = ReverseOrder.Get(false);
	ReverseOrder = MoveTemp(InAttr);
	const bool bNew = ReverseOrder.Get(false);
	if (bOld != bNew)
	{
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

void SReversibleUniformGridPanel::OnArrangeChildren(
	const FGeometry& AllottedGeometry,
	FArrangedChildren& ArrangedChildren) const
{
	// Let the base class do its thing. No mirroring here anymore.
	SUniformGridPanel::OnArrangeChildren(AllottedGeometry, ArrangedChildren);
}