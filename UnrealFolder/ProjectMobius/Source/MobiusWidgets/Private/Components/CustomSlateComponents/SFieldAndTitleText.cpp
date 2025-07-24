//copyright
#include "Components/CustomSlateComponents/SFieldAndTitleText.h"

#include "Fonts/FontMeasure.h"

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
		.Justification(InArgs._VerticalStacking.Get() ? ETextJustify::Center : ETextJustify::Left);
	
	// Field Text
	SAssignNew(FieldTextBlock, STextBlock)
		.Text(InArgs._FieldText)
		.Justification(InArgs._VerticalStacking.Get() ? ETextJustify::Center : ETextJustify::Left);

	TitleTextBlock->SetTextStyle(InArgs._TitleTextStyle);
	FieldTextBlock->SetTextStyle(InArgs._FieldTextStyle);
	bVerticalStacking = InArgs._VerticalStacking.Get();
	
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
		GridPanel->Slot(0,0).Padding(FMargin(5.0f, 0.0f, 5.0f, 0.0f));
		GridPanel->Slot(1,0).Padding(FMargin(5.0f, 0.0f, 5.0f, 0.0f));

		// Align the text blocks in the center
		GridPanel->Slot(0,0).HAlign(HAlign_Center);
		GridPanel->Slot(1,0).HAlign(HAlign_Center);
		GridPanel->Slot(0,0).VAlign(VAlign_Fill);
		GridPanel->Slot(1,0).VAlign(VAlign_Fill);
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

void SFieldAndTitleText::SetFontSize(float InFontSize) const
{
	// Each field and title uses the same font size, however we could make this more flexible in the future
	// to define the difference between the title and field font sizes we use a bold font for the title and a regular font for the field.
	
	// Set the font size for the title text block
	FSlateFontInfo FontInfo = TitleTextBlock->GetFont();
	FontInfo.Size = InFontSize;
	TitleTextBlock->SetFont(FontInfo);

	// Set the font size for the field text block
	FontInfo = FieldTextBlock->GetFont();
	FontInfo.Size = InFontSize;
	FieldTextBlock->SetFont(FontInfo);
}

FVector2D SFieldAndTitleText::GetTextSize() const
{
	FVector2D TitleTextSize(0.0f, 0.0f);
	FVector2D FieldTextSize(0.0f, 0.0f);
	if (TitleTextBlock.IsValid())
	{
		FSlateFontInfo FontInfo = TitleTextBlock->GetFont();
		TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		FVector2D TextSize = FontMeasureService->Measure(TitleTextBlock->GetText(), FontInfo);
		TitleTextSize = TextSize;
	}
	if (FieldTextBlock.IsValid())
	{
		FSlateFontInfo FontInfo = FieldTextBlock->GetFont();
		TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		FVector2D TextSize = FontMeasureService->Measure(FieldTextBlock->GetText(), FontInfo);
		FieldTextSize = TextSize;
	}

	FVector2D CombinedMeasurement(0.0f, 0.0f);
	if (bVerticalStacking) {
		CombinedMeasurement.X = FMath::Max(TitleTextSize.X, FieldTextSize.X);
		CombinedMeasurement.Y = TitleTextSize.Y + FieldTextSize.Y;
	} else {
		CombinedMeasurement.X = TitleTextSize.X + FieldTextSize.X;
		CombinedMeasurement.Y = FMath::Max(TitleTextSize.Y, FieldTextSize.Y);
	}

	// TODO: add in padding if needed
	
	return CombinedMeasurement;
}

int32 SFieldAndTitleText::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                                  const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
                                  const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle,
	                                bParentEnabled);
}

float SFieldAndTitleText::GetFontSize()
{
	float FontSize = 0.0f;
	if (TitleTextBlock.IsValid())
	{
		FSlateFontInfo FontInfo = TitleTextBlock->GetFont();
		FontSize = FontInfo.Size;
	}
	if (FieldTextBlock.IsValid())
	{
		FSlateFontInfo FontInfo = FieldTextBlock->GetFont();
		FontSize = FontInfo.Size;
	}
	return FontSize;
}
