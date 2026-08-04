// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include "CoreMinimal.h"
#include "EnumsAndStructs/ScalabilityEnums.h"
#include "UI/Theme/MobiusThemedUserWidget.h"
#include "ScalabilityMatrixRowWidget.generated.h"

class UCheckBox;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnScalabilityRowLevelStaged,
	TEnumAsByte<EScalabilityCategories>, Category, TEnumAsByte<EScalabilitySettings>, NewLevel);

/**
 * One row of the Custom Display Settings matrix: a label plus five checkbox cells (Low / Medium / High /
 * Ultra / Cinematic) behaving as an exclusive group.
 *
 * WHY checkboxes and not the five UButtonWithText of UScalabilitySettingWidget: the panel rebuild
 * (2026-08-04) prints the quality names ONCE across the top of a 9x5 matrix, so a row is five cells wide
 * with no per-cell label. UMG has no radio widget, so the cells are standard UCheckBox with exclusivity
 * enforced here — exactly one is always checked, and unticking the checked cell re-ticks it rather than
 * leaving the row with no level.
 *
 * WHY the level is STAGED and not applied: owner ruling — nothing reaches the engine until Confirm. This
 * widget therefore never calls UPerformanceUtilSubsystem; it holds the pending level and reports it to the
 * owning UScalabilityPanelWidget, which batches all nine rows plus the resolution into one apply step.
 * That is also what makes Reset possible (revert to last confirmed).
 *
 * UScalabilitySettingWidget (the 5-button row) is deliberately left untouched — it still drives
 * WBP_ScalabilitySettingBase and WBP_CustomScalabilitySettingsRef.
 */
UCLASS()
class MOBIUSWIDGETS_API UScalabilityMatrixRowWidget : public UMobiusThemedUserWidget
{
	GENERATED_BODY()

public:
	/** Fires when the user picks a different cell. The owning panel listens; nothing is applied here. */
	UPROPERTY(BlueprintAssignable, Category = "Scalability Matrix")
	FOnScalabilityRowLevelStaged OnLevelStaged;

	/** Which of the nine per-feature categories this row drives. Set per row instance in the designer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Matrix")
	TEnumAsByte<EScalabilityCategories> ScalabilityCategory = ESc_GlobalIllumination;

	/**
	 * Row label. Pushed to RowLabelText in NativePreConstruct so the designer shows the real copy and the
	 * text is not duplicated between the C++ default and the .uasset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Matrix")
	FText RowLabel;

	/** Sets the pending level and re-ticks the cells. Does NOT broadcast — this is the panel pushing down. */
	UFUNCTION(BlueprintCallable, Category = "Scalability Matrix")
	void SetStagedLevel(const TEnumAsByte<EScalabilitySettings> NewLevel);

	UFUNCTION(BlueprintCallable, Category = "Scalability Matrix")
	TEnumAsByte<EScalabilitySettings> GetStagedLevel() const { return StagedLevel; }

protected:
	//~ Begin UUserWidget Interface
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	//~ End UUserWidget Interface

	//~ Begin UMobiusThemedUserWidget Interface
	virtual void ApplyMobiusTheme_Implementation() override;
	//~ End UMobiusThemedUserWidget Interface

	/** Row label ("Global Illumination", ...). Recoloured to LabelText on theme apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RowLabelText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UCheckBox> LowCheckBox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UCheckBox> MediumCheckBox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UCheckBox> HighCheckBox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UCheckBox> UltraCheckBox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UCheckBox> CinematicCheckBox;

private:
	/** One UFUNCTION per cell: OnCheckStateChanged carries only the new bool, not which box sent it. */
	UFUNCTION()
	void HandleLowToggled(bool bIsChecked);
	UFUNCTION()
	void HandleMediumToggled(bool bIsChecked);
	UFUNCTION()
	void HandleHighToggled(bool bIsChecked);
	UFUNCTION()
	void HandleUltraToggled(bool bIsChecked);
	UFUNCTION()
	void HandleCinematicToggled(bool bIsChecked);

	/** Shared body for the five handlers above. */
	void HandleCellToggled(TEnumAsByte<EScalabilitySettings> CellLevel, bool bIsChecked);

	/** Writes the five check states from StagedLevel. Re-entrant-safe via bSuppressCellCallbacks. */
	void RefreshCellStates();

	/** Returns the five cells in level order, skipping any that failed to bind. */
	void ForEachCell(TFunctionRef<void(UCheckBox*, TEnumAsByte<EScalabilitySettings>)> Visitor) const;

	/** The pending level. Only Confirm on the owning panel turns this into engine state. */
	TEnumAsByte<EScalabilitySettings> StagedLevel = EScalabilitySettings::ESsl_Epic;

	/** True while RefreshCellStates is writing, so SetIsChecked does not re-enter the handlers. */
	bool bSuppressCellCallbacks = false;
};
