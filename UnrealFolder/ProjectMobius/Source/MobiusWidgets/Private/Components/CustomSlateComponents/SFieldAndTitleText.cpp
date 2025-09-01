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
	// Variables/Attributes
	bVerticalStacking = InArgs._VerticalStacking.Get();
	bAutoCenterTextToWidget = InArgs._AutoCenterTextToWidget.Get();
	
	// Container
	TSharedRef<SGridPanel> GridPanel = SNew(SGridPanel);

	// Lambda to handle text alignment
	auto JustifyText = [this](bool VerticalStacking, bool AutoCenterTextToWidget , bool bTitle = true)
	{
		if (VerticalStacking)
		{
			return ETextJustify::Center;
		}
		else
		{
			if (AutoCenterTextToWidget)
			{
				if (bTitle)
				{
					return ETextJustify::Right;
				}
				else
				{
					return ETextJustify::Left;
				}
			}
			else
			{
				return ETextJustify::Left;
			}
		}
	};
	
	// Title Text
	SAssignNew(TitleTextBlock, STextBlock)
	.Text(InArgs._TitleText)
	.Justification(JustifyText(bVerticalStacking, bAutoCenterTextToWidget, true));
	
	// Field Text
	SAssignNew(FieldTextBlock, STextBlock)
	.Text(InArgs._FieldText)
	.Justification(JustifyText(bVerticalStacking, bAutoCenterTextToWidget, false));

	TitleTextBlock->SetTextStyle(InArgs._TitleTextStyle);
	FieldTextBlock->SetTextStyle(InArgs._FieldTextStyle);
	
	if (!bVerticalStacking)
	{
		if (bAutoCenterTextToWidget)
		{
			// --- Centered as a unit: [fill][title(auto)][field(auto)][fill] ---
			GridPanel->AddSlot(0, 0)[ SNullWidget::NullWidget ]; // left spacer
			GridPanel->AddSlot(1, 0)
			         .HAlign(HAlign_Fill).VAlign(VAlign_Center)
			         .Padding(FMargin(5.f, 0.f, 2.f, 0.f))
				[ TitleTextBlock.ToSharedRef() ];
			GridPanel->AddSlot(2, 0)
			         .HAlign(HAlign_Fill).VAlign(VAlign_Center)
			         .Padding(FMargin(2.f, 0.f, 5.f, 0.f))
				[ FieldTextBlock.ToSharedRef() ];
			GridPanel->AddSlot(3, 0)[ SNullWidget::NullWidget ]; // right spacer

			GridPanel->SetColumnFill(0, 1.f); // spacers eat leftover width equally
			GridPanel->SetColumnFill(1, 0.f); // auto
			GridPanel->SetColumnFill(2, 0.f); // auto
			GridPanel->SetColumnFill(3, 1.f);
			GridPanel->SetRowFill(0, 1.f);
		}
		else
		{
			// --- Your previous non-centered two-column behavior ---
			GridPanel->AddSlot(0, 0)
			         .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			         .Padding(FMargin(5.f, 0.f, 5.f, 0.f))
				[ TitleTextBlock.ToSharedRef() ];
			GridPanel->AddSlot(1, 0)
			         .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			         .Padding(FMargin(5.f, 0.f, 5.f, 0.f))
				[ FieldTextBlock.ToSharedRef() ];

			// keep your old fill weights (change to whatever you had)
			GridPanel->SetColumnFill(0, 0.5f);
			GridPanel->SetColumnFill(1, 0.5f);
			GridPanel->SetRowFill(0, 1.f);
		}
	}
	else
	{
		if (bAutoCenterTextToWidget)
		{
			// --- Vertical: center the stack with symmetric row spacers ---
			GridPanel->AddSlot(0, 0)[ SNullWidget::NullWidget ];   // top spacer
			GridPanel->AddSlot(0, 1)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[ TitleTextBlock.ToSharedRef() ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[ FieldTextBlock.ToSharedRef() ]
			];
			GridPanel->AddSlot(0, 2)[ SNullWidget::NullWidget ];   // bottom spacer

			GridPanel->SetRowFill(0, 1.f);
			GridPanel->SetRowFill(1, 0.f); // auto
			GridPanel->SetRowFill(2, 1.f);
			GridPanel->SetColumnFill(0, 1.f);
		}
		else
		{
			// --- Your previous vertical behavior (two rows) ---
			GridPanel->AddSlot(0, 0)
			         .HAlign(HAlign_Center).VAlign(VAlign_Bottom)
				[ TitleTextBlock.ToSharedRef() ];
			GridPanel->AddSlot(0, 1)
			         .HAlign(HAlign_Center).VAlign(VAlign_Top)
				[ FieldTextBlock.ToSharedRef() ];

			GridPanel->SetRowFill(0, 0.5f);
			GridPanel->SetRowFill(1, 0.5f);
			GridPanel->SetColumnFill(0, 1.f);
		}
	}

	ChildSlot [ GridPanel ];
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
