// Fill out your copyright notice in the Description page of Project Settings.


#include "Util/WidgetUtilHelpers.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/GridSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridSlot.h"
#include "Components/WidgetComponent.h"
#include "Components/CustomSlateWidgets/FieldAndTextWidget.h"
#include "Fonts/FontMeasure.h"


UWidgetUtilHelpers::UWidgetUtilHelpers()
{
}

UWidgetUtilHelpers::~UWidgetUtilHelpers()
{
}

void UWidgetUtilHelpers::ClearComboBoxOptions(TObjectPtr<UComboBoxString> ComboBox)
{
	if (ComboBox)
	{
		ComboBox->ClearSelection();
		ComboBox->ClearOptions();
	}
}

void UWidgetUtilHelpers::UpdateComboBoxOptions(TObjectPtr<UComboBoxString> ComboBox, const TArray<FString>& Options,
                                              const FString& SelectedOption)
{
	if (ComboBox->IsValidLowLevel() && Options.Num() > 0)
	{
		ClearComboBoxOptions(ComboBox);
		
		// Add the options to the combo box
		for (const FString& Option : Options)
		{
			ComboBox->AddOption(Option);
		}

		if (ComboBox->FindOptionIndex(SelectedOption) != INDEX_NONE)
		{
			ComboBox->SetSelectedOption(SelectedOption);
		}
		else
		{
			// No option valid so no selection will be made
		}
	}
	else
	{
		
	}
}

void UWidgetUtilHelpers::FindAndSetComboBoxOption(TObjectPtr<UComboBoxString> ComboBox, const FString& Option,
                                                 bool bSetSelection)
{
	if (ComboBox->IsValidLowLevel() && ComboBox->FindOptionIndex(Option) != INDEX_NONE)
	{
		if (bSetSelection)
		{
			ComboBox->SetSelectedOption(Option);
		}
	}
}

void UWidgetUtilHelpers::UpdateTextIfChanged(UFieldAndTextWidget* Widget, const FText& NewText)
{
	if (!Widget) return;

	if (!Widget->FieldText.EqualTo(NewText))
	{
		Widget->SetUpdateFieldText(NewText);
	}
}

void UWidgetUtilHelpers::UpdateNumberIfChanged(UFieldAndTextWidget* Widget, int32 NewNumber)
{
	if (!Widget) return;

	FText NewText = FText::AsNumber(NewNumber);
	if (!Widget->FieldText.EqualTo(NewText))
	{
		Widget->SetUpdateFieldText(NewText);
	}
}

void UWidgetUtilHelpers::UpdateFloatIfChanged(UFieldAndTextWidget* Widget, float NewFloat)
{
	if (!Widget) return;

	FText NewText = FText::FromString(FString::Printf(TEXT("%.2f"), NewFloat));
	if (!Widget->FieldText.EqualTo(NewText))
	{
		Widget->SetUpdateFieldText(NewText);
	}
}

void UWidgetUtilHelpers::UpdateVectorIfChanged(UFieldAndTextWidget* Widget, const FVector& Vec)
{
	if (!Widget) return;

	FText NewText = FText::FromString(FString::Printf(TEXT("%.2f, %.2f, %.2f"), Vec.X, Vec.Y, Vec.Z));

	if (!Widget->FieldText.EqualTo(NewText))
	{
		Widget->SetUpdateFieldText(NewText);
	}
}

void UWidgetUtilHelpers::SetGridSlotAlignment(UWidget* Widget, EHorizontalAlignment HAlign,
                                             EVerticalAlignment VAlign)
{
	if (!Widget)
	{
		return;
	}

	if (UPanelSlot* Slot = Widget->Slot)
	{
		if (UGridSlot* GridSlot = Cast<UGridSlot>(Slot))
		{
			GridSlot->SetHorizontalAlignment(HAlign);
			GridSlot->SetVerticalAlignment(VAlign);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Unsupported slot type for text block %s"), *Widget->GetName());
		}
	}
}

