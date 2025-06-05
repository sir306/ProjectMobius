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
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
#include "BaseChangePedestrianMaterial.generated.h"

class USlider;
class UComboBoxString;
class UMaterialTypeCheckbox;
class UCheckBox;
class UTextBlock;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UBaseChangePedestrianMaterial : public UUserWidget
{
	GENERATED_BODY()
#pragma region METHODS
public:
	// Native Pre Construct
	virtual void NativePreConstruct() override;

	// Native Constructor
	virtual void NativeConstruct() override;

	// Synchronize properties with the widget
	virtual void SynchronizeProperties() override;

	/**
	 * Takes an array of materials to convert into dynamic material instances
	 *
	 * @param[TArray<UMaterialInstance*>] AdultMaleMaterials - The array of materials to convert into dynamic material instances for Adult Males
	 * @param[TArray<UMaterialInstance*>] ElderlyMaleMaterials - The array of materials to convert into dynamic material instances for Elderly Males
	 * @param[TArray<UMaterialInstance*>] AdultFemaleMaterials - The array of materials to convert into dynamic material instances for Adult Females
	 * @param[TArray<UMaterialInstance*>] ElderlyFemaleMaterials - The array of materials to convert into dynamic material instances for Elderly Females
	 * @param[TArray<UMaterialInstance*>] ChildrenMaterials - The array of materials to convert into dynamic material instances for Children
	 * @param[TArray<FString>] DisplayName - The display name for the material
	 */
	UFUNCTION(BlueprintCallable)
	void ConvertMaterialsToDynamicMaterialInstances(
		const TArray<UMaterialInstance*>& AdultMaleMaterials,
		const TArray<UMaterialInstance*>& ElderlyMaleMaterials,
		const TArray<UMaterialInstance*>& AdultFemaleMaterials,
		const TArray<UMaterialInstance*>& ElderlyFemaleMaterials,
		const TArray<UMaterialInstance*>& ChildrenMaterials,
		const TArray<FString> DisplayName);

	// TODO: more user settings and customization for the material types will be desired but for now they will be given fixed inputs
	/**
	 * Add Toggle boxes and text to the widget depending on the material type selected
	 */
	void AddMaterialTypeComponents();
	

protected:
	/** Method to assign the material names to the combo box */
	void AssignMaterialNamesToComboBox();

	/** Method to handle when the material combo box value changes */
	UFUNCTION()
	void OnMaterialTypeChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	/** Method to handle when the clothing colour checkbox changes */
	UFUNCTION(BlueprintCallable)
	void OnRandomClothingCheckBoxChanged(bool bIsChecked);

	/** Method to handle the event when the slider value changes */
	UFUNCTION()
	void OnMaterialPropertySliderChanged(float Value);

	/** Method to update slider properties, like what material property is being updated */
	void UpdateSliderProperties();

	/**
	 * Update the stored material instances to the selected material instance
	 */
	void UpdateRepSubsystemMaterialInstances();

#pragma endregion

#pragma region UI_COMPONENTS
public:


protected:
	/** Material type drop box for different material types */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UComboBoxString> MaterialTypeComboBox;

	/** Store the current selected material instance for male adult body */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrian Material")
	TObjectPtr<UMaterialInstanceDynamic> CurrentSelectedMaleMaterialInstance;

	/** Store the current selected material instance for male adult eyes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrian Material")
	TObjectPtr<UMaterialInstanceDynamic> CurrentSelectedMaleEyesMaterialInstance;

	/** Store the current selected material instance for male Elderly body */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrian Material")
	TObjectPtr<UMaterialInstanceDynamic> CurrentSelectedMaleElderlyMaterialInstance;

	/** Store the current selected material instance for male Elderly eyes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrian Material")
	TObjectPtr<UMaterialInstanceDynamic> CurrentSelectedMaleElderlyEyesMaterialInstance;
	
	/** Store the current selected material instance for female adult body */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrian Material")
	TObjectPtr<UMaterialInstanceDynamic> CurrentSelectedFemaleMaterialInstance;

	/** Store the current selected material instance for female adult eyes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrian Material")
	TObjectPtr<UMaterialInstanceDynamic> CurrentSelectedFemaleEyesMaterialInstance;

	/** Store the current selected material instance for female Elderly body */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrian Material")
	TObjectPtr<UMaterialInstanceDynamic> CurrentSelectedFemaleElderlyMaterialInstance;

	/** Store the current selected material instance for female Elderly eyes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrian Material")
	TObjectPtr<UMaterialInstanceDynamic> CurrentSelectedFemaleElderlyEyesMaterialInstance;

	//TODO: currently doing non gender specific children
	/** Store the current selected material instance for child body */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrian Material")
	TObjectPtr<UMaterialInstanceDynamic> CurrentSelectedChildMaterialInstance;

	/** Store the current selected material instance for child eyes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrian Material")
	TObjectPtr<UMaterialInstanceDynamic> CurrentSelectedChildEyesMaterialInstance;

	/** Checkbox to toggle random color or grey clothing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UCheckBox> RandomClothingCheckBox;

	/** Text associated with the random clothing check box */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> RandomClothingText;

	/** Text title for the slider value changer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> PropertySliderText;

	/** Text for min slider value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> SliderMinValText;

	/** Text for max slider value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> SliderMaxValText;

	/** Slider for adjusting properties like opacity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<USlider> MaterialPropertySlider;
	
	// TODO add the components for the material types that we want to change 


private:

#pragma endregion	
	
#pragma region PROPERTIES
public:
	/** The desired display name for the material */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pedestrian Material")
	TMap<int32, FString> MatInstDynamicDisplayNames;

	// While the materials are being set the same, each mesh will require different materials to vertex animate correctly
	/** Materials for adult males */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pedestrian Material")
	TArray<UMaterialInstanceDynamic*> MaleMaterialDynamicInstances;

	/** Materials for elderly males */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pedestrian Material")
	TArray<UMaterialInstanceDynamic*> ElderlyMaleMaterialDynamicInstances;

	/** Materials for adult females */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pedestrian Material")
	TArray<UMaterialInstanceDynamic*> FemaleMaterialDynamicInstances;

	/** Materials for elderly females */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pedestrian Material")
	TArray<UMaterialInstanceDynamic*> ElderlyFemaleMaterialDynamicInstances;

	/** Materials for children */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pedestrian Material")
	TArray<UMaterialInstanceDynamic*> ChildrenMaterialDynamicInstances;

	/** To prevent the representation subsystem from destroying and keep the spawn materials we store a ptr to it */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pedestrian Material|Subsystem")
	TObjectPtr<UMRS_RepresentationSubsystem> RepresentationSubsystem;
	
protected:
	// TODO add the properties for the material types that we want to change and ptrs to the material types

private:
	// String value to represent what the current material property is being changed for the slider
	FString CurrentMaterialProperty;
	

#pragma endregion	
};
