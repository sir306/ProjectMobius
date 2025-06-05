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

#include "BaseChangePedestrianMaterial.h"

#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"

void UBaseChangePedestrianMaterial::NativePreConstruct()
{
	Super::NativePreConstruct();
}

void UBaseChangePedestrianMaterial::NativeConstruct()
{
	Super::NativeConstruct();

	// if the combo box is valid, then we can bind the method to the combo box
	if (MaterialTypeComboBox)
	{
		MaterialTypeComboBox->OnSelectionChanged.AddDynamic(this, &UBaseChangePedestrianMaterial::OnMaterialTypeChanged);
	}

	// if the random clothing checkbox is valid, then we can bind the method to the checkbox
	if (RandomClothingCheckBox)
	{
		RandomClothingCheckBox->OnCheckStateChanged.AddDynamic(this, &UBaseChangePedestrianMaterial::OnRandomClothingCheckBoxChanged);
	}

	// if the material property slider is valid, then we can bind the method to the slider
	if (MaterialPropertySlider)
	{
		MaterialPropertySlider->OnValueChanged.AddDynamic(this, &UBaseChangePedestrianMaterial::OnMaterialPropertySliderChanged);
	}

	if(RepresentationSubsystem == nullptr)
	{
		RepresentationSubsystem = GetWorld()->GetSubsystem<UMRS_RepresentationSubsystem>();
	}

	// Make sure the UI matches the current material instance
	if (MaterialTypeComboBox && RandomClothingCheckBox && CurrentSelectedMaleMaterialInstance && CurrentSelectedFemaleMaterialInstance && RepresentationSubsystem)
	{
		// Make sure the random clothing matches the current random setting
		OnRandomClothingCheckBoxChanged(RandomClothingCheckBox->IsChecked());
		
		// ensure the materials are set on the agents
		UpdateRepSubsystemMaterialInstances();	

		// Ensure the slider properties are updated
		UpdateSliderProperties();
		
	}
}

void UBaseChangePedestrianMaterial::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// if the material array isn't empty, then we can assign the material names to the combo box
	if (MaterialTypeComboBox && MatInstDynamicDisplayNames.Num() > 0)
	{
		AssignMaterialNamesToComboBox();
	}

	if(RandomClothingText)
	{
		RandomClothingText->SetText(FText::FromString("Random Clothing Colour"));
	}
}