FUniformGridLayout UWidgetUtilHelpers::ComputeUniformGridLayout(const FVector2D& DrawSizePx, int32 NumItems,
                                                               const FVector2D& MinCellPx, int32 PreferredColsHint)
{
	FUniformGridLayout Out;
	Out.Columns = 1;
	Out.Rows    = FMath::Max(NumItems, 1);

	if (NumItems <= 0 || DrawSizePx.X <= 0.f || DrawSizePx.Y <= 0.f)
	{
		Out.CellPx = DrawSizePx;
		return Out;
	}

	// Search a reasonable column count:
	// - start from hint if provided, else a sqrt layout
	const int32 SqrtCols = FMath::Clamp(static_cast<int32>(FMath::Sqrt((float)NumItems)), 1, NumItems);
	const int32 StartCols = PreferredColsHint > 0 ? PreferredColsHint : SqrtCols;

	int32 BestCols = StartCols;
	bool  bFoundNonScrolling = false;

	// Try a small band around StartCols (wider if many items).
	const int32 MaxTry = FMath::Clamp(NumItems, 1, 16);
	for (int32 Delta = 0; Delta <= MaxTry; ++Delta)
	{
		for (int Sign = -1; Sign <= 1; Sign += 2)
		{
			const int32 Cols = FMath::Clamp(StartCols + Sign * Delta, 1, NumItems);
			const int32 Rows = CeilDiv(NumItems, Cols);

			const FVector2D Cell(DrawSizePx.X / float(Cols), DrawSizePx.Y / float(Rows));
			const bool bFits = (Cell.X >= MinCellPx.X) && (Cell.Y >= MinCellPx.Y);

			if (bFits)
			{
				BestCols = Cols;
				bFoundNonScrolling = true;
				Out.Columns = BestCols;
				Out.Rows    = CeilDiv(NumItems, BestCols);
				Out.CellPx  = FVector2D(DrawSizePx.X / BestCols, DrawSizePx.Y / Out.Rows);
				Out.bNeedsHorizontalScroll = false;
				return Out; // early out on first feasible fit
			}
		}
	}

	// No feasible non-scrolling layout -> mark for horizontal scroll (dirty fix)
	Out.Columns = FMath::Clamp(StartCols, 1, NumItems);
	Out.Rows    = CeilDiv(NumItems, Out.Columns);
	Out.CellPx  = FVector2D(DrawSizePx.X / Out.Columns, DrawSizePx.Y / Out.Rows);
	Out.bNeedsHorizontalScroll = true;
	return Out;
}
//TODO: FIX THIS METHOD
void UWidgetUtilHelpers::ApplyUniformGridLayout(
	UUniformGridPanel* Grid,
	const TArray<UWidget*>& Children,
	const FUniformGridLayout& Layout)
{
	if (!Grid) return;

	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		if (UWidget* Child = Children[Index])
		{
			// TODO: this calculation is wrong
			const int32 Row = Index / Layout.Columns;
			const int32 Col = Index % Layout.Columns;

			// Ensure child is in the grid at the correct cell
			UUniformGridSlot* Slot = Cast<UUniformGridSlot>(Child->Slot);
			if (!Slot)
			{
				// If child wasn’t added yet, parent must add it to Grid before calling this.
				continue;
			}
			Slot->SetRow(Row);
			Slot->SetColumn(Col);
			UniformGridFillCell(Child);
		}
	}
}

void UWidgetUtilHelpers::UniformGridFillCell(UWidget* Child)
{
	if (!Child) return;
	if (UUniformGridSlot* S = Cast<UUniformGridSlot>(Child->Slot))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Fill);
		//S->SetPadding(FMargin(0.f));
	}
}

