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
#include "UI/Theme/MobiusThemedUserWidget.h"  // A5: event-driven control theming base
#include "MaterialPicker.generated.h"

/**
 * Forward Declarations
 */
class USynth2DSlider;
class USlider;
class UGridPanel;
class UImage;
class UTextBlock;
class UEditableTextBox;
class UHorizontalBox;
class SMoveableWindow;

/**
 * Delegate for when the colour value changes
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnColourValueChanged);


/**
 * 
 */
UCLASS()
// A5 (2026-07-28): base changed UUserWidget -> UMobiusThemedUserWidget so this widget themes its own
// standard controls on construct + every OnThemeChanged (see UUIThemeSubsystem::ThemeStandardControlsInTree)
// instead of waiting for the value walk to find them. Pure C++ base insertion - the WBP still parents to
// this class, so no .uasset changes.
class MOBIUSWIDGETS_API UMaterialPicker : public UMobiusThemedUserWidget
{
	GENERATED_BODY()

#pragma region METHODS
public:
	// Constructor 
	virtual void NativeConstruct() override;

	// Synchronize
	virtual void SynchronizeProperties() override;

	void GetAndAssignMaterialColourValue();

	UFUNCTION()
	void OnRValueTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void OnGValueTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	
	UFUNCTION()
	void OnBValueTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void OnAValueTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	
protected:
	bool CheckTextCommittedIsNumeric(const FString& Text);
	
	void SetTextCommittedDelegates();

	UFUNCTION(BlueprintCallable, Category="MaterialPicker|Methods")
	void SetParameterNameAndTitleText(FName NewParameterName, FText NewTitleText);
	
	/**
	 * When a text box value changes due to a user text input the color and slider values needs to be updated
	 * 
	 * @param NewColour The new colour value to update the sliders and colours with
	 */
	UFUNCTION(Category="MaterialPicker|Methods")
	void UpdateSlidersAndColourValues(FLinearColor NewColour);

	/**
	 * When a picker is made, the colour values need to be updated and the UI elements need to be updated to match
	 * 
	 * @param NewColour The new colour value to update the sliders, text and colours with
	 */
	UFUNCTION(Category="MaterialPicker|Methods")
	void InitalizeColoursAndUI(FLinearColor NewColour);

	/**
	 * Creates the material instance dynamics for the synth slider, saturation slider and value slider
	 *
	 * @param[UMaterialInterface] SynthSliderMaterial - The material to create the MID for the synth slider
	 * @param[UMaterialInterface] SaturationValueSliderMaterial - The material to create the MID for the saturation and value slider
	 */
	UFUNCTION(BlueprintCallable, Category="MaterialPicker|Methods")
	void CreateMaterialInstanceDynamics(UMaterialInterface* SynthSliderMaterial, UMaterialInterface* SaturationValueSliderMaterial);
	void UpdateEditableTextColourVals() const;
	void UpdateColoursFromSliders(bool bUpdateSatAndValSliders);
	void UpdateSlidersColoursAndText(bool bUpdateSatAndValSliders = true);

	/**
	 * Method to call when the synth slider changes in the x value
	 *
	 * @param[float] NewValue The new value of the synth slider
	 */
	UFUNCTION()
	void OnColourSynthSliderXValueChanged(float NewValue);

	/**
	 * Method to call when the synth slider changes in the y value
	 *
	 * @param[float] NewValue The new value of the synth slider
	 */
	UFUNCTION()
	void OnColourSynthSliderYValueChanged(float NewValue);

	/**
	 * Method to call when the Saturation slider changes value
	 *
	 * @param[float] NewValue The new value of the synth slider
	 */
	UFUNCTION()
	void OnSaturationSliderValueChanged(float NewValue);

	/**
	 * Method to call when the value slider changes value
	 *
	 * @param[float] NewValue The new value of the synth slider
	 */
	UFUNCTION()
	void OnValueSliderValueChanged(float NewValue);

	/**
	 * Confirm the new color value and commit it to the material parameter collection and update the UI elements to match
	 */
	UFUNCTION(BlueprintCallable, Category="MaterialPicker|Methods")
	void ConfirmNewColorValue();

	/**
	 * Reset the new color value to the current value and update the UI elements to match
	 */
	UFUNCTION(BlueprintCallable, Category="MaterialPicker|Methods")
	void ResetNewColorValue();

	/**
	 * Open this picker as a themed, draggable SMoveableWindow instead of a widget pinned inside the game
	 * viewport — the same host the Settings and Custom Display cards use
	 * (MobiusPanelWindow::Open / ScalabilityPanelWidget::ShowAsWindow). An SMoveableWindow is a real
	 * SWindow, so the picker can be dragged clear of the game window and onto another monitor, which is the
	 * point: you cannot judge a colour against the heatmap while the picker is sitting on top of it.
	 *
	 * Call this INSTEAD of adding the picker to the viewport. The host detaches the widget from any current
	 * parent first, so calling it on an already-parented picker is safe.
	 *
	 * SINGLE WINDOW BY CONSTRUCTION. `CreateHeatmapMaterialPicker` in WBP_HeatmapColourBands returns a
	 * fresh picker per band click, so unlike UScalabilityPanelWidget — which reuses ONE persistent instance
	 * and can therefore guard with a plain `PanelWindow.IsValid()` — a per-instance handle here would give
	 * one window per chip clicked, and the guard would never fire because each new picker starts with a
	 * null handle. `WindowedPicker` closes whichever picker currently owns a window before opening the
	 * next, so six chips cannot leave six windows on screen.
	 */
	UFUNCTION(BlueprintCallable, Category="MaterialPicker|Window")
	void ShowAsWindow();

