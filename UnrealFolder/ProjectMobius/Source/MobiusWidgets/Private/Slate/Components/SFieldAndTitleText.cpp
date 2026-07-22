//copyright
#include "Slate/Components/SFieldAndTitleText.h"

#include "Fonts/FontMeasure.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/Font.h" // BW3/D69: LoadObject<UFont> in SetFieldFontFace

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

	TitleTextBlock->SetAutoWrapText(InArgs._TitleAutoWrapText.Get());
	FieldTextBlock->SetAutoWrapText(InArgs._FieldAutoWrapText.Get());

	TitleTextBlock->SetTextStyle(InArgs._TitleTextStyle);
	FieldTextBlock->SetTextStyle(InArgs._FieldTextStyle);

	TSharedRef<SWidget> TitleWidget = SNew(SBox)
		.Padding(InArgs._TitlePadding.Get())
		[
			TitleTextBlock.ToSharedRef()
		];
	TSharedRef<SWidget> FieldWidget = SNew(SBox)
		.Padding(InArgs._FieldPadding.Get())
		[
			FieldTextBlock.ToSharedRef()
		];

	if (!bVerticalStacking)
	{
		if (bAutoCenterTextToWidget)
		{
			// --- Centered as a unit: [fill][title(auto)][field(auto)][fill] ---
			GridPanel->AddSlot(0, 0)[ SNullWidget::NullWidget ]; // left spacer
			GridPanel->AddSlot(1, 0)
			         .HAlign(HAlign_Fill).VAlign(VAlign_Center)
			         .Padding(FMargin(5.f, 0.f, 2.f, 0.f))
			[ TitleWidget ];
			GridPanel->AddSlot(2, 0)
			         .HAlign(HAlign_Fill).VAlign(VAlign_Center)
			         .Padding(FMargin(2.f, 0.f, 5.f, 0.f))
			[ FieldWidget ];
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
				[ TitleWidget ];
			GridPanel->AddSlot(1, 0)
			         .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			         .Padding(FMargin(5.f, 0.f, 5.f, 0.f))
				[ FieldWidget ];

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
				[ TitleWidget ]
				// FieldWidget slot was set to HAlign(HAlign_Center), 
				// but that caused issues with auto-wrapping text, it stopped it from being unwrapped when we made error log boxes small to wider
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill)
				[ FieldWidget ]
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
				[ TitleWidget ];
			GridPanel->AddSlot(0, 1)
			         .HAlign(HAlign_Center).VAlign(VAlign_Top)
				[ FieldWidget ];

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

void SFieldAndTitleText::SetTextColors(const FSlateColor& InTitleColor, const FSlateColor& InFieldColor)
{
	// COLOUR-only reland (SetColorAndOpacity Assigns + Invalidate(Paint)); font/size/face untouched so the
	// numeric-field Mono face and the OnPaint shrink-to-fit are preserved across a theme toggle.
	if (TitleTextBlock.IsValid())
	{
		TitleTextBlock->SetColorAndOpacity(InTitleColor);
	}
	if (FieldTextBlock.IsValid())
	{
		FieldTextBlock->SetColorAndOpacity(InFieldColor);
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

void SFieldAndTitleText::SetFieldFontFace(FName InTypeface)
{
	if (!FieldTextBlock.IsValid())
	{
		return;
	}
	FSlateFontInfo FontInfo = FieldTextBlock->GetFont();
	// Ensure the composite Font_Inter is the font object so face names (Regular/Mono/...) resolve.
	if (UFont* Inter = LoadObject<UFont>(nullptr, TEXT("/Game/01_Dev/Widgets/Fonts/Font_Inter.Font_Inter")))
	{
		FontInfo.FontObject = Inter;
	}
	FontInfo.TypefaceFontName = InTypeface;
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
	// Shrink-to-fit: in-world flow counters allot each section a fixed cell; long title+value pairs
	// overflowed the cell (and the widget's card) and overlapped their neighbours. When the measured
	// text exceeds the allotted box, binary-search the largest font size that fits and push it via
	// SetFontSize. Shrink-only (no stored base size, so no grow-back / no oscillation); a paint pass
	// where the text already fits costs one measure.
	if (TitleTextBlock.IsValid() && FieldTextBlock.IsValid())
	{
		const FVector2D Allotted = AllottedGeometry.GetLocalSize();
		if (!Allotted.IsNearlyZero())
		{
			// Slot/box padding inside the internal grid (5+2 / 2+5 horizontal) — leave headroom.
			const FVector2D FitBox(Allotted.X * 0.94f, Allotted.Y);
			const float CurrentSize = FMath::Max(TitleTextBlock->GetFont().Size, FieldTextBlock->GetFont().Size);

			const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			auto MeasureAt = [&](const float FontSize) -> FVector2D
			{
				FSlateFontInfo TitleFont = TitleTextBlock->GetFont();
				TitleFont.Size = FontSize;
				FSlateFontInfo FieldFont = FieldTextBlock->GetFont();
				FieldFont.Size = FontSize;
				const FVector2D TitleSize = Measure->Measure(TitleTextBlock->GetText(), TitleFont);
				const FVector2D FieldSize = Measure->Measure(FieldTextBlock->GetText(), FieldFont);
				return bVerticalStacking
					? FVector2D(FMath::Max(TitleSize.X, FieldSize.X), TitleSize.Y + FieldSize.Y)
					: FVector2D(TitleSize.X + FieldSize.X, FMath::Max(TitleSize.Y, FieldSize.Y));
			};

			const FVector2D CurrentMeasured = MeasureAt(CurrentSize);
			if (CurrentMeasured.X > FitBox.X || CurrentMeasured.Y > FitBox.Y)
			{
				constexpr int32 MinFontSize = 6;
				int32 Low = MinFontSize;
				int32 High = FMath::Max(MinFontSize, static_cast<int32>(CurrentSize) - 1);
				while (Low < High)
				{
					const int32 Mid = (Low + High + 1) / 2;
					const FVector2D Size = MeasureAt(static_cast<float>(Mid));
					if (Size.X <= FitBox.X && Size.Y <= FitBox.Y) { Low = Mid; } else { High = Mid - 1; }
				}
				if (static_cast<float>(Low) < CurrentSize)
				{
					SetFontSize(static_cast<float>(Low));
				}
			}
		}
	}

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
