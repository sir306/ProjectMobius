// Fill out your copyright notice in the Description page of Project Settings.


#include "Util/WidgetUtilHelpers.h"

#include "Components/ComboBoxString.h"


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
