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
#include "Blueprint/UserWidget.h"
#include "Interfaces/ProjectMobiusInterface.h"
#include "SimulationSetupWidget.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API USimulationSetupWidget : public UUserWidget, public IProjectMobiusInterface
{
	GENERATED_BODY()

#pragma region METHODS
public:

	// Native Pre Construct
	virtual void NativePreConstruct() override;

	// Native Constructor 
	virtual void NativeConstruct() override;

	/** Method To Setup the widgets components */
	void SetupWidgetComponents();

	void UpdateCurrentTimeDilationScaleText();

	/** Method To Bind the update TimeDilationButton */
	UFUNCTION(BlueprintCallable)
	void UpdateTimeDilation();

	/**
	* Method to Update the Time Dilation Scale
	*/
	UFUNCTION(BlueprintCallable)
	void UpdateTimeDilationScale(float TimeDilationScale);

	/**
	 * Apply a playback-speed preset (S11). Also writes the value into the numeric box, so the box and the
	 * speed actually in effect can never disagree — a preset that changed playback but left a stale number
	 * on screen reads as the preset having done nothing.
	 */
	UFUNCTION(BlueprintCallable)
	void ApplyTimeDilationPreset(float PresetScale);

	/** Push the live time-dilation factor into the editable box. Used on construct and to revert a rejected
	 *  edit, so the box only ever shows a value that is genuinely in effect. */
	void SyncTimeDilationTextBoxToCurrent();

	/** UButton::OnClicked carries no payload, so a single handler cannot know which preset was pressed and
	 *  AddDynamic cannot bind one with the multiplier baked in. Hence one zero-argument UFUNCTION per
	 *  preset, each a one-line forwarder to ApplyTimeDilationPreset. */
	UFUNCTION()
	void HandlePreset1xClicked();

	UFUNCTION()
	void HandlePreset2xClicked();

	UFUNCTION()
	void HandlePreset5xClicked();

	UFUNCTION()
	void HandlePreset10xClicked();

	/** Commit on Enter in the numeric box, so the Update button is a convenience rather than the only way
	 *  to apply a typed value. Ignores focus-loss commits, which would otherwise apply half-typed input. */
	UFUNCTION()
	void HandleTimeDilationTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	/**
	 * S14 sim-cache controls. These live on this class rather than in a Blueprint graph so the whole
	 * Simulation Settings section is bind-and-C++ with no graph nodes to author or maintain.
	 *
	 * ProjectMobius already depends on MobiusCore (UUserProjectSettings) and owns MobiusSimCache, so this
	 * needs no new module dependency — unlike the themed button classes in MobiusWidgets, which it cannot
	 * reference without a cycle.
	 */
	UFUNCTION()
	void HandleCacheOnImportChanged(bool bIsChecked);

	UFUNCTION()
	void HandleReuseCacheOnReopenChanged(bool bIsChecked);

	/** Clear the .msc cache and refresh the size readout. Deletes only files inside the cache directory. */
	UFUNCTION(BlueprintCallable)
	void HandleClearCacheClicked();

	/** Recompute the on-disk cache size and write it into CacheSizeText. Called on construct and after a
	 *  clear — deliberately not on a timer: the size only changes on import or clear. */
	UFUNCTION(BlueprintCallable)
	void RefreshCacheSizeText();

	/** Set both cache checkboxes from the persisted settings, so the panel opens showing the real state
	 *  rather than whatever the asset was authored with. */
	void SyncCacheCheckBoxesToSettings();

#pragma endregion METHODS

#pragma region PROPERTIES_AND_CLASS_COMPONENTS
	/** Text block to show current Time Dilation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	class UTextBlock* CurrentTimeDilationScaleText;

	/** Editable Text Box Used for changing TimeDilation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	class UEditableTextBox* TimeDilationScaleEditableTextBox;

	/**
	 * Optional explicit "Apply" button.
	 *
	 * BindWidgetOptional since 2026-08-11 (owner ruling): an Apply button is redundant when Enter in the
	 * numeric box and the presets already commit, so the shipping panel does not include one. Kept as an
	 * optional bind rather than deleted so a host that wants an explicit commit affordance still works —
	 * and because a REQUIRED bind cannot be dropped from a widget asset at all
	 * (MobiusWidgetEditorTools::RemoveWidget refuses it, correctly, to avoid a compile break).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	class UButton* UpdateTimeDilationButton;

	/**
	 * 1x / 2x / 5x / 10x playback-speed presets (S11).
	 *
	 * BindWidgetOptional, deliberately NOT BindWidget: a host asset may legitimately expose only some of
	 * the presets, and a required bind would fail that host's compile rather than simply omitting a button.
	 * Every use site null-checks accordingly.
	 *
	 * Typed as plain UButton rather than a themed Mobius button class because MobiusWidgets depends on
	 * ProjectMobius — referencing UBaseButton from this module would create a module cycle. Nothing is lost:
	 * UUIThemeSubsystem's recursive widget walk themes plain UButtons, which is how every icon button in the
	 * app is already styled.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	class UButton* TimeDilationPreset1xButton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	class UButton* TimeDilationPreset2xButton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	class UButton* TimeDilationPreset5xButton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	class UButton* TimeDilationPreset10xButton;

	/** S14 control 1 — "cache imported simulations" (mobius.SimCache.WriteOnImport). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	class UCheckBox* CacheOnImportCheckBox;

	/** S14 control 2 — "reuse cache when reopening a file" (mobius.SimCache.FastReload). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	class UCheckBox* ReuseCacheOnReopenCheckBox;

	/** S14 control 3 — clear the cache directory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	class UButton* ClearCacheButton;

	/** On-disk cache size readout that sits beside the clear button (S14 control 3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	class UTextBlock* CacheSizeText;

#pragma endregion PROPERTIES_AND_CLASS_COMPONENTS
};
