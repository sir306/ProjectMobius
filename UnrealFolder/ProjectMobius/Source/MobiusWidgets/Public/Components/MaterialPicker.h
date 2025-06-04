// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
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

/**
 * Delegate for when the colour value changes
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnColourValueChanged);


/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UMaterialPicker : public UUserWidget
{
	GENERATED_BODY()

#pragma region METHODS
public:
	// Constructor 
	virtual void NativeConstruct() override;

	// Tick Method for in C++ for the widget
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

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
#pragma endregion PROPERTIES
};


