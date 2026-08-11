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

#include "UI/Components/BaseChangePedestrianMaterial.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"
#include "UI/Theme/UIThemeSubsystem.h"

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
	if (MaterialTypeComboBox && RandomClothingCheckBox && CurrentSelectedMaleMaterialInstance && CurrentSelectedFemaleMaterialInstance && RepresentationSubsystem && bInDestopMode)
	{
		// Make sure the random clothing matches the current random setting
		OnRandomClothingCheckBoxChanged(RandomClothingCheckBox->IsChecked());

		// ensure the materials are set on the agents
		UpdateRepSubsystemMaterialInstances();

		// Ensure the slider properties are updated
		UpdateSliderProperties();

	}
}

void UBaseChangePedestrianMaterial::ApplyMobiusTheme_Implementation()
{
	if (UUIThemeSubsystem* T = GetThemeSubsystem())
	{
		T->ReapplyToUserWidget(this);
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
	const TArray<FString> DisplayName,
	bool bDesktopMode)
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
	if (bDesktopMode)
	{
		// Assign the material names to the combo box
		AssignMaterialNamesToComboBox();
	}

	// Next tick, not now: the wheelchair array is filled by a SEPARATE Blueprint node that may legally
	// run either side of this one, so an inline check would false-alarm on a graph that wires it second.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this]() { ValidateMaterialArrayWiring(); }));
	}
}

void UBaseChangePedestrianMaterial::ValidateMaterialArrayWiring()
{
	// Only meaningful once the human arrays exist - an empty widget is not a mis-wired one.
	if (MaleMaterialDynamicInstances.Num() == 0)
	{
		return;
	}

	// Both companion arrays fail the same way and are reported the same way. Each is filled by its own
	// Blueprint node, so either can be missing independently.
	struct FCompanionArrayCheck
	{
		const TArray<UMaterialInstanceDynamic*>& Instances;
		const TCHAR* NodeName;
		const TCHAR* Consequence;
	};

	const FCompanionArrayCheck Checks[] = {
		{ WheelchairMaterialDynamicInstances, TEXT("ConvertWheelchairMaterialsToDynamicInstances"),
		  TEXT("wheelchairs will not follow the material selection at either spec level") },
		{ LowSpecMaterialDynamicInstances, TEXT("ConvertLowSpecMaterialsToDynamicInstances"),
		  TEXT("the material selection will do nothing while the low-spec agents are on screen") }
	};

	for (const FCompanionArrayCheck& Check : Checks)
	{
		if (Check.Instances.Num() == 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: human material arrays are populated but the array filled by %s is EMPTY, so %s. ")
				TEXT("Wire that node with one material per combo option, in the same order as the ")
				TEXT("DisplayName list passed to ConvertMaterialsToDynamicMaterialInstances."),
				*GetName(), Check.NodeName, Check.Consequence);
			continue;
		}

		// Half-filled is worse than empty: the combo offers options the array cannot honour, so
		// selecting a later one leaves those agents on whatever they last had while the rest update.
		if (Check.Instances.Num() != MatInstDynamicDisplayNames.Num())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: %s produced %d material(s) but the combo offers %d option(s). Options beyond ")
				TEXT("that count will leave those agents on their previous material."),
				*GetName(), Check.NodeName, Check.Instances.Num(), MatInstDynamicDisplayNames.Num());
		}
	}
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

			// Empty wheelchair - RAW index, no *2: body only, there is no eyes entry.
			// IsValidIndex rather than an assumption, because this array is populated by a separate
			// Blueprint call that an older graph may simply not make.
			if (WheelchairMaterialDynamicInstances.IsValidIndex(MaterialInst.Key))
			{
				CurrentSelectedWheelchairMaterialInstance = WheelchairMaterialDynamicInstances[MaterialInst.Key];
			}

			// Low-spec SimpleAgent - RAW index for the same reason: one material per option, shared by
			// every human demographic, no eyes entry.
			if (LowSpecMaterialDynamicInstances.IsValidIndex(MaterialInst.Key))
			{
				CurrentSelectedLowSpecMaterialInstance = LowSpecMaterialDynamicInstances[MaterialInst.Key];
			}


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

			// Empty wheelchair - Index is already the combo index doubled for the body/eyes stride,
			// so halve it back. The chair has one material slot, so it indexes 1:1 with the options.
			const int32 SingleSlotIndex = Index / 2;
			if (WheelchairMaterialDynamicInstances.IsValidIndex(SingleSlotIndex))
			{
				CurrentSelectedWheelchairMaterialInstance = WheelchairMaterialDynamicInstances[SingleSlotIndex];
			}

			// Low-spec SimpleAgent - same single-slot indexing as the chair.
			if (LowSpecMaterialDynamicInstances.IsValidIndex(SingleSlotIndex))
			{
				CurrentSelectedLowSpecMaterialInstance = LowSpecMaterialDynamicInstances[SingleSlotIndex];
			}

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

