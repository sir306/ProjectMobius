// Fill out your copyright notice in the Description page of Project Settings.

#include "Slate/Components/SWindowContentPanel.h"
#include "Slate/Components/SFieldAndTitleText.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

SWindowContentPanel::SWindowContentPanel()
{
}

SWindowContentPanel::~SWindowContentPanel()
{
}

void SWindowContentPanel::Construct(const FArguments& InArgs)
{
	const bool bHasLocation = !InArgs._LocationText.Get().IsEmptyOrWhitespace();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(TitleMessageWidget, SFieldAndTitleText)
			.TitleText(InArgs._TitleText)
			.FieldText(InArgs._MessageText)
			.TitleTextStyle(InArgs._TitleTextStyle)
			.FieldTextStyle(InArgs._MessageTextStyle)
			.VerticalStacking(true)
			.AutoCenterTextToWidget(false)
			.TitleAutoWrapText(false)
			.FieldAutoWrapText(true)
			.FieldPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
		[
			SAssignNew(LocationContainer, SBox)
			.Visibility(bHasLocation ? EVisibility::Visible : EVisibility::Collapsed)
			[
				SAssignNew(LocationTextBlock, STextBlock)
				.Text(InArgs._LocationText)
				.TextStyle(InArgs._LocationTextStyle)
				.AutoWrapText(true)
			]
		]
	];
}

void SWindowContentPanel::SetTitleText(const FText& InTitleText)
{
	if (TitleMessageWidget.IsValid())
	{
		TitleMessageWidget->SetTitleText(InTitleText);
	}
}

void SWindowContentPanel::SetMessageText(const FText& InMessageText)
{
	if (TitleMessageWidget.IsValid())
	{
		TitleMessageWidget->SetFieldText(InMessageText);
	}
}

void SWindowContentPanel::SetLocationText(const FText& InLocationText)
{
	const bool bHasLocation = !InLocationText.IsEmptyOrWhitespace();

	if (LocationTextBlock.IsValid())
	{
		LocationTextBlock->SetText(InLocationText);
		LocationTextBlock->SetVisibility(bHasLocation ? EVisibility::Visible : EVisibility::Collapsed);
	}

	if (LocationContainer.IsValid())
	{
		LocationContainer->SetVisibility(bHasLocation ? EVisibility::Visible : EVisibility::Collapsed);
	}
}
