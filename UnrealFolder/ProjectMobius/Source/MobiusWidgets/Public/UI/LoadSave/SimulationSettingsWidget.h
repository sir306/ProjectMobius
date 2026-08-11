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
#include "Interfaces/ProjectMobiusInterface.h"
#include "SimulationSettingsWidget.generated.h"

class UCheckBox;

/**
 * How a playback-speed preset button is drawn. Three states, not two: Apply-gating (2026-08-11) means the
 * preset you clicked is not yet the speed that is running, and without a distinct PENDING look a click gave
 * no feedback at all until Apply was pressed.
 */
enum class EPresetVisualState : uint8
{
	/** Not the live speed and not staged. Ordinary button colours. */
	Idle,
	/** Staged in the numeric box, awaiting Apply. Takes ButtonHoverBg — reads as "armed". */
	Pending,
	/** The speed actually running. Takes the Accent fill, matching UGlobalQualitySegmentWidget. */
	Active
};

/**
 * The ONE simulation-settings panel: playback speed (S11), the sim-cache controls (S14) and the B-Risk
 * load-time toggles.
 *
 * 2026-08-11 unification (owner: the old split *"makes no sense in its current form and is bad coding
 * practice"*). This class is `USimulationSetupWidget` moved here from **ProjectMobius** and merged with the
 * small `USimulationSettingsWidget` that used to own only the B-Risk toggles. `WBP_SimulationSettings`
 * reaches it through a `[CoreRedirects]` `ClassRedirects` entry, so no asset was ever left with a missing
 * parent class.
 *
 * WHY THIS MODULE, and it is not a preference: `MobiusWidgets` depends privately on `ProjectMobius`
 * (`MobiusWidgets.Build.cs:28`) and `MobiusCore` (`:39`), so from here every API both halves need is
 * reachable — `TimeDilationSubSystem`, `MobiusSimCache`, `UBRiskDataSubsystem`, `UUserProjectSettings`,
 * `IProjectMobiusInterface`. The reverse direction is a module cycle, which is exactly why the old
 * ProjectMobius home could NOT derive `UMobiusThemedUserWidget` (see `UIThemeSubsystem.cpp:1171-1173`) and
 * had to lean on the subsystem's recursive theme walk — a walk A6b-6 intends to delete. Living here means
 * the panel themes itself through the base like the rest of the UI.
 *
 * `IProjectMobiusInterface` inheritance is KEPT, and must be. Its time-dilation helpers read like statics at
 * the call sites (`IProjectMobiusInterface::GetMobiusGameInstanceSimulationTimeDilatationFactor(World)`) but
 * they are NON-STATIC members — that form is a qualified call on `this`, which only resolves because the
 * class implements the interface. Dropping the inheritance compiles right up to
 * `error C2352: a call of a non-static member function requires an object`. The interface lives in
 * **MobiusCore**, not ProjectMobius, so this include is a dependency MobiusWidgets already has.
 */
UCLASS()
class MOBIUSWIDGETS_API USimulationSettingsWidget : public UMobiusThemedUserWidget, public IProjectMobiusInterface
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
	 * STAGE a playback-speed preset into the numeric box without applying it (owner, 2026-08-11).
	 *
	 * Was `ApplyTimeDilationPreset`, which committed immediately. The owner's requirement is that changing
	 * the speed updates the PENDING value only, and the live simulation plus the "Current speed" readout move
	 * only on Apply — so a preset is now an input, exactly like typing a number. Renaming rather than quietly
	 * changing behaviour behind the old name; verified first that no Blueprint graph calls either function
	 * (content-wide byte scan), which is why no FunctionRedirect is needed.
	 */
	UFUNCTION(BlueprintCallable)
	void StageTimeDilationPreset(float PresetScale);

	/**
	 * Repaint the preset buttons so the one matching the APPLIED speed carries the Accent fill, mirroring
	 * UGlobalQualitySegmentWidget's active-segment treatment. Reads the live factor, never the pending one:
	 * the highlight answers "what speed is running", which is the question the readout beside it also answers.
	 */
	void RefreshPresetHighlight();

	/** Push the live time-dilation factor into the editable box. Used on construct and to revert a rejected
	 *  or abandoned edit, so the box falls back to a value that is genuinely in effect. */
	void SyncTimeDilationTextBoxToCurrent();

	/** Paint one preset button for its visual state. Handles both shapes the asset might use: it styles the
	 *  button's own foregrounds AND any direct TextBlock content, so it works whether the label inherits the
	 *  foreground or carries its own colour. */
	void StylePresetButton(class UButton* Button, EPresetVisualState State) const;

	/** The playback factor actually in effect, read from the game instance.
	 *  NOT const: the IProjectMobiusInterface getter is a non-const member, so calling it through a const
	 *  `this` fails with C2662 even though it only reads. */
	float GetLiveTimeDilationScale();

	//~ Begin UMobiusThemedUserWidget Interface
	/** Re-lands the preset highlight: its Accent fill and label colour are palette values, so a theme switch
	 *  has to repaint them or the active preset keeps the old theme's accent. */
	virtual void ApplyMobiusTheme_Implementation() override;
	//~ End UMobiusThemedUserWidget Interface

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

	/** Toggle: use the B-Risk zone-CSV Time column to drive shared playback timing. */
	UFUNCTION()
	void OnUseBRiskTimingChanged(bool bIsChecked);

	/** Toggle: generate solid room-wall geometry from the B-Risk scenario. */
	UFUNCTION()
	void OnLoadRoomGeometryChanged(bool bIsChecked);

	/** Toggle: fill each opening B-Risk has SHUT with a solid panel. Off by default. */
	UFUNCTION()
	void OnShowClosedOpeningsChanged(bool bIsChecked);

#pragma endregion METHODS

#pragma region PROPERTIES_AND_CLASS_COMPONENTS
	/**
	 * Text block to show current Time Dilation.
	 *
	 * BindWidgetOptional since the 2026-08-11 merge, downgraded from a REQUIRED BindWidget: this class now
	 * has more than one host, and a required bind fails the compile of any host that does not carry the
	 * widget rather than simply omitting the control. Every use site already null-checks.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	class UTextBlock* CurrentTimeDilationScaleText;

	/** Editable Text Box Used for changing TimeDilation. Optional for the same reason as the readout above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
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

	/**
	 * B-Risk load-time toggles, merged in from the old small USimulationSettingsWidget (2026-08-11).
	 * Each drives UBRiskDataSubsystem LIVE — see the handlers for what each one re-evaluates.
	 */
	/** "Use B-Risk playback timing" toggle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	TObjectPtr<UCheckBox> UseBRiskTimingCheckBox;

	/** "Load room geometry" toggle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	TObjectPtr<UCheckBox> LoadRoomGeometryCheckBox;

	/**
	 * "Show closed openings" toggle. Note it defaults OFF, so an UNBOUND control and a working one look
	 * identical on screen — the name in the host asset must match exactly or the feature is silently
	 * unreachable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	TObjectPtr<UCheckBox> ShowClosedOpeningsCheckBox;

#pragma endregion PROPERTIES_AND_CLASS_COMPONENTS
};
