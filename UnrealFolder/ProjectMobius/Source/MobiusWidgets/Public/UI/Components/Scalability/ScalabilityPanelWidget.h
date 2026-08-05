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
#include "ScalabilityPanelWidget.generated.h"

class SMoveableWindow;
class UButton;
class UButtonWithText;
class UComboBoxString;
class UEditableTextBox;
class UImage;
class UScalabilityMatrixRowWidget;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnScalabilitySettingsConfirmed);

/**
 * Born-themed parent for WBP_CustomScalabilitySettings (the "Custom Display Settings" panel).
 *
 * The old runtime value-remap walk misses this panel (shown before/after a startup theme ticker,
 * and its background used the playbar material MI_PlayBarBackground which the walk can't retint).
 * As a UMobiusThemedUserWidget it self-applies on NativeConstruct and every OnThemeChanged:
 *  - the panel background image is replaced with a solid RibbonBg surface,
 *  - the panel title text is recoloured to PanelHeaderText.
 *
 * PANEL REBUILD (2026-08-04) — this class also became the panel's STAGING owner.
 *
 * The rebuilt layout is a 9x5 matrix (nine UScalabilityMatrixRowWidget rows) over a footer bar whose
 * Reset / Confirm buttons cover the WHOLE window, not just the resolution as they used to. Owner ruling:
 * nothing reaches the engine until Confirm. So every pending change — nine quality levels plus the
 * resolution — is held here and applied in one batch by ConfirmStagedSettings().
 *
 * Reset means "back to the LAST CONFIRMED values", not engine defaults (a separate reset-to-defaults
 * control is a later task). Because nothing was applied, Reset only has to restore the staged copy and
 * re-tick the UI; it never has to undo engine state.
 *
 * The nine rows are DISCOVERED from the widget tree rather than named as nine BindWidgets: the matrix is
 * authored in the designer and rows get renamed/reordered there, and a missing BindWidget is a compile
 * error on the .uasset rather than a recoverable miss.
 */
UCLASS()
class MOBIUSWIDGETS_API UScalabilityPanelWidget : public UMobiusThemedUserWidget
{
	GENERATED_BODY()

public:
	/** Fires after a successful Confirm, so the Settings panel can re-derive its Global Quality row. */
	UPROPERTY(BlueprintAssignable, Category = "Scalability Panel")
	FOnScalabilitySettingsConfirmed OnSettingsConfirmed;

	/** Panel surface. Its brush is replaced with a solid RibbonBg box (no material) on theme apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UImage> PanelBackgroundImage;

	/** Panel title ("Custom Display Settings"); recoloured to PanelHeaderText on theme apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PanelTitleText;

	/**
	 * Re-reads applied engine state into the staged copy and refreshes every control. Call this whenever
	 * the panel is shown — the window is not destroyed between openings, so stale staged values from a
	 * previous session would otherwise survive.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scalability Panel")
	void SyncFromAppliedSettings();

	/** Applies all nine staged levels plus the staged resolution in one batch, then re-baselines Reset. */
	UFUNCTION(BlueprintCallable, Category = "Scalability Panel")
	void ConfirmStagedSettings();

	/** Restores the last confirmed values. Applies nothing — staged changes never reached the engine. */
	UFUNCTION(BlueprintCallable, Category = "Scalability Panel")
	void ResetStagedSettings();

	/** True when at least one staged value differs from the last confirmed one. */
	UFUNCTION(BlueprintCallable, Category = "Scalability Panel")
	bool HasPendingChanges() const;

	/**
	 * Opens this card in its own draggable window (phase 2). Detaches from any parent widget tree first, so
	 * it is no longer size-bound by — or hit-test-blocked by — the Settings card that used to nest it.
	 * Re-opening while already open just brings the window forward.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scalability Panel")
	void ShowAsWindow();

	/** Closes the window. Staged-but-unconfirmed changes are discarded — see the ruling in the .cpp. */
	UFUNCTION(BlueprintCallable, Category = "Scalability Panel")
	void CloseWindow();

	UFUNCTION(BlueprintPure, Category = "Scalability Panel")
	bool IsWindowOpen() const;

protected:
	//~ Begin UUserWidget Interface
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget Interface

	//~ Begin UMobiusThemedUserWidget Interface
	virtual void ApplyMobiusTheme_Implementation() override;
	//~ End UMobiusThemedUserWidget Interface

	/** X of the staged resolution. Committed on enter/focus-loss, not per keystroke. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> ResolutionXTextBox;

	/** Y of the staged resolution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> ResolutionYTextBox;

	/** Supported-resolution list. Picking an entry stages it and mirrors it into the X/Y fields. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ResolutionPresetComboBox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UButtonWithText> ResetButton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UButtonWithText> ConfirmButton;

private:
	UFUNCTION()
	void HandleRowLevelStaged(TEnumAsByte<EScalabilityCategories> Category, TEnumAsByte<EScalabilitySettings> NewLevel);

	UFUNCTION()
	void HandleResetClicked();

	UFUNCTION()
	void HandleConfirmClicked();

	UFUNCTION()
	void HandleResolutionTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandlePresetSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

	/** Finds the nine matrix rows in the tree and subscribes to each one's OnLevelStaged. */
	void CollectMatrixRows();

	/** The phase-2 host window. Not a UPROPERTY — Slate shared pointers are not GC-tracked. */
	TSharedPtr<SMoveableWindow> PanelWindow;

	/** Fills the preset combo from UPerformanceUtilSubsystem::GetSystemScreenResolutions(). */
	void PopulatePresetOptions();

	/** Pushes StagedResolution into the X/Y fields and the combo selection. */
	/**
	 * The resolution the user is actually looking at. Prefers the live viewport size over
	 * GetCurrentScreenResolution, which reads the SAVED GameUserSettings value and so could show a
	 * resolution the window did not have (owner, 2026-08-05). Falls back to the saved value when there is
	 * no viewport, e.g. design time.
	 */
	FIntPoint ResolveCurrentResolution(class UPerformanceUtilSubsystem* Performance) const;

	void RefreshResolutionControls();

	/** Pushes StagedLevels into the rows. */
	void RefreshMatrixRows();

	/** "1920 x 1080" — the combo's display form, and the only place that format is defined. */
	static FString FormatResolution(const FIntPoint& Resolution);

	/** Nine rows, discovered at construct. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UScalabilityMatrixRowWidget>> MatrixRows;

	/** Pending per-category levels, and the values Reset returns to. */
	TMap<TEnumAsByte<EScalabilityCategories>, TEnumAsByte<EScalabilitySettings>> StagedLevels;
	TMap<TEnumAsByte<EScalabilityCategories>, TEnumAsByte<EScalabilitySettings>> ConfirmedLevels;

	FIntPoint StagedResolution = FIntPoint::ZeroValue;
	FIntPoint ConfirmedResolution = FIntPoint::ZeroValue;

	/** Resolution list backing the combo, index-aligned with its options. */
	TArray<FIntPoint> PresetResolutions;

	/** True while this class writes the combo selection, so HandlePresetSelected ignores its own write. */
	bool bSuppressPresetCallback = false;
};
