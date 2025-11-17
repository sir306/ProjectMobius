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
		return;

	const TArray<UWidget*> OriginalChildren = ScrollBox->GetAllChildren();

	struct FItem
	{
		UUserWidget* Widget = nullptr;
		FFloorCounterKey Key;
		int32 OriginalIndex = 0;
	};

	TArray<FItem> Items;
	Items.Reserve(OriginalChildren.Num());

	for (int32 i = 0; i < OriginalChildren.Num(); ++i)
	{
		UUserWidget* UW = Cast<UUserWidget>(OriginalChildren[i]);
		FItem Item;
		Item.Widget = UW;
		Item.OriginalIndex = i;
		if (UW)
			Item.Key = ExtractFloorCounterFromWidget(UW, TEXT("FC_NameTextBlock"));
		Items.Add(Item);
	}

	Items.Sort([bDescendingFloor, bDescendingCounter](const FItem& A, const FItem& B)
	{
		const auto& KA = A.Key;
		const auto& KB = B.Key;

		if (KA.bHasValidKey != KB.bHasValidKey)
		{
			return KA.bHasValidKey && !KB.bHasValidKey;
		}

		if (!KA.bHasValidKey && !KB.bHasValidKey)
		{
			return A.OriginalIndex < B.OriginalIndex;
		}

		// 1) Compare floor low first
		if (KA.SortFloorLow != KB.SortFloorLow)
		{
			return bDescendingFloor
				       ? (KA.SortFloorLow > KB.SortFloorLow)
				       : (KA.SortFloorLow < KB.SortFloorLow);
		}

		// 2) Within the same low floor:
		//    Single-floor entries (F1) should come before ranges (F1~F2).
		const bool AIsRange = (KA.SortFloorLow != KA.SortFloorHigh);
		const bool BIsRange = (KB.SortFloorLow != KB.SortFloorHigh);

		if (AIsRange != BIsRange)
		{
			// false (single) < true (range)
			return !AIsRange && BIsRange;
		}

		// 3) (Optional) if you want to sort by high floor inside ranges:
		// if (KA.SortFloorHigh != KB.SortFloorHigh)
		// {
		//     return bDescendingFloor
		//         ? (KA.SortFloorHigh > KB.SortFloorHigh)
		//         : (KA.SortFloorHigh < KB.SortFloorHigh);
		// }

		// 4) Then compare counter
		if (KA.Counter != KB.Counter)
		{
			return bDescendingCounter
				       ? (KA.Counter > KB.Counter)
				       : (KA.Counter < KB.Counter);
		}

		// 5) Stable fallback
		return A.OriginalIndex < B.OriginalIndex;
	});

	ScrollBox->ClearChildren();
	for (const FItem& Item : Items)
	{
		if (Item.Widget)
			ScrollBox->AddChild(Item.Widget);
	}
}

int32 UWidgetUtilHelpers::SortScrollBoxChildrenByFloorAndCounterWithNewPos(UScrollBox* ScrollBox, bool bDescendingFloor,
                                                                           bool bDescendingCounter, UUserWidget* TargetWidget)
{
	if (!ScrollBox)
		return INDEX_NONE;

	const TArray<UWidget*> OriginalChildren = ScrollBox->GetAllChildren();

	struct FItem
	{
		UUserWidget* Widget = nullptr;
		FFloorCounterKey Key;
		int32 OriginalIndex = 0;
	};

	TArray<FItem> Items;
	Items.Reserve(OriginalChildren.Num());

	for (int32 i = 0; i < OriginalChildren.Num(); ++i)
	{
		UUserWidget* UW = Cast<UUserWidget>(OriginalChildren[i]);
		FItem Item;
		Item.Widget = UW;
		Item.OriginalIndex = i;
		if (UW)
			Item.Key = ExtractFloorCounterFromWidget(UW, TEXT("FC_NameTextBlock"));
		Items.Add(Item);
	}

	// ---- Sort with the range fix ----
	Items.Sort([bDescendingFloor, bDescendingCounter](const FItem& A, const FItem& B)
	{
		const auto& KA = A.Key;
		const auto& KB = B.Key;

		if (KA.bHasValidKey != KB.bHasValidKey)
			return KA.bHasValidKey && !KB.bHasValidKey;

		if (!KA.bHasValidKey && !KB.bHasValidKey)
			return A.OriginalIndex < B.OriginalIndex;

		if (KA.SortFloorLow != KB.SortFloorLow)
			return bDescendingFloor ? (KA.SortFloorLow > KB.SortFloorLow)
				       : (KA.SortFloorLow < KB.SortFloorLow);

		const bool AIsRange = (KA.SortFloorLow != KA.SortFloorHigh);
		const bool BIsRange = (KB.SortFloorLow != KB.SortFloorHigh);

		if (AIsRange != BIsRange)
			return !AIsRange && BIsRange; // single-floor first

		if (KA.Counter != KB.Counter)
			return bDescendingCounter ? (KA.Counter > KB.Counter)
				       : (KA.Counter < KB.Counter);

		return A.OriginalIndex < B.OriginalIndex;
	});

	// ---- Rebuild ScrollBox ----
	ScrollBox->ClearChildren();
	for (const FItem& Item : Items)
	{
		if (Item.Widget)
			ScrollBox->AddChild(Item.Widget);
	}

	// ---- Find new index of TargetWidget ----
	if (TargetWidget)
	{
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (Items[i].Widget == TargetWidget)
				return i; // return the new sorted index
		}
	}

	return INDEX_NONE;
}

