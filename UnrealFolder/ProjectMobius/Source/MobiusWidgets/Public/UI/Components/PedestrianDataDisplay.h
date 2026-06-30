// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EnumsAndStructs/AgentMeshViewer.h"
#include "PedestrianDataDisplay.generated.h"

class UAgentInfoDisplay;
class UFieldAndTextWidget;
class UTextBlock;
class UGridPanel;

// Delegates
/** When the visibility of the selected agent component changes we need notify other classes that require it in blueprints or code */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectedAgentComponentVisibilityChanged, bool, bIsVisible);

/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UPedestrianDataDisplay : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void SynchronizeProperties() override;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	
	void ConfigureTextBlockStyles() const;

	void SetupTextBlockTitles() const;

	void UpdateFieldTextBlocks() const;

	void ResizeGridPanelParentSlotToFitLargeText(FVector2D& InTextSize) const;

	void ResizeScreenGridToDefaultSize() const;

	void GetScreenGridCoefficients(int32 Col, int32 Row, float& OutWidthCoefficient, float& OutHeightCoefficient) const;
	
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
public:
	/** Grid Panel to arrange sizing for the whole screen */
	UPROPERTY(EditAnywhere,BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UGridPanel> ScreenGrid;

	/** Grid Panel to arrange items */
	UPROPERTY(EditAnywhere,BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UGridPanel> WidgetHeadGridPanel;

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
};
