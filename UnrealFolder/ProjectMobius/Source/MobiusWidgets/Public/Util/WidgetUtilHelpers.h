// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class UComboBoxString;
/**
 * 
 */
class MOBIUSWIDGETS_API WidgetUtilHelpers
{
public:
	WidgetUtilHelpers();
	~WidgetUtilHelpers();

	/**
	 * Clears all options in the provided combo string box
	 *
	 * @param[UComboBoxString] ComboBox The combo box to clear options from
	 */
	static void ClearComboBoxOptions(TObjectPtr<UComboBoxString> ComboBox);

	/**
	 * Populates a combo box with the provided options
	 *
	 * @param[UComboBoxString] ComboBox The combo box to populate
	 * @param[TArray<FString>] Options The options to populate the combo box with
	 * @param[FString] SelectedOption The option to select in the combo box
	 */
	static void UpdateComboBoxOptions(TObjectPtr<UComboBoxString> ComboBox, const TArray<FString>& Options, const FString& SelectedOption);

	/**
	 * Find if input string is a valid option in the combo box and if the flag to set it true then will set the combo box to that option
	 *
	 * @param[UComboBoxString] ComboBox The combo box to check
	 * @param[FString] Option The option to check for in the combo box
	 * @param[bool] bSetSelection If true, will set the combo box to the option if it exists
	 */
	static void FindAndSetComboBoxOption(TObjectPtr<UComboBoxString> ComboBox, const FString& Option, bool bSetSelection = true);
};
