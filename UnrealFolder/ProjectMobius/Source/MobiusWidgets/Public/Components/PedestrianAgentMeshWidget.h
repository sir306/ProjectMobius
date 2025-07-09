// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Slate/SMeshWidget.h"
#include "Components/PedestrianAgentMeshWidget.h"

class UAgentInfoDisplay;
/**
 * 
 */
class MOBIUSWIDGETS_API SPedestrianAgentMeshWidget final : public SMeshWidget
{
public:
	SLATE_BEGIN_ARGS(SPedestrianAgentMeshWidget)
		:
		_Text()
	{
 
	}
		SLATE_ATTRIBUTE( FText, Text )
	SLATE_END_ARGS()

	 void Construct(const FArguments& InArgs, UAgentInfoDisplay& InThis);

protected:
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	UAgentInfoDisplay* ParentWidget = nullptr;
};