void UWidgetUtilHelpers::UpdateWidgetComponentScaleForScreenHeight(
	UWidgetComponent* WidgetComp,
	APlayerController* PC,
	float DesiredScreenHeightPx,
	float ReferenceWorldHeightUU,
	bool  bClamp,
	float MinScale,
	float MaxScale)
{
	if (!WidgetComp || !PC || DesiredScreenHeightPx <= 0.f || ReferenceWorldHeightUU <= 0.f)
		return;

	// Camera/FOV -> focal length in pixels
	int32 ViewX = 0, ViewY = 0;
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D ViewportSize = FVector2D(ViewX, ViewY);
		GEngine->GameViewport->GetViewportSize(ViewportSize);
		ViewX = static_cast<int32>(ViewportSize.X);
		ViewY = static_cast<int32>(ViewportSize.Y);
	}
	if (ViewY <= 0)
		return;

	const float FOVdeg  = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 90.f;
	const float FOVrad  = FMath::DegreesToRadians(FOVdeg);
	const float FocalPx = (0.5f * float(ViewY)) / FMath::Tan(0.5f * FOVrad);

	// Distance from camera to component origin
	const FVector CamLoc = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation()
		                       : PC->GetFocalLocation();
	const float Distance = FVector::Distance(CamLoc, WidgetComp->GetComponentLocation());
	if (Distance <= KINDA_SMALL_NUMBER)
		return;

	// For a planar widget of world "height" H_world, projected height on screen is:
	//   H_px = (H_world * FocalPx) / Distance
	// => To achieve DesiredScreenHeightPx, scale must be:
	//   Scale = (DesiredScreenHeightPx * Distance) / (FocalPx * ReferenceWorldHeightUU)
	float Scale = (DesiredScreenHeightPx * Distance) / (FocalPx * ReferenceWorldHeightUU);

	if (bClamp)
	{
		Scale = FMath::Clamp(Scale, MinScale, MaxScale);
	}

	// Uniform scale (X/Y/Z)
	WidgetComp->SetWorldScale3D(FVector(Scale));
}

int32 UWidgetUtilHelpers::FindFittingFontSize(const FText& Text,
                                             const FSlateFontInfo& BaseFont,
                                             const FVector2D& BoxPx,
                                             int32 MinSize,
                                             int32 MaxSize,
                                             float PaddingScale)
{
	if (BoxPx.X <= 0.f || BoxPx.Y <= 0.f || MaxSize < MinSize)
		return FMath::Max(MinSize, 0);

	TSharedPtr<FSlateFontMeasure> FontMeasure;
	if (FSlateApplication::IsInitialized())
	{
		FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	}
	if (!FontMeasure.IsValid())
		return FMath::Max(MinSize, 0);

	const FVector2D Target(BoxPx.X * PaddingScale, BoxPx.Y * PaddingScale);

	int32 L = MinSize, R = MaxSize, Best = MinSize;
	while (L <= R)
	{
		const int32 Mid = (L + R) / 2;
		FSlateFontInfo Test = BaseFont;
		Test.Size = Mid;

		const FVector2D Ext = FontMeasure->Measure(Text, Test);
		const bool bFits = (Ext.X <= Target.X) && (Ext.Y <= Target.Y);

		if (bFits) { Best = Mid; L = Mid + 1; }
		else       { R = Mid - 1; }
	}
	return Best;
}

void UWidgetUtilHelpers::ApplyFontSize(UTextBlock* TB, int32 NewSize)
{
	if (!TB) return;
	FSlateFontInfo F = TB->GetFont();
	F.Size = FMath::Max(NewSize, 1);
	TB->SetFont(F);
}

