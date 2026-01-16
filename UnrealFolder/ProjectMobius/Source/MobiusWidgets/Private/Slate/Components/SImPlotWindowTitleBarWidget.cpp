// Fill out your copyright notice in the Description page of Project Settings.

#include "Slate/Components/SImPlotWindowTitleBarWidget.h"
#include "Widgets/Text/STextBlock.h"

SImPlotWindowTitleBarWidget::SImPlotWindowTitleBarWidget()
{
}

SImPlotWindowTitleBarWidget::~SImPlotWindowTitleBarWidget()
{
}

void SImPlotWindowTitleBarWidget::Construct(const FArguments& InArgs)
{
	SAssignNew(TitleTextBlock, STextBlock)
		.Text(InArgs._TitleText)
		.TextStyle(InArgs._TitleTextStyle);

	ChildSlot
	[
		TitleTextBlock.ToSharedRef()
	];
}

void SImPlotWindowTitleBarWidget::SetTitleText(const FText& InTitleText)
{
	if (TitleTextBlock.IsValid())
	{
		TitleTextBlock->SetText(InTitleText);
	}
}