	/** Close the hosted window if one is open. Safe to call when it is not. */
	UFUNCTION(BlueprintCallable, Category="MaterialPicker|Window")
	void CloseWindow();

	/** True while this picker is being hosted in a window. */
	UFUNCTION(BlueprintPure, Category="MaterialPicker|Window")
	bool IsWindowOpen() const;

protected:
	/**
	 * The window holds this widget's Slate, so leaving it up past the widget's life would paint a dead
	 * tree — same reason UScalabilityPanelWidget::NativeDestruct closes its own.
	 */
	virtual void NativeDestruct() override;

public:

#pragma endregion METHODS

#pragma region COMPONENTS
public:
	/** Grid box to hold all the child components */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<UGridPanel> GridPanel;
	
	/** Image to Display current color */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<UImage> HeatmapColorImage_Current;

	/** Image to Display new color */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<UImage> HeatmapColorImage_New;

	/** Text block for title to show what this Widget color represents */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<UTextBlock> TitleTextBlock;

	/**
	 * The picker's OWN in-tree title bar (title text + close button). ShowAsWindow() collapses it, because
	 * SMoveableWindow supplies a themed title bar with its own close button and two stacked title bars read
	 * as a bug — this is what MobiusPanelWindowHost.h means by "the cards' own in-tree title bars should be
	 * collapsed".
	 *
	 * BindWidgetOptional, not BindWidget: the picker still works when shown the old in-viewport way, and a
	 * required bind would hard-fail compilation of any WBP that does not provide it.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidgetOptional))
	TObjectPtr<UGridPanel> TopBarGrid;

	/** Text block for R input hint */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<UTextBlock> RInputHintTextBlock;
	
	/** Text block for G input hint */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<UTextBlock> GInputHintTextBlock;

	/** Text block for B input hint */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<UTextBlock> BInputHintTextBlock;

	/** Text block for A input hint */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<UTextBlock> AInputHintTextBlock;
	
	/** Editable Textbox for the R Value for color */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<UEditableTextBox> RValueEditableTextBlock;

	/** Editable Textbox for the G Value for color */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<UEditableTextBox> GValueEditableTextBlock;

	/** Editable Textbox for the B Value for color */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<UEditableTextBox> BValueEditableTextBlock;

	/** Editable Textbox for the A Value for color */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<UEditableTextBox> AValueEditableTextBlock;
	
	/** Synth Slider for the UI material picker */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<USynth2DSlider> ColourPickerSynthSlider;

	/** Value slider to control saturation */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<USlider> SaturationSlider;

	/** Value slider to control colour value */
	UPROPERTY(BlueprintReadWrite, Category = "MaterialPicker|Components", meta = (BindWidget))
	TObjectPtr<USlider> ValueSlider;

	/** Material Instance Dynamic for the Synth Slider */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MaterialPicker|Materials")
	TObjectPtr<UMaterialInstanceDynamic> SynthSliderMID;
	
	/** Material Instance Dynamic for the Saturation Slider */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MaterialPicker|Materials")
	TObjectPtr<UMaterialInstanceDynamic> SaturationSliderMID;
	
	/** Material Instance Dynamic for the Value Slider */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MaterialPicker|Materials")
	TObjectPtr<UMaterialInstanceDynamic> ValueSliderMID;
	
#pragma endregion COMPONENTS

#pragma region PROPERTIES
public:
	/** Material Parameter Collection that heatmap uses */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaterialPicker")
	TObjectPtr<UMaterialParameterCollection> MaterialParameterCollection;
	
	/** Current Colour of the heatmap for this parameter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaterialPicker")
	FLinearColor CurrentColorValue;

	/** New Colour of the heatmap for this parameter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaterialPicker")
	FLinearColor NewColorValue;

	/** FName of the parameter that this picker uses */
	UPROPERTY()
	FName ParameterName = FName("");

	/** THe synth 2D slider fires x changed even if the value is the same so we have to store the previous values so
	 * not to update colours when no value has changed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaterialPicker")
	FVector2D PreviousSynthSliderValue = FVector2D(0.0f, 0.0f);

	/** Delegate to broadcast when colour change has been updated */
	UPROPERTY(BlueprintAssignable, Category = "MaterialPicker")
	FOnColourValueChanged OnColourValueChanged;

private:
	/**
	 * The window hosting this picker, when ShowAsWindow() is used. Not a UPROPERTY — it is Slate, not a
	 * UObject.
	 */
	TSharedPtr<SMoveableWindow> PanelWindow;

	/**
	 * Whichever picker currently owns a hosted window, across all instances. See ShowAsWindow(): the
	 * pickers are created per band click, so "one window at a time" cannot be enforced from a per-instance
	 * member. Weak so a GC'd picker does not keep this pointing at a dead object.
	 */
	static TWeakObjectPtr<UMaterialPicker> WindowedPicker;

	/**
	 * Set only while NativeDestruct is closing the window. MobiusPanelWindow::Close destroys the window
	 * synchronously and runs the closed-event lambda inline, so without this the "X means cancel" path
	 * would also run during teardown.
	 */
	bool bSuppressCancelOnClose = false;

#pragma endregion PROPERTIES
};


