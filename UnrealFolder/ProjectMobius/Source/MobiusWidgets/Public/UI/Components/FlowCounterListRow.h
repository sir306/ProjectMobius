// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Theme/MobiusThemedUserWidget.h"
#include "FlowCounterListRow.generated.h"

class UImage;
class UTextBlock;
class UCheckBox;
class UUIThemeSubsystem;

/**
 * C++ base for WBP_FlowCounterManipulation — the per-counter row spawned into the Flow Counters list.
 *
 * The row was a plain BP with a hardcoded BLACK highlight image + white text, so it read as a black bar
 * in light mode and never followed the theme. This base drives the row's SELECTION highlight + text colour
 * from the palette: selected = accent fill + white text, idle = transparent + primary (InputText) text.
 * Re-applies on OnThemeChanged so a live toggle is followed. The row's own checkbox (SELECT column) is the
 * selection signal — its OnCheckStateChanged re-applies the visual; the container's selection logic (which
 * also watches the checkbox) is untouched and coexists.
 *
 * BindWidgetOptional keeps this safe if a widget is renamed in the WBP: the row simply skips that element.
 *
 * A6b-5 (2026-07-28): base changed UUserWidget -> UMobiusThemedUserWidget. This class had already hand-rolled
 * the base's entire contract — a weak subsystem pointer, a bThemeBound flag, its own OnThemeChanged
 * bind/unbind and its own HandleThemeChanged — so the swap DELETES that duplication rather than adding to it.
 * The hand-rolled handler also had to go regardless: its name collides with the base's UFUNCTION of the same
 * name. Its body is now the ApplyMobiusTheme override, which is the same call at the same two moments.
 */
UCLASS()
class MOBIUSWIDGETS_API UFlowCounterListRow : public UMobiusThemedUserWidget
{
	GENERATED_BODY()

public:
	/** Apply the selected / idle visual (accent+white vs transparent+primary) for the current theme. */
	UFUNCTION(BlueprintCallable, Category = "FlowCounterRow")
	void SetRowSelected(bool bSelected);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Theme toggled — re-state the row visual. Replaces the hand-rolled HandleThemeChanged. */
	virtual void ApplyMobiusTheme_Implementation() override;

private:
	/** Checkbox (SELECT column) toggled — refresh the row visual to match. */
	UFUNCTION()
	void HandleSelectChanged(bool bIsChecked);

	void ApplyRowVisual();
	UUIThemeSubsystem* ResolveThemeSubsystem() const;

	// BlueprintReadWrite + AllowPrivateAccess: the existing WBP graph already Gets these bound widgets, so
	// they must stay Blueprint-visible after this class takes them over as BindWidget members.
	/** The row highlight image behind the row content (authored flat black in the WBP). */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UImage> Image_48;

	/** Counter name label. */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> FC_NameTextBlock;

	/** Sections count label. */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> SectionCountText;

	/** SELECT-column checkbox; its checked state is the row's selected state. */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UCheckBox> SelectedFlowCounter_ChkBox;

	bool bSelectedCached = false;
};