void UWidgetUtilHelpers::FillParentSlot(UWidget* Widget)
{
	if (!Widget) return;

	if (UPanelSlot* Slot = Widget->Slot)
	{
		if (auto* Canvas = Cast<UCanvasPanelSlot>(Slot))
		{
			Canvas->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			Canvas->SetOffsets(FMargin(0.f));
			Canvas->SetAlignment(FVector2D(0.f, 0.f));
			Canvas->SetAutoSize(false);
			return;
		}
		if (auto* Grid = Cast<UGridSlot>(Slot))
		{
			Grid->SetHorizontalAlignment(HAlign_Fill);
			Grid->SetVerticalAlignment(VAlign_Fill);
			Grid->SetPadding(FMargin(0.f));
			return;
		}
		if (auto* UniformGrid = Cast<UUniformGridSlot>(Slot))
		{
			UniformGrid->SetHorizontalAlignment(HAlign_Fill);
			UniformGrid->SetVerticalAlignment(VAlign_Fill);
			//UniformGrid->SetPadding(FMargin(0.f));
			return;
		}
		// add other slots if you need them later
	}
}

int32 UWidgetUtilHelpers::FindFittingFontSizeForFieldAndText(class UFieldAndTextWidget* W, const FVector2D& BoxPx,
	int32 MinSize, int32 MaxSize, float PaddingScale)
{
	if (!W || BoxPx.X <= 0.f || BoxPx.Y <= 0.f || MaxSize < MinSize)
		return FMath::Max(MinSize, 0);

	// Choose styles (or fallback to Core NormalText)
	const FTextBlockStyle& TitleStyle = (W->TitleTextStyle)
		? *W->TitleTextStyle->GetStyle<FTextBlockStyle>()
		: FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
	const FTextBlockStyle& FieldStyle = (W->FieldTextStyle)
		? *W->FieldTextStyle->GetStyle<FTextBlockStyle>()
		: FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");

	const FSlateFontInfo TitleBase = TitleStyle.Font;
	const FSlateFontInfo FieldBase = FieldStyle.Font;
	const bool bVertical = W->GetIsTitleAboveField();

	const FVector2D Target(BoxPx.X * PaddingScale, BoxPx.Y * PaddingScale);

	int32 L = MinSize, R = MaxSize, Best = MinSize;
	while (L <= R)
	{
		const int32 Mid = (L + R) / 2;
		const FVector2D Ext = MeasureFieldAndTextAtSize(
			W->GetTitleText(), W->GetFieldText(), TitleBase, FieldBase, Mid, bVertical);
		const bool bFits = (Ext.X <= Target.X) && (Ext.Y <= Target.Y);

		if (bFits) { Best = Mid; L = Mid + 1; }
		else       { R = Mid - 1; }
	}
	return Best;
}

FVector2D UWidgetUtilHelpers::MeasureFieldAndTextAtSize(const FText& Title, const FText& Field,
	const FSlateFontInfo& TitleFontBase, const FSlateFontInfo& FieldFontBase, int32 SizePx, bool bVertical)
{
	if (!FSlateApplication::IsInitialized())
		return FVector2D::ZeroVector;

	TSharedPtr<FSlateFontMeasure> Measure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	FSlateFontInfo TitleFont = TitleFontBase; TitleFont.Size = SizePx;
	FSlateFontInfo FieldFont = FieldFontBase; FieldFont.Size = SizePx;

	const FVector2D T = Measure->Measure(Title, TitleFont);
	const FVector2D F = Measure->Measure(Field, FieldFont);

	return bVertical
		? FVector2D(FMath::Max(T.X, F.X), T.Y + F.Y)
		: FVector2D(T.X + F.X, FMath::Max(T.Y, F.Y));
}

