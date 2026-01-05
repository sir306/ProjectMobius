// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Slate/SMeshWidget.h"
#include "EnumsAndStructs/AgentMeshViewer.h"

class UAgentInfoDisplay;
/**
 * 
 */
class MOBIUSWIDGETS_API SAgentFollowIndicator final : public SMeshWidget
{
public:
	SLATE_BEGIN_ARGS(SAgentFollowIndicator) :
		_FollowIndicator(true)
		{
 
		}
		SLATE_ATTRIBUTE(bool, FollowIndicator)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UAgentInfoDisplay& InThis);

protected:
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	                      FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
	                      bool bParentEnabled) const override;
	
	void CreateRenderMeshData(const FAgentMeshViewer& PedestrianAgentData, const FGeometry& AllottedGeometry, FSlateInstanceBufferData& PerInstanceUpdate) const;
	
	UAgentInfoDisplay* ParentWidget = nullptr;
	bool FollowIndicator = true; // Flag to determine if this is a follow indicator or not, used for different rendering logic

	UTexture2D* IconTexture;
};
