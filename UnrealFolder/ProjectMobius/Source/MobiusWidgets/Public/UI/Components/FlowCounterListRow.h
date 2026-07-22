// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
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
 */
UCLASS()
class MOBIUSWIDGETS_API UFlowCounterListRow : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Apply the selected / idle visual (accent+white vs transparent+primary) for the current theme. */
	UFUNCTION(BlueprintCallable, Category = "FlowCounterRow")
	void SetRowSelected(bool bSelected);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	/** Re-apply the current visual for the current theme (OnThemeChanged handler + checkbox handler). */
	UFUNCTION()
	void HandleThemeChanged();

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

	TWeakObjectPtr<UUIThemeSubsystem> ThemeSubsystem;
	bool bThemeBound = false;
	bool bSelectedCached = false;
};