void UBaseChangePedestrianMaterial::ConvertWheelchairMaterialsToDynamicInstances(
	const TArray<UMaterialInstance*>& WheelchairMaterials)
{
	// One MID per combo option, in the same order as the DisplayName list the five human arrays use.
	// No body/eyes pairing and no *2 stride: the chair mesh has a single material slot, which is why
	// this cannot simply be a sixth parameter on ConvertMaterialsToDynamicMaterialInstances - that
	// function's length check and its i*2 indexing both assume pairs.
	WheelchairMaterialDynamicInstances.Reset(WheelchairMaterials.Num());

	for (UMaterialInstance* SourceMaterial : WheelchairMaterials)
	{
		if (SourceMaterial == nullptr)
		{
			continue;
		}
		WheelchairMaterialDynamicInstances.Add(UMaterialInstanceDynamic::Create(SourceMaterial, this));
	}

	// Adopt the current selection immediately, so a graph that calls this AFTER the five human arrays
	// have already been built and selected does not leave the chair on a null material until the
	// user next touches the combo box.
	const int32 SelectedIndex = MaterialTypeComboBox ? MaterialTypeComboBox->GetSelectedIndex() : 0;
	const int32 SafeIndex = WheelchairMaterialDynamicInstances.IsValidIndex(SelectedIndex) ? SelectedIndex : 0;
	if (WheelchairMaterialDynamicInstances.IsValidIndex(SafeIndex))
	{
		CurrentSelectedWheelchairMaterialInstance = WheelchairMaterialDynamicInstances[SafeIndex];
		UpdateRepSubsystemMaterialInstances();
	}
}

void UBaseChangePedestrianMaterial::ConvertLowSpecMaterialsToDynamicInstances(
	const TArray<UMaterialInstance*>& LowSpecMaterials)
{
	// One MID per combo option. The low-spec system renders every human from the same SimpleAgent mesh,
	// so there is no per-demographic material and no eyes variant - which is why this is a separate
	// entry point rather than five more arrays on ConvertMaterialsToDynamicMaterialInstances.
	LowSpecMaterialDynamicInstances.Reset(LowSpecMaterials.Num());

	for (UMaterialInstance* SourceMaterial : LowSpecMaterials)
	{
		if (SourceMaterial == nullptr)
		{
			continue;
		}
		LowSpecMaterialDynamicInstances.Add(UMaterialInstanceDynamic::Create(SourceMaterial, this));
	}

	// Adopt the current selection immediately - same reasoning as the wheelchair conversion: a graph
	// that wires this after the human arrays have already been built would otherwise leave the low-spec
	// agents on a null material until the user next touches the combo box.
	const int32 SelectedIndex = MaterialTypeComboBox ? MaterialTypeComboBox->GetSelectedIndex() : 0;
	const int32 SafeIndex = LowSpecMaterialDynamicInstances.IsValidIndex(SelectedIndex) ? SelectedIndex : 0;
	if (LowSpecMaterialDynamicInstances.IsValidIndex(SafeIndex))
	{
		CurrentSelectedLowSpecMaterialInstance = LowSpecMaterialDynamicInstances[SafeIndex];
		UpdateRepSubsystemMaterialInstances();
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
			// Empty wheelchair - no gender, no age: one chair mesh serves every wheelchair agent.
			RepresentationSubsystem->SetWheelchairMaterial(CurrentSelectedWheelchairMaterialInstance);
			// Low-spec humans - one SimpleAgent material for all five demographics. Pushed unconditionally
			// alongside the high-spec set; the subsystem decides which one reaches the live component
			// based on the current spec level, and stores both either way.
			RepresentationSubsystem->SetLowSpecPedestrianMaterial(CurrentSelectedLowSpecMaterialInstance);
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
			// Empty wheelchair - no gender, no age: one chair mesh serves every wheelchair agent.
			RepresentationSubsystem->SetWheelchairMaterial(CurrentSelectedWheelchairMaterialInstance);
			// Low-spec humans - one SimpleAgent material for all five demographics. Pushed unconditionally
			// alongside the high-spec set; the subsystem decides which one reaches the live component
			// based on the current spec level, and stores both either way.
			RepresentationSubsystem->SetLowSpecPedestrianMaterial(CurrentSelectedLowSpecMaterialInstance);
		}
}
