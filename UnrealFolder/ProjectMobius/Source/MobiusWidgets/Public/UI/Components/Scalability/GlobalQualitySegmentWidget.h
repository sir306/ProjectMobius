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
#include "ScalabilityWidgetBase.h"
#include "GlobalQualitySegmentWidget.generated.h"

class UButtonWithText;
class UMobiusThemedBorder;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCustomQualityRequested);

/**
 * The "Global Quality" control in the Settings panel: five segments — Low | Medium | High | Ultra | Custom.
 *
 * Parent for WBP_ScalabilitySettingGlobal, whose five UButtonWithText already carry the names bound below,
 * so the rebuild reparents that asset rather than re-authoring it. It replaces the widget's BP graph
 * (owner ruling: state lives in C++), including the graph that swapped SWS_ScaleabilityButtonCurrentSet in
 * to mark the active tier.
 *
 * Visual language is the A20 theme toggle's, deliberately: one hairline container, active segment filled
 * Accent with a white SemiBold label, inactive transparent with a TabInactiveText Regular label. Segments
 * clear bFollowThemePalette so UBaseButton's flat re-stamp cannot repaint the meaning away.
 *
 * TWO THINGS THAT ARE NOT THE OBVIOUS DESIGN, both owner rulings (2026-08-04):
 *
 *  1. The active segment is the LOWEST of the nine applied per-feature levels, not "Custom when mixed".
 *     A mixed set therefore reads as its weakest link, which is the honest answer to "what quality am I
 *     actually getting". Nothing here ever selects Custom on the user's behalf.
 *
 *  2. Custom is a segment that OPENS THE CUSTOM DISPLAY WINDOW, not a selectable state. It is styled as
 *     the mockup draws it — inactive fill with an Accent label, i.e. a link — and broadcasts
 *     OnCustomQualityRequested.
 *
 * Clicking a real tier calls UPerformanceUtilSubsystem::UpdateGlobalScalabilitySetting, which applies that
 * level to all nine categories. That is applied IMMEDIATELY: the Confirm-batching ruling scopes to the
 * Custom Display window's own Reset/Confirm footer, and this control has neither.
 */
UCLASS()
class MOBIUSWIDGETS_API UGlobalQualitySegmentWidget : public UScalabilityWidgetBase
{
	GENERATED_BODY()

public:
	/** Fires when the Custom segment is clicked. The Settings panel shows the Custom Display window. */
	UPROPERTY(BlueprintAssignable, Category = "Scalability Settings")
	FOnCustomQualityRequested OnCustomQualityRequested;

	/**
	 * Re-derives the active segment from applied engine state. Call after anything that could have changed
	 * a per-feature level — notably UScalabilityPanelWidget::OnSettingsConfirmed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scalability Settings")
	void RefreshActiveSegment();

protected:
	//~ Begin UUserWidget Interface
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	//~ End UUserWidget Interface

	//~ Begin UScalabilityWidgetBase Interface
	/**
	 * Both are no-ops here. The base pair reads the level for ScalabilityCategory and then re-APPLIES it on
	 * every construct, which for a global control means writing all nine categories from a value this
	 * widget has not derived yet. This class owns its own read (RefreshActiveSegment) and its own write
	 * (a segment click), so the base's construct-time apply is not just redundant, it fights them.
	 */
	virtual void InitializeScalabilityLevel() override {}
	virtual void UpdateScalabilityLevel() override {}
	//~ End UScalabilityWidgetBase Interface

	//~ Begin UMobiusThemedUserWidget Interface
	virtual void ApplyMobiusTheme_Implementation() override;
	//~ End UMobiusThemedUserWidget Interface

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings", meta = (BindWidget))
	TObjectPtr<UButtonWithText> LowSetting_Button;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings", meta = (BindWidget))
	TObjectPtr<UButtonWithText> MedSetting_Button;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings", meta = (BindWidget))
	TObjectPtr<UButtonWithText> HighSetting_Button;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings", meta = (BindWidget))
	TObjectPtr<UButtonWithText> EpicSetting_Button;

	/** The Custom segment. Opens the Custom Display window; never becomes the active segment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings", meta = (BindWidget))
	TObjectPtr<UButtonWithText> CustomSetting_Button;

	/**
	 * The hairline pill the five segments sit inside — built in C++ at construct, because the Blueprint
	 * binds only the buttons. Not a BindWidget: it does not exist in the .uasset.
	 *
	 * This class's header promised "one hairline container" from the start but never built one, so the
	 * transparent inactive segments showed the CARD instead of InputBg and the control read as bare text
	 * next to the UI Theme toggle two rows below it. Same construction as
	 * UThemeToggleWidget::SegmentContainer, deliberately — that is the A20 visual language this control
	 * is supposed to speak.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMobiusThemedBorder> SegmentContainer;

private:
	/**
	 * Wrap the segments' existing GridPanel in SegmentContainer, once, before the theme pass runs.
	 *
	 * Wraps the PANEL rather than moving the five buttons into a new box: measured live, they sit in a
	 * single-row GridPanel (row 0, columns 0-4) that holds nothing else, so re-parenting the panel keeps
	 * every GridSlot — and therefore the column widths and the H_ALIGN_FILL that stops unequal pressed
	 * padding from eating clicks — exactly as authored.
	 */
	void EnsureSegmentContainer();

	UFUNCTION()
	void HandleLowClicked();
	UFUNCTION()
	void HandleMediumClicked();
	UFUNCTION()
	void HandleHighClicked();
	UFUNCTION()
	void HandleUltraClicked();
	UFUNCTION()
	void HandleCustomClicked();

	/** Applies a tier to all nine categories, then re-derives the active segment. */
	void ApplyGlobalTier(TEnumAsByte<EGlobalScalabilitySettings> Tier);

	/** Lowest of the nine applied per-feature levels — the displayed tier per owner ruling 1 above. */
	TEnumAsByte<EScalabilitySettings> DeriveLowestAppliedLevel() const;

	/** Restyles all five segments for the current theme and active tier. */
	void RestyleSegments() const;

	/** Segment styling. bAccentLabel is the Custom "link" case: inactive fill, Accent label. */
	void StyleSegment(UButtonWithText* Segment, bool bActive, bool bFirst, bool bLast, bool bAccentLabel) const;

	/** The tier the active segment currently shows. Derived, never authoritative. */
	TEnumAsByte<EScalabilitySettings> DisplayedLevel = EScalabilitySettings::ESsl_Epic;
};