void UBaseChangePedestrianMaterial::ConvertMaterialsToDynamicMaterialInstances(
	const TArray<UMaterialInstance*>& AdultMaleMaterials,
	const TArray<UMaterialInstance*>& ElderlyMaleMaterials,
	const TArray<UMaterialInstance*>& AdultFemaleMaterials,
	const TArray<UMaterialInstance*>& ElderlyFemaleMaterials,
	const TArray<UMaterialInstance*>& ChildrenMaterials,
	const TArray<FString> DisplayName)
{
	// Clear the material instances array
	MatInstDynamicDisplayNames.Empty();

	// TODO: this check need to be better and handled differently
	// Check that the arrays are the same length
	if(
		AdultMaleMaterials.Num() == AdultFemaleMaterials.Num() &&
		AdultMaleMaterials.Num() == ChildrenMaterials.Num() &&
		ElderlyMaleMaterials.Num() == ElderlyFemaleMaterials.Num() &&
		DisplayName.Num() == 2)
	{
		for(int32 i = 0; i < (AdultMaleMaterials.Num() / 2); i++)
		{
			UE_LOG(LogTemp, Warning, TEXT("1. i: %d"), i);
			// turn the material into a dynamic material instance
			// Adult Materials Body
			UMaterialInstanceDynamic* MaleDynamicMaterialInst = UMaterialInstanceDynamic::Create(AdultMaleMaterials[i*2], this);
			UMaterialInstanceDynamic* FemaleDynamicMaterialInst = UMaterialInstanceDynamic::Create(AdultFemaleMaterials[i*2], this);
			// Elderly Materials Body
			UMaterialInstanceDynamic* ElderlyMaleDynamicMaterialInst = UMaterialInstanceDynamic::Create(ElderlyMaleMaterials[i*2], this);
			UMaterialInstanceDynamic* ElderlyFemaleDynamicMaterialInst = UMaterialInstanceDynamic::Create(ElderlyFemaleMaterials[i*2], this);
			// Children Materials Body
			UMaterialInstanceDynamic* ChildDynamicMaterialInst = UMaterialInstanceDynamic::Create(ChildrenMaterials[i*2], this);

			// Adult Materials Eyes
			UMaterialInstanceDynamic* MaleEyesDynamicMaterialInst = UMaterialInstanceDynamic::Create(AdultMaleMaterials[i*2 + 1], this);
			UMaterialInstanceDynamic* FemaleEyesDynamicMaterialInst = UMaterialInstanceDynamic::Create(AdultFemaleMaterials[i*2 + 1], this);
			// Elderly Materials Eyes
			UMaterialInstanceDynamic* ElderlyMaleEyesDynamicMaterialInst = UMaterialInstanceDynamic::Create(ElderlyMaleMaterials[i*2 + 1], this);
			UMaterialInstanceDynamic* ElderlyFemaleEyesDynamicMaterialInst = UMaterialInstanceDynamic::Create(ElderlyFemaleMaterials[i*2 + 1], this);
			// Children Materials Eyes
			UMaterialInstanceDynamic* ChildEyesDynamicMaterialInst = UMaterialInstanceDynamic::Create(ChildrenMaterials[i*2 + 1], this);
			
			// log i
			UE_LOG(LogTemp, Warning, TEXT("2. i: %d"), i);
			
			// Add the Index as the key and the corresponding display name as the value
			MatInstDynamicDisplayNames.Add(i,DisplayName[i]);

			// add the dynamic material instance to the arrays
			// Male Adult Material Instances
			MaleMaterialDynamicInstances.Add(MaleDynamicMaterialInst);
			MaleMaterialDynamicInstances.Add(MaleEyesDynamicMaterialInst);

			// Elderly Male Material Instances
			ElderlyMaleMaterialDynamicInstances.Add(ElderlyMaleDynamicMaterialInst);
			ElderlyMaleMaterialDynamicInstances.Add(ElderlyMaleEyesDynamicMaterialInst);

			// Elderly Female Material Instances
			ElderlyFemaleMaterialDynamicInstances.Add(ElderlyFemaleDynamicMaterialInst);
			ElderlyFemaleMaterialDynamicInstances.Add(ElderlyFemaleEyesDynamicMaterialInst);

			// Female Adult Material Instances
			FemaleMaterialDynamicInstances.Add(FemaleDynamicMaterialInst);
			FemaleMaterialDynamicInstances.Add(FemaleEyesDynamicMaterialInst);
			
			//Children Material Instances
			ChildrenMaterialDynamicInstances.Add(ChildDynamicMaterialInst);
			ChildrenMaterialDynamicInstances.Add(ChildEyesDynamicMaterialInst);
		}
	}
	
	// Assign the material names to the combo box
	AssignMaterialNamesToComboBox(); 
}

void UBaseChangePedestrianMaterial::AddMaterialTypeComponents()
{
	// for now just create one checkbox and text block to toggle random clothing
	
}

void UBaseChangePedestrianMaterial::AssignMaterialNamesToComboBox()
{
	// Clear the combo box - we cant check the length of array against the combo box items as it could be same length but different values
	MaterialTypeComboBox->ClearOptions();

	for(auto MaterialInst : MatInstDynamicDisplayNames)
	{
		// Add the display name to the combo box
		MaterialTypeComboBox->AddOption(MaterialInst.Value);

		// Does the MaterialInst match the starting material of the pedestrian? // TODO
		if (MaterialInst.Key == 0)
		{
			// Set the current selected material instances to the starting material
			// Male Adults
			CurrentSelectedMaleMaterialInstance = MaleMaterialDynamicInstances[MaterialInst.Key * 2];
			CurrentSelectedMaleEyesMaterialInstance = MaleMaterialDynamicInstances[(MaterialInst.Key * 2) + 1];

			// Elderly Males
			CurrentSelectedMaleElderlyMaterialInstance = ElderlyMaleMaterialDynamicInstances[MaterialInst.Key * 2];
			CurrentSelectedMaleElderlyEyesMaterialInstance = ElderlyMaleMaterialDynamicInstances[(MaterialInst.Key * 2) + 1];

			// Female Adults
			CurrentSelectedFemaleMaterialInstance = FemaleMaterialDynamicInstances[MaterialInst.Key * 2];
			CurrentSelectedFemaleEyesMaterialInstance = FemaleMaterialDynamicInstances[(MaterialInst.Key * 2) + 1];

			// Female Elderly
			CurrentSelectedFemaleElderlyMaterialInstance = ElderlyFemaleMaterialDynamicInstances[MaterialInst.Key * 2];
			CurrentSelectedFemaleElderlyEyesMaterialInstance = ElderlyFemaleMaterialDynamicInstances[(MaterialInst.Key * 2) + 1];

			// Children
			CurrentSelectedChildMaterialInstance = ChildrenMaterialDynamicInstances[MaterialInst.Key * 2];
			CurrentSelectedChildEyesMaterialInstance = ChildrenMaterialDynamicInstances[(MaterialInst.Key * 2) + 1];

			
			MaterialTypeComboBox->SetSelectedOption(MaterialInst.Value);
		}
	}
}

