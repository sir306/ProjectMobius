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
#include "UI/Theme/MobiusThemedUserWidget.h"
#include "MobiusSettingsWindowWidget.generated.h"

class SMoveableWindow;
class UButtonWithText;
class UCheckBox;
class UGlobalQualitySegmentWidget;
class UImage;
class UScalabilityPanelWidget;
class UTextBlock;

/**
 * Parent for WBP_ChangeScalabilitySettings — the "Settings" window from the panel rebuild brief (option 1a).
 *
 * The asset had NO dedicated C++ class before this: it parented straight to UMobiusThemedUserWidget, whose
 * ApplyMobiusTheme_Implementation is empty, and every behaviour lived in the widget graph. Owner ruling for
 * the rebuild is layout-in-WBP / logic-in-C++, so this class owns:
 *
 *  - the four logging flags, which are NOT one setting each side of a grid. "This session" is live
 *    subsystem state (UMobiusCustomLoggerSubsystem for the enable, the log WINDOW's own open/closed state
 *    for the second box); "At startup" is two UPROPERTY(Config) booleans on UUserProjectSettings. The brief
 *    draws them as a symmetric 2x2, which is a UI symmetry over two different storage mechanisms.
 *    Do NOT route the session enable through UUserProjectSettings::EnableMobiusLogger — that writes the
 *    STARTUP flag as well, which would couple the two columns the brief deliberately separates.
 *  - showing / hiding the Custom Display window, which in the rebuild is a SIBLING card rather than a child
 *    of the settings body. That un-nesting is the fix for the brief's "oversized window" complaint: nested
 *    inside a 420px column, the 640px matrix could never fit.
 *  - the group labels and helper lines, which take explicit palette roles. They are bound rather than left
 *    to StyleTextBlockForTheme's authored-colour remap, because that remap keys off whatever grey the
 *    .uasset happens to ship and these are new widgets with no established value.
 *
 * Phase 2 of the brief moves this whole card into SMoveableWindow. Nothing here should assume it is a child
 * of the game viewport's widget tree.
 */
UCLASS()
class MOBIUSWIDGETS_API UMobiusSettingsWindowWidget : public UMobiusThemedUserWidget
{
	GENERATED_BODY()

public:
	/** Shows or hides the Custom Display Settings card, re-syncing it from applied state when shown. */
	UFUNCTION(BlueprintCallable, Category = "Mobius Settings")
	void SetCustomPanelVisible(bool bVisible);

	/** Re-reads all four logging flags and the pedestrian flag into the checkboxes. */
	UFUNCTION(BlueprintCallable, Category = "Mobius Settings")
	void RefreshSettingStates();

	/**
	 * Opens the Settings card in its own draggable window (phase 2). Detaches it from the host UI tree on
	 * first open. Re-opening while open brings the window forward and refreshes the control states.
	 * This is the entry point the gear button should call instead of toggling visibility.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mobius Settings")
	void ShowAsWindow();

	/** Closes the Settings window. Does NOT close the Custom Display window — they are independent. */
	UFUNCTION(BlueprintCallable, Category = "Mobius Settings")
	void CloseWindow();

	UFUNCTION(BlueprintPure, Category = "Mobius Settings")
	bool IsWindowOpen() const;

	/** Gear-button friendly: opens if closed, closes if open. */
	UFUNCTION(BlueprintCallable, Category = "Mobius Settings")
	void ToggleWindow();

protected:
	//~ Begin UUserWidget Interface
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget Interface