int32 UWidgetUtilHelpers::GetWidgetIndexInScrollBox(UScrollBox* ScrollBox, UUserWidget* Target)
{
	if (!ScrollBox || !Target)
		return INDEX_NONE;

	const TArray<UWidget*> Children = ScrollBox->GetAllChildren();

	for (int32 i = 0; i < Children.Num(); ++i)
	{
		if (Children[i] == Target)
			return i;
	}

	return INDEX_NONE;
}

UWidgetUtilHelpers::FFloorCounterKey UWidgetUtilHelpers::ParseFloorCounterText(const FString& InText)
{
	FFloorCounterKey Out;
	FString Text = InText.TrimStartAndEnd();

	// Common split
	FString LeftPart, RightPart;
	if (!Text.Split(TEXT("Counter"), &LeftPart, &RightPart))
		return Out;

	LeftPart = LeftPart.TrimStartAndEnd();
	RightPart = RightPart.TrimStartAndEnd();

	if (!RightPart.IsNumeric())
		return Out;

	Out.Counter = FCString::Atoi(*RightPart);

	// Case 1: F3~F5
	if (LeftPart.StartsWith("F") && LeftPart.Contains("~F"))
	{
		FString BottomStr, TopStr;
		if (LeftPart.Split("~F", &BottomStr, &TopStr))
		{
			BottomStr.RightChopInline(1); // drop 'F' in front
			if (BottomStr.IsNumeric() && TopStr.IsNumeric())
			{
				Out.SortFloorLow  = FCString::Atoi(*BottomStr);
				Out.SortFloorHigh = FCString::Atoi(*TopStr);
				Out.bHasValidKey  = true;
				return Out;
			}
		}
	}

	// Case 2: F{num}
	if (LeftPart.StartsWith("F"))
	{
		const FString NumStr = LeftPart.Mid(1).TrimStartAndEnd();
		if (NumStr.IsNumeric())
		{
			const int32 Val = FCString::Atoi(*NumStr);
			Out.SortFloorLow = Out.SortFloorHigh = Val;
			Out.bHasValidKey = true;
			return Out;
		}
	}

	// Case 3: B{num}  -> basement floors (negative)
	if (LeftPart.StartsWith("B"))
	{
		const FString NumStr = LeftPart.Mid(1).TrimStartAndEnd();
		if (NumStr.IsNumeric())
		{
			const int32 Val = FCString::Atoi(*NumStr);
			Out.SortFloorLow = Out.SortFloorHigh = -Val; // negative
			Out.bHasValidKey = true;
			return Out;
		}
	}

	// Case 4: A{num}  -> attic/above floors (positive offset)
	if (LeftPart.StartsWith("A"))
	{
		const FString NumStr = LeftPart.Mid(1).TrimStartAndEnd();
		if (NumStr.IsNumeric())
		{
			const int32 Val = FCString::Atoi(*NumStr);
			Out.SortFloorLow = Out.SortFloorHigh = 1000 + Val; // push above normal range
			Out.bHasValidKey = true;
			return Out;
		}
	}

	return Out;
}

UWidgetUtilHelpers::FFloorCounterKey UWidgetUtilHelpers::ExtractFloorCounterFromWidget(UUserWidget* Widget,
	const FName& TextBlockName)
{
	FFloorCounterKey Out;
	if (!Widget || !Widget->WidgetTree)
		return Out;

	if (UTextBlock* TB = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TextBlockName)))
	{
		Out = ParseFloorCounterText(TB->GetText().ToString());
	}
	return Out;
}
