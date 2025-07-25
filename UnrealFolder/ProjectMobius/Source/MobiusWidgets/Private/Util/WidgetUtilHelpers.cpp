// Fill out your copyright notice in the Description page of Project Settings.


#include "Util/WidgetUtilHelpers.h"

#include "Components/ComboBoxString.h"
#include "Components/GridSlot.h"
#include "Components/CustomSlateWidgets/FieldAndTextWidget.h"


WidgetUtilHelpers::WidgetUtilHelpers()
{
}

WidgetUtilHelpers::~WidgetUtilHelpers()
{
}

void WidgetUtilHelpers::ClearComboBoxOptions(TObjectPtr<UComboBoxString> ComboBox)
{
	if (ComboBox)
	{
		ComboBox->ClearSelection();
		ComboBox->ClearOptions();
	}
}

void WidgetUtilHelpers::UpdateComboBoxOptions(TObjectPtr<UComboBoxString> ComboBox, const TArray<FString>& Options,
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

void WidgetUtilHelpers::FindAndSetComboBoxOption(TObjectPtr<UComboBoxString> ComboBox, const FString& Option,
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

void WidgetUtilHelpers::UpdateTextIfChanged(UFieldAndTextWidget* Widget, const FText& NewText)
{
        if (!Widget) return;

        if (!Widget->FieldText.EqualTo(NewText))
        {
                Widget->SetFieldText(NewText);
        }
}

void WidgetUtilHelpers::UpdateNumberIfChanged(UFieldAndTextWidget* Widget, int32 NewNumber)
{
        if (!Widget) return;

        FText NewText = FText::AsNumber(NewNumber);
        if (!Widget->FieldText.EqualTo(NewText))
        {
                Widget->SetFieldText(NewText);
        }
}

void WidgetUtilHelpers::UpdateFloatIfChanged(UFieldAndTextWidget* Widget, float NewFloat)
{
        if (!Widget) return;

        FText NewText = FText::FromString(FString::Printf(TEXT("%.2f"), NewFloat));
        if (!Widget->FieldText.EqualTo(NewText))
        {
                Widget->SetFieldText(NewText);
        }
}

void WidgetUtilHelpers::UpdateVectorIfChanged(UFieldAndTextWidget* Widget, const FVector& Vec)
{
        if (!Widget) return;

        FText NewText = FText::FromString(FString::Printf(TEXT("%.2f, %.2f, %.2f"), Vec.X, Vec.Y, Vec.Z));

        if (!Widget->FieldText.EqualTo(NewText))
        {
                Widget->SetFieldText(NewText);
        }
}

void WidgetUtilHelpers::SetGridSlotAlignment(UWidget* Widget, EHorizontalAlignment HAlign,
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
