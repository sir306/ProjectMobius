// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Theme/MobiusThemedUserWidget.h"
#include "EnumsAndStructs/AgentMeshViewer.h"
#include "PedestrianDataDisplay.generated.h"

class UAgentInfoDisplay;
class UFieldAndTextWidget;
class UTextBlock;
class UGridPanel;
class UImage;

// Delegates
/** When the visibility of the selected agent component changes we need notify other classes that require it in blueprints or code */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectedAgentComponentVisibilityChanged, bool, bIsVisible);

/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UPedestrianDataDisplay : public UMobiusThemedUserWidget
{
	GENERATED_BODY()

public:
	virtual void SynchronizeProperties() override;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;

	/** Unbinds OnSelectedAgentInfoChanged — the subsystem outlives this widget. See NativeConstruct. */
	virtual void NativeDestruct() override;

	/** Born-theme: repaint the popup background surface (RibbonBg) on construct + every OnThemeChanged. */
	virtual void ApplyMobiusTheme_Implementation() override;

	void ConfigureTextBlockStyles() const;

	void SetupTextBlockTitles() const;

	void UpdateFieldTextBlocks() const;

	/**
	 * §3.4/D69: populate + visibility-gate the B-RISK tenability rows for the currently displayed agent.
	 * Gate = whether SelectedAgentID has an entry in UStatisticSubsystem's egress-health snapshot; that
	 * array is empty unless a B-RISK sim is loaded (the tenability Mass fragment/processor only publishes
	 * then), which is the module-safe "B-RISK loaded" signal (MobiusWidgets must not depend on the
	 * ProjectMobius BRiskDataSubsystem). No defaulting: when absent the whole section stays collapsed.
	 */
	void UpdateBRiskTenabilitySection(int32 SelectedAgentID) const;

	void ResizeGridPanelParentSlotToFitLargeText(FVector2D& InTextSize) const;

	void ResizeScreenGridToDefaultSize() const;

	void SetupTitleFieldWidgetFontSize() const;

	/** Font-fit cache (D4) for SetupTitleFieldWidgetFontSize: the panel paint-space size and the widest
	 *  field-text size the fitted font was last computed for. The fit (and the grid-slot resize) is a pure
	 *  function of these two inputs, so when both are unchanged the method early-returns instead of re-running
	 *  SetFont x8 + the grid-slot resize, each of which invalidates the panel and forces a Slate prepass/paint
	 *  every update. Keyed on text size as well as box size (not box alone) because these fields' values change
	 *  during playback/hover, so a box-only key could skip a real refit and clip text. Initialised to (-1,-1)
	 *  (an impossible real size) so the first call always computes. Mutable: the method is const. */
	mutable FVector2D CachedFontFitBoxSize = FVector2D(-1.0f, -1.0f);
	mutable FVector2D CachedFontFitTextSize = FVector2D(-1.0f, -1.0f);

	/** D143: number of rows the font-fit last budgeted for. The header grid holds 8 agent rows PLUS 5
	 *  optional B-RISK rows (BindWidgetOptional) that toggle visible only when tenability data is loaded.
	 *  The row budget (panel height / row count) and the panel-grow factor must track the number of VISIBLE
	 *  rows, not a hard-coded 8, or the extra B-RISK rows steal the fixed panel height and collapse the agent
	 *  rows to zero. Part of the D4 cache key so a B-RISK toggle forces a refit even when box+text are steady.
	 *  Floored at 8 so the agent-only case is pixel-identical to the pre-B-RISK behaviour. */
	mutable int32 CachedVisibleRowCount = -1;
public:
	/** Grid Panel to arrange sizing for the whole screen */
	UPROPERTY(EditAnywhere,BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UGridPanel> ScreenGrid;

	/** Grid Panel to arrange items */
	UPROPERTY(EditAnywhere,BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UGridPanel> WidgetHeadGridPanel;

	/**
	 * Popup background surface. Born-theme replaces its brush with a SOLID RibbonBg box (dropping the
	 * MI_PlayBarBackground material the value-walk couldn't retint). BindWidgetOptional so this C++
	 * builds/ships before the asset names the Image; when absent the background is left untouched.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UImage> PanelBackgroundImage;

       UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
       TObjectPtr<UFieldAndTextWidget> TitleFieldWidget1;

       UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
       TObjectPtr<UFieldAndTextWidget> TitleFieldWidget2;

       UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
       TObjectPtr<UFieldAndTextWidget> TitleFieldWidget3;

       UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
       TObjectPtr<UFieldAndTextWidget> TitleFieldWidget4;

       UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
       TObjectPtr<UFieldAndTextWidget> TitleFieldWidget5;

       UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
       TObjectPtr<UFieldAndTextWidget> TitleFieldWidget6;

       UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
       TObjectPtr<UFieldAndTextWidget> TitleFieldWidget7;

       UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
       TObjectPtr<UFieldAndTextWidget> TitleFieldWidget8;

       /** Convenience array holding all title field widgets for iteration */
       UPROPERTY(Transient)
       TArray<TObjectPtr<UFieldAndTextWidget>> TitleFieldWidgets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UAgentInfoDisplay> InWorldSMeshDisplay;

	UPROPERTY(EditAnywhere, BlueprintAssignable)
	FOnSelectedAgentComponentVisibilityChanged OnSelectedAgentComponentNowVisible;

	/**
	 * §3.4/D69 B-RISK tenability section. OPTIONAL rows appended to WBP_SelectAgentStats, shown only when
	 * the displayed agent has a B-RISK tenability entry (see UpdateBRiskTenabilitySection). BindWidgetOptional
	 * so this C++ builds/ships before the asset gains the widgets; when absent the window is unchanged.
	 * Caption used title-only ("B-RISK tenability" — B-RISK caps kept per C2). Values Mono (JetBrains).
	 * Field titles/units follow B-RISK exactly: Visibility (m), Toxic FED, Thermal FED, Temperature (C).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UFieldAndTextWidget> BRiskSectionCaption;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UFieldAndTextWidget> BRiskVisibilityField;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UFieldAndTextWidget> BRiskToxicFEDField;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UFieldAndTextWidget> BRiskThermalFEDField;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UFieldAndTextWidget> BRiskTemperatureField;
};