	//~ Begin UMobiusThemedUserWidget Interface
	virtual void ApplyMobiusTheme_Implementation() override;
	//~ End UMobiusThemedUserWidget Interface

#pragma region Chrome
	/** Card surface; replaced with a solid RibbonBg rounded box on theme apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UImage> PanelBackgroundImage;

	/** Title bar strip behind PanelTitleText; takes PanelHeaderBg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UImage> TitleBarImage;

	/** "Settings". Takes PanelHeaderText. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PanelTitleText;
#pragma endregion

#pragma region GroupLabels
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GroupLabel_GlobalQuality;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GroupLabel_UITheme;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GroupLabel_Pedestrian;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GroupLabel_Logging;

	/** "This session" / "At startup" column heads over the logging grid; take MicroText. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ColumnHead_ThisSession;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ColumnHead_AtStartup;

	/** "Static meshes instead of animated agents — faster with large crowds."; takes HintText. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PedestrianHelperText;

	/** Log-file note. Its copy is set in C++ so the filename cannot drift from the logger's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LogFileNoteText;
#pragma endregion

#pragma region Controls
	/** Five-segment Low | Medium | High | Ultra | Custom control. Its Custom segment opens the other card. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UGlobalQualitySegmentWidget> GlobalQualityWidget;

	/** The Custom Display Settings card — a sibling, not a child of the settings body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UScalabilityPanelWidget> CustomSettingsPanel;

	/** Footer "Custom display settings…" row; does the same thing as the Custom segment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UButtonWithText> OpenCustomSettingsButton;

	/** Live logging on/off for THIS session (UMobiusCustomLoggerSubsystem). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> SessionLoggingCheckBox;

	/** Shows/hides the log window now. Reads the window's live state, not a stored bool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> SessionLogWindowCheckBox;

	/** UUserProjectSettings::bEnableMobiusLoggerAtStartup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> StartupLoggingCheckBox;

	/** UUserProjectSettings::bDisplayMobiusLogWindowAtStartup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> StartupLogWindowCheckBox;
#pragma endregion

private:
	/**
	 * Fills GlobalQualityWidget / CustomSettingsPanel by class when BindWidget could not.
	 * Both child widgets predate this class and are named for their assets (WBP_ScalabilitySettingGlobal,
	 * WBP_CustomScalabilitySettings), and the panel's Blueprint graph still holds the open/close flow
	 * through those variable names — so renaming them to match the BindWidget properties would break
	 * graph nodes that tooling cannot re-author. Resolving by class instead leaves the graph untouched.
	 */
	void ResolvePanelReferences();

	UFUNCTION()
	void HandleSessionLoggingToggled(bool bIsChecked);

	UFUNCTION()
	void HandleSessionLogWindowToggled(bool bIsChecked);

	UFUNCTION()
	void HandleStartupLoggingToggled(bool bIsChecked);

	UFUNCTION()
	void HandleStartupLogWindowToggled(bool bIsChecked);

	UFUNCTION()
	void HandleOpenCustomSettingsClicked();

	UFUNCTION()
	void HandleCustomSettingsConfirmed();

	/**
	 * Structural fixes the .uasset does not carry, applied once before the theme pass (owner, 2026-08-05).
	 * Done in C++ rather than the designer for the same reason the rest of this panel's layout repairs are:
	 * the Blueprint binds the leaves, and reaching their containers means walking GetParent() at runtime.
	 *
	 *  - Moves OpenCustomSettingsButton from the footer to directly under the Global Quality bar, and
	 *    styles it as a LINK (Accent label + outline) so it reads as clickable rather than as a caption.
	 *  - Re-parents each logging checkbox's label INTO the checkbox, so clicking the text toggles it. A
	 *    UCheckBox is a UContentWidget, so its single content slot is exactly the right home for the label
	 *    and Slate then treats the pair as one hit target. NOT applied to the Custom Display matrix, whose
	 *    cells are deliberately label-less (owner).
	 *  - Lifts the width cap: the panel's SizeBox pins MaxDesiredWidth to 600, which is what clipped
	 *    "Medium" to "Mediu:" and "Cinematic" once the fifth segment stopped being a short word.
	 */
	void RestructureSettingsLayout();

	/** Puts Label inside CheckBox's content slot so the text is part of the checkbox's hit area. */
	static void NestCheckBoxLabel(UCheckBox* CheckBox);

	/**
	 * Paints OpenCustomSettingsButton as a LINK: Accent label, hairline Accent outline, card-coloured fill
	 * so it does not compete with the segment bar above it. Called from RestructureSettingsLayout and again
	 * from ApplyMobiusTheme_Implementation, because the button clears bFollowThemePalette and therefore owns
	 * its own colours across a theme switch.
	 */
	void StyleCustomDisplayLink();

	/** The phase-2 host window. Not a UPROPERTY — Slate shared pointers are not GC-tracked. */
	TSharedPtr<SMoveableWindow> PanelWindow;

	/** True while RefreshSettingStates writes check states, so the handlers ignore their own writes. */
	bool bSuppressLoggingCallbacks = false;
};
