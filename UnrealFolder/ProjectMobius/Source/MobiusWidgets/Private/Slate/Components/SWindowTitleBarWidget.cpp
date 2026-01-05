// Fill out your copyright notice in the Description page of Project Settings.

#include "Slate/Components/SWindowTitleBarWidget.h"
#include "Framework/Application/SWindowTitleBar.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

SWindowTitleBarWidget::SWindowTitleBarWidget()
{
}

SWindowTitleBarWidget::~SWindowTitleBarWidget()
{
}

void SWindowTitleBarWidget::Construct(const FArguments& InArgs)
{
	WindowStyle = *InArgs._WindowStyle;

	SAssignNew(TitleTextBlock, STextBlock)
		.Text(InArgs._TitleText)
		.TextStyle(InArgs._TitleTextStyle);

	if (!InArgs._OwnerWindow.IsValid())
	{
		ChildSlot
		[
			SNullWidget::NullWidget
		];
		return;
	}

	ChildSlot
	[
		SAssignNew(TitleBarWidget, SWindowTitleBar, InArgs._OwnerWindow.ToSharedRef(), TitleTextBlock, InArgs._TitleAlignment)
			.Style(&WindowStyle)
			.ShowAppIcon(InArgs._ShowAppIcon)
	];
}

void SWindowTitleBarWidget::SetTitleText(const FText& InTitleText)
{
	if (TitleTextBlock.IsValid())
	{
		TitleTextBlock->SetText(InTitleText);
	}
}

TSharedPtr<IWindowTitleBar> SWindowTitleBarWidget::GetTitleBar() const
{
	return TitleBarWidget;
}