void UBaseChangePedestrianMaterial::OnMaterialTypeChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// Check that the combo box has options and the number matches the material instances
	if(MaterialTypeComboBox->GetOptionCount() == MatInstDynamicDisplayNames.Num() && MaterialTypeComboBox->GetOptionCount() > 0)
	{
		// Check that the index is valid
		if(MaterialTypeComboBox->FindOptionIndex(SelectedItem) != INDEX_NONE)
		{
			int32 Index = MaterialTypeComboBox->FindOptionIndex(SelectedItem) * 2;
			// Set the current selected material instances to the starting material
			// Male Adults
			CurrentSelectedMaleMaterialInstance = MaleMaterialDynamicInstances[Index];
			CurrentSelectedMaleEyesMaterialInstance = MaleMaterialDynamicInstances[Index + 1];

			// Male Elderly
			CurrentSelectedMaleElderlyMaterialInstance = ElderlyMaleMaterialDynamicInstances[Index];
			CurrentSelectedMaleElderlyEyesMaterialInstance = ElderlyMaleMaterialDynamicInstances[Index + 1];
			
			// Female Adults
			CurrentSelectedFemaleMaterialInstance = FemaleMaterialDynamicInstances[Index];
			CurrentSelectedFemaleEyesMaterialInstance = FemaleMaterialDynamicInstances[Index + 1];

			// Female Elderly
			CurrentSelectedFemaleElderlyMaterialInstance = ElderlyFemaleMaterialDynamicInstances[Index];
			CurrentSelectedFemaleElderlyEyesMaterialInstance = ElderlyFemaleMaterialDynamicInstances[Index + 1];
			
			// Children
			CurrentSelectedChildMaterialInstance = ChildrenMaterialDynamicInstances[Index];
			CurrentSelectedChildEyesMaterialInstance = ChildrenMaterialDynamicInstances[Index + 1];

			// Change the material of the pedestrian
			//ChangePedestrianMaterial(SelectedMaterialInst);

			// update the material on the pedestrian
			UpdateRepSubsystemMaterialInstances();

			// Make sure the random clothing matches the current random setting
			if(RandomClothingCheckBox)
			{
				OnRandomClothingCheckBoxChanged(RandomClothingCheckBox->IsChecked());
			}

			// update slider
			UpdateSliderProperties();
		}
	}

	// TODO: Implement the logic to change the material of the pedestrian
}

void UBaseChangePedestrianMaterial::OnRandomClothingCheckBoxChanged(bool bIsChecked)
{
	if(bIsChecked)
	{
		// Male Adult
		CurrentSelectedMaleMaterialInstance->SetScalarParameterValue("RandomPersonGen", 1.0f);
		// Male Elderly
		CurrentSelectedMaleElderlyMaterialInstance->SetScalarParameterValue("RandomPersonGen", 1.0f);
		// Female Adult
		CurrentSelectedFemaleMaterialInstance->SetScalarParameterValue("RandomPersonGen", 1.0f);
		// Female Elderly
		CurrentSelectedFemaleElderlyMaterialInstance->SetScalarParameterValue("RandomPersonGen", 1.0f);
		// Children
		CurrentSelectedChildMaterialInstance->SetScalarParameterValue("RandomPersonGen", 1.0f);
	}
	else
	{
		// Male Adult
		CurrentSelectedMaleMaterialInstance->SetScalarParameterValue("RandomPersonGen", 0.0f);
		// Male Elderly
		CurrentSelectedMaleElderlyMaterialInstance->SetScalarParameterValue("RandomPersonGen", 0.0f);
		// Female Adult
		CurrentSelectedFemaleMaterialInstance->SetScalarParameterValue("RandomPersonGen", 0.0f);
		// Female Elderly
		CurrentSelectedFemaleElderlyMaterialInstance->SetScalarParameterValue("RandomPersonGen", 0.0f);
		// Children
		CurrentSelectedChildMaterialInstance->SetScalarParameterValue("RandomPersonGen", 0.0f);
	}
	// update the material on the pedestrian
	UpdateRepSubsystemMaterialInstances();
}