void UWidgetUtilHelpers::SortScrollBoxChildrenByFloorAndCounter(
	UScrollBox* ScrollBox,
	bool bDescendingFloor,
	bool bDescendingCounter)
{
	if (!ScrollBox)
	{
		return;
	}

	// Capture original children and preserve order for non-matching entries.
	const TArray<UWidget*> OriginalChildren = ScrollBox->GetAllChildren();

	struct FItem
	{
		UUserWidget* Widget = nullptr;
		int32 Floor = 0;
		int32 Counter = 0;
		int32 OriginalIndex = 0;
		bool bHasValidKey = false;
	};

	TArray<FItem> Items;
	Items.Reserve(OriginalChildren.Num());

	for (int32 Index = 0; Index < OriginalChildren.Num(); ++Index)
	{
		UWidget* Child = OriginalChildren[Index];

		FItem Item;
		Item.OriginalIndex = Index;
		Item.Widget = Cast<UUserWidget>(Child);

		if (Item.Widget)
		{
			Item.bHasValidKey = ExtractFloorAndCounterFromWidget(
				Item.Widget,
				Item.Floor,
				Item.Counter);
		}

		Items.Add(Item);
	}

	// Sort with two groups:
	//  1) Widgets with valid keys, sorted by Floor/Counter.
	//  2) Widgets without keys, kept in original relative order at the end.
	Items.Sort(
		[bDescendingFloor, bDescendingCounter](const FItem& A, const FItem& B)
		{
			// Non-valid always go after valids.
			if (A.bHasValidKey != B.bHasValidKey)
			{
				return A.bHasValidKey && !B.bHasValidKey;
			}

			// If neither has a valid key, keep original order stable.
			if (!A.bHasValidKey && !B.bHasValidKey)
			{
				return A.OriginalIndex < B.OriginalIndex;
			}

			// Both have valid keys: compare Floor
			if (A.Floor != B.Floor)
			{
				return bDescendingFloor
					? (A.Floor > B.Floor)
					: (A.Floor < B.Floor);
			}

			// Floors equal; compare Counter
			if (A.Counter != B.Counter)
			{
				return bDescendingCounter
					? (A.Counter > B.Counter)
					: (A.Counter < B.Counter);
			}

			// Full tie: fall back to original index for deterministic order
			return A.OriginalIndex < B.OriginalIndex;
		});

	// Rebuild the ScrollBox with new order
	ScrollBox->ClearChildren();

	for (const FItem& Item : Items)
	{
		if (UWidget* Child = OriginalChildren.IsValidIndex(Item.OriginalIndex)
			                     ? OriginalChildren[Item.OriginalIndex]
			                     : nullptr)
		{
			ScrollBox->AddChild(Child);
		}
	}
}

bool UWidgetUtilHelpers::TryParseFloorAndCounterFromText(
	const FString& InText,
	int32& OutFloor,
	int32& OutCounter)
{
	OutFloor = 0;
	OutCounter = 0;

	FString Trimmed = InText.TrimStartAndEnd();
	if (!Trimmed.StartsWith(TEXT("F")))
		return false;

	Trimmed.RightChopInline(1); // drop 'F'

	FString FloorStr, CounterPart;
	if (!Trimmed.Split(TEXT("Counter"), &FloorStr, &CounterPart))
		return false;

	FloorStr = FloorStr.TrimStartAndEnd();
	CounterPart = CounterPart.TrimStartAndEnd();

	if (!FloorStr.IsNumeric() || !CounterPart.IsNumeric())
		return false;

	OutFloor   = FCString::Atoi(*FloorStr);
	OutCounter = FCString::Atoi(*CounterPart);
	return true;
}

bool UWidgetUtilHelpers::ExtractFloorAndCounterFromWidget(
	UUserWidget* Widget,
	int32& OutFloor,
	int32& OutCounter)
{
	if (!Widget || !Widget->WidgetTree)
		return false;

	TArray<UWidget*> AllWidgets;
	Widget->WidgetTree->GetAllWidgets(AllWidgets);

	for (UWidget* ChildWidget : AllWidgets)
	{
		if (UTextBlock* TB = Cast<UTextBlock>(ChildWidget))
		{
			const FString Text = TB->GetText().ToString();
			if (TryParseFloorAndCounterFromText(Text, OutFloor, OutCounter))
			{
				return true; // stop at first match
			}
		}
	}

	return false;
}