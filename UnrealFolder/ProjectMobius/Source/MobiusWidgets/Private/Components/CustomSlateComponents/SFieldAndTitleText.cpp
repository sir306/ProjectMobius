//copyright
#include "Components/CustomSlateComponents/SFieldAndTitleText.h"

SFieldAndTitleText::SFieldAndTitleText()
{
}

SFieldAndTitleText::~SFieldAndTitleText()
{
}

void SFieldAndTitleText::Construct(const FArguments& InArgs)
{
	// Container
	TSharedRef<SGridPanel> GridPanel = SNew(SGridPanel);
	
	// Title Text
	SAssignNew(TitleTextBlock, STextBlock)
		.Text(InArgs._TitleText)
		.Justification(ETextJustify::Center);
	
	// Field Text
	SAssignNew(FieldTextBlock, STextBlock)
		.Text(InArgs._FieldText)
		.Justification(ETextJustify::Center);

	TitleTextBlock->SetTextStyle(InArgs._TitleTextStyle);
	FieldTextBlock->SetTextStyle(InArgs._FieldTextStyle);
	
	if (InArgs._VerticalStacking.Get())
	{
		GridPanel->AddSlot(0, 0)
		[
			TitleTextBlock.ToSharedRef()
		];

		GridPanel->AddSlot(0, 1)
		[
			FieldTextBlock.ToSharedRef()
		];

		GridPanel->SetRowFill(0, 0.5f);
		GridPanel->SetRowFill(1, 0.5f);
		GridPanel->SetColumnFill(0, 1.0f);
		
		// Align the text blocks in the center
		GridPanel->Slot(0,0).HAlign(HAlign_Center);
		GridPanel->Slot(0,1).HAlign(HAlign_Center);
		GridPanel->Slot(0,0).VAlign(VAlign_Center);
		GridPanel->Slot(0,1).VAlign(VAlign_Center);
	}
	else
	{
		GridPanel->AddSlot(0, 0)
		[
			TitleTextBlock.ToSharedRef()
		];

		GridPanel->AddSlot(1, 0)
		[
			FieldTextBlock.ToSharedRef()
		];

		GridPanel->SetColumnFill(0, 0.5f);
		GridPanel->SetColumnFill(1, 0.5f);
		GridPanel->SetRowFill(0, 1.0f);

		// Align the text blocks in the center
		GridPanel->Slot(0,0).HAlign(HAlign_Center);
		GridPanel->Slot(1,0).HAlign(HAlign_Center);
		GridPanel->Slot(0,0).VAlign(VAlign_Center);
		GridPanel->Slot(1,0).VAlign(VAlign_Center);
	}
	
	ChildSlot
	[
		GridPanel
	];
}

void SFieldAndTitleText::SetTitleText(FText InTitleText)
{
	if (TitleTextBlock.IsValid())
	{
		TitleTextBlock->SetText(InTitleText);
	}
}

void SFieldAndTitleText::SetFieldText(FText InFieldText)
{
	if (FieldTextBlock.IsValid())
	{
		FieldTextBlock->SetText(InFieldText);
	}
}

int32 SFieldAndTitleText::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                                  const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
                                  const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle,
	                                bParentEnabled);
}