void UBaseChangePedestrianMaterial::OnMaterialPropertySliderChanged(float Value)
{
	// check CurrentMaterialProperty and update the slider properties
	if(!CurrentMaterialProperty.IsEmpty())
	{
		FName ParamName = FName(CurrentMaterialProperty);

		// Male Adult
		CurrentSelectedMaleMaterialInstance->SetScalarParameterValue(ParamName, Value);
		CurrentSelectedMaleEyesMaterialInstance->SetScalarParameterValue(ParamName, Value);

		// Male Elderly
		CurrentSelectedMaleElderlyMaterialInstance->SetScalarParameterValue(ParamName, Value);
		CurrentSelectedMaleElderlyEyesMaterialInstance->SetScalarParameterValue(ParamName, Value);
		
		// Female Adult
		CurrentSelectedFemaleMaterialInstance->SetScalarParameterValue(ParamName, Value);
		CurrentSelectedFemaleEyesMaterialInstance->SetScalarParameterValue(ParamName, Value);

		// Female Elderly
		CurrentSelectedFemaleElderlyMaterialInstance->SetScalarParameterValue(ParamName, Value);
		CurrentSelectedFemaleElderlyEyesMaterialInstance->SetScalarParameterValue(ParamName, Value);

		// Children
		CurrentSelectedChildMaterialInstance->SetScalarParameterValue(ParamName, Value);
		CurrentSelectedChildEyesMaterialInstance->SetScalarParameterValue(ParamName, Value);

		// TODO: we may want to display a value to a user
		// Set the text block to the current material property
		//PropertySliderText->SetText(FText::FromString("Current " + CurrentMaterialProperty));

		// update the material on the pedestrian
		UpdateRepSubsystemMaterialInstances();
	}
}

void UBaseChangePedestrianMaterial::UpdateSliderProperties()
{
	// Check that the combo box has options and the number matches the material instances
	if(MaterialTypeComboBox->GetOptionCount() == MatInstDynamicDisplayNames.Num() && MaterialTypeComboBox->GetOptionCount() > 0)
	{
		
		// Check that the index is valid
		if(MaterialTypeComboBox->GetSelectedIndex() != INDEX_NONE)
		{
			// find the material property that is being updated by looking at the selected combo box item
			if(MaterialTypeComboBox->GetSelectedOption() == "Solid Colour")
			{
				// not sure what yet
				CurrentMaterialProperty = FString("");
			}
			else if(MaterialTypeComboBox->GetSelectedOption() == "Translucent")
			{
				CurrentMaterialProperty = FString("Opacity");
			}
			else
			{
				// NOT valid or implemented
				CurrentMaterialProperty = FString("");
			}
		}
	}

	// check CurrentMaterialProperty and update the slider properties
	if(!CurrentMaterialProperty.IsEmpty())
	{
		float CurrentVal;
		FName ParamName = FName(CurrentMaterialProperty);
		FMaterialParameterInfo ParameterInfo(ParamName);
		

		if(CurrentSelectedMaleMaterialInstance->GetScalarParameterValue(ParameterInfo, CurrentVal))
		{
			MaterialPropertySlider->SetValue(CurrentVal);
		}

		// Set the text block to the current material property
		PropertySliderText->SetText(FText::FromString("Current " + CurrentMaterialProperty));
		//PropertySliderText->SetText(FText::FromString("Current " + CurrentMaterialProperty + ": " + FString::SanitizeFloat(CurrentVal))); //TODO this needs to be done in slider changed too
		
		float SliderMin = 0.0f;
		float SliderMax = 1.0f;
		// Getting scalar parameter slider min max is only available in editor - so while debugging we can use it but have to implement extra logic for non-editor builds
#if WITH_EDITOR
		// Get the min and max values of the slider	if they exist - NOTE: scalar parameter slider min max is 0.0 by default so check they match expected values
		if(CurrentSelectedMaleMaterialInstance->GetScalarParameterSliderMinMax(ParameterInfo, SliderMin, SliderMax))
		{
			MaterialPropertySlider->SetMinValue(SliderMin);
			MaterialPropertySlider->SetMaxValue(SliderMax);
			SliderMinValText->SetText(FText::FromString(FString::SanitizeFloat(SliderMin)));
			SliderMaxValText->SetText(FText::FromString(FString::SanitizeFloat(SliderMax)));
		}
#else
		MaterialPropertySlider->SetMinValue(SliderMin);
		MaterialPropertySlider->SetMaxValue(SliderMax);
		SliderMinValText->SetText(FText::FromString(FString::SanitizeFloat(SliderMin)));
		SliderMaxValText->SetText(FText::FromString(FString::SanitizeFloat(SliderMax)));

#endif
		
		// if the current material property is valid then it should be visible in case it was hidden
		SliderMinValText->SetVisibility(ESlateVisibility::Visible);
		SliderMaxValText->SetVisibility(ESlateVisibility::Visible);
		MaterialPropertySlider->SetVisibility(ESlateVisibility::Visible);
		PropertySliderText->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		// TODO: until i figure the best way to handle resizing and scaling of the widget, we will hide the slider and text this way the widget will not be resized
		// if the current material property is empty then we should hide the slider and text as this is not an implemented material property
		SliderMinValText->SetVisibility(ESlateVisibility::Hidden);
		SliderMaxValText->SetVisibility(ESlateVisibility::Hidden);
		MaterialPropertySlider->SetVisibility(ESlateVisibility::Hidden);
		PropertySliderText->SetVisibility(ESlateVisibility::Hidden); 
	}
}

void UBaseChangePedestrianMaterial::UpdateRepSubsystemMaterialInstances()
{
	// update the material on the pedestrian
		if(RepresentationSubsystem)
		{
			// adult
			RepresentationSubsystem->SetPedestrianMaterial(CurrentSelectedMaleMaterialInstance, CurrentSelectedMaleEyesMaterialInstance, EPedestrianGender::Epg_Male, EAgeDemographic::Ead_Adult);
			RepresentationSubsystem->SetPedestrianMaterial(CurrentSelectedFemaleMaterialInstance, CurrentSelectedFemaleEyesMaterialInstance, EPedestrianGender::Epg_Female, EAgeDemographic::Ead_Adult);
			// Elderly
			RepresentationSubsystem->SetPedestrianMaterial(CurrentSelectedMaleElderlyMaterialInstance, CurrentSelectedMaleElderlyEyesMaterialInstance, EPedestrianGender::Epg_Male, EAgeDemographic::Ead_Elderly);
			RepresentationSubsystem->SetPedestrianMaterial(CurrentSelectedFemaleElderlyMaterialInstance, CurrentSelectedFemaleElderlyEyesMaterialInstance, EPedestrianGender::Epg_Female, EAgeDemographic::Ead_Elderly);
			// when we do different gender for children we will need to set the gender parameter
			RepresentationSubsystem->SetPedestrianMaterial(CurrentSelectedChildMaterialInstance, CurrentSelectedChildEyesMaterialInstance, EPedestrianGender::Epg_Default, EAgeDemographic::Ead_Child);
		}
		else
		{
			RepresentationSubsystem = GetWorld()->GetSubsystem<UMRS_RepresentationSubsystem>();
			// adult
			RepresentationSubsystem->SetPedestrianMaterial(CurrentSelectedMaleMaterialInstance, CurrentSelectedMaleEyesMaterialInstance, EPedestrianGender::Epg_Male, EAgeDemographic::Ead_Adult);
			RepresentationSubsystem->SetPedestrianMaterial(CurrentSelectedFemaleMaterialInstance, CurrentSelectedFemaleEyesMaterialInstance, EPedestrianGender::Epg_Female, EAgeDemographic::Ead_Adult);
			// Elderly
			RepresentationSubsystem->SetPedestrianMaterial(CurrentSelectedMaleElderlyMaterialInstance, CurrentSelectedMaleElderlyEyesMaterialInstance, EPedestrianGender::Epg_Male, EAgeDemographic::Ead_Elderly);
			RepresentationSubsystem->SetPedestrianMaterial(CurrentSelectedFemaleElderlyMaterialInstance, CurrentSelectedFemaleEyesMaterialInstance, EPedestrianGender::Epg_Female, EAgeDemographic::Ead_Elderly);
			// when we do different gender for children we will need to set the gender parameter
			RepresentationSubsystem->SetPedestrianMaterial(CurrentSelectedChildMaterialInstance, CurrentSelectedChildEyesMaterialInstance, EPedestrianGender::Epg_Default, EAgeDemographic::Ead_Child);
		}
}
