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

#include "Components/MaterialPicker.h"

#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "UI/Synth2DSlider.h"
#include "Materials/MaterialParameterCollection.h"
#include "ParameterCollection.h"
// include kismetmateriallibrary.h for the SetVectorParameterDefaultValue method
#include "Components/Slider.h"
#include "Kismet/KismetMaterialLibrary.h"

void UMaterialPicker::NativeConstruct()
{
	Super::NativeConstruct();

	// if the synth slider is valid bind the value changed delegate
	if(ColourPickerSynthSlider)
	{
		ColourPickerSynthSlider->OnValueChangedX.AddDynamic(this, &UMaterialPicker::OnColourSynthSliderXValueChanged);
		ColourPickerSynthSlider->OnValueChangedY.AddDynamic(this, &UMaterialPicker::OnColourSynthSliderYValueChanged);
	}
	// Bind the value slider value changed delegate
	if (ValueSlider)
	{
		ValueSlider->OnValueChanged.AddDynamic(this, &UMaterialPicker::OnValueSliderValueChanged);
	}
	// Bind the saturation slider value changed delegate
	if (SaturationSlider)
	{
		SaturationSlider->OnValueChanged.AddDynamic(this, &UMaterialPicker::OnSaturationSliderValueChanged);
	}
	
}

void UMaterialPicker::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

void UMaterialPicker::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	
	if(RInputHintTextBlock)
	{
		RInputHintTextBlock->SetText(FText::FromString("R:"));
	}
	if(GInputHintTextBlock)
	{
		GInputHintTextBlock->SetText(FText::FromString("G:"));
	}
	if(BInputHintTextBlock)
	{
		BInputHintTextBlock->SetText(FText::FromString("B:"));
	}
	if(AInputHintTextBlock)
	{
		AInputHintTextBlock->SetText(FText::FromString("A:"));
	}

	

	// Check if Input editable text boxes are valid
	if(RValueEditableTextBlock && GValueEditableTextBlock && BValueEditableTextBlock && AValueEditableTextBlock)
	{
		// Set the text committed delegates
		SetTextCommittedDelegates();
	}
		
}


void UMaterialPicker::GetAndAssignMaterialColourValue()
{
	// Check if the image is valid
	if (HeatmapColorImage_Current && HeatmapColorImage_New && MaterialParameterCollection)
	{
		// check material heatmap collection is valid
		if (MaterialParameterCollection.Get())
		{
			// Check the parameter name is valid and not empty
			if(ParameterName.IsValid() && !ParameterName.IsNone() && !ParameterName.IsEqual(NAME_None))
			{
				// Get the parameter value
				//CollectionParamIndex = MaterialParameterCollection->GetVectorParameterIndexByName(ParameterName);
				
				// Get the color value
				CurrentColorValue = UKismetMaterialLibrary::GetVectorParameterValue(this, MaterialParameterCollection.Get(), ParameterName);

				const FCollectionVectorParameter* FoundVecColParam = MaterialParameterCollection.Get()->GetVectorParameterByName(ParameterName);

				// Update the editable text boxes and slider values to match the new color value
				//InitalizeColoursAndUI(FLinearColor(FoundVecColParam->DefaultValue));
				InitalizeColoursAndUI(CurrentColorValue);
			}
		}
	}
}

void UMaterialPicker::OnRValueTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if(!GetWorld())
	{
		return;
	}
	
	// Convert the text to a string
	FString TextStr = Text.ToString();
	
	// Check if the text is numeric
	if(CheckTextCommittedIsNumeric(TextStr))
	{
		// Convert the string to a float
		float Value = FMath::Clamp(FCString::Atof(*TextStr), 0.0f, 1.0f);
		
		// Update the color value
		NewColorValue.R = Value;
	}
	
	UpdateSlidersAndColourValues(NewColorValue);
}

void UMaterialPicker::OnGValueTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if(!GetWorld())
	{
		return;
	}
	
	// Convert the text to a string
	FString TextStr = Text.ToString();
	
	// Check if the text is numeric
	if(CheckTextCommittedIsNumeric(TextStr))
	{
		// Convert the string to a float
		float Value = FMath::Clamp(FCString::Atof(*TextStr), 0.0f, 1.0f);
		
		// Update the color value
		NewColorValue.G = Value;
	}
	
	UpdateSlidersAndColourValues(NewColorValue);
}

void UMaterialPicker::OnBValueTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if(!GetWorld())
	{
		return;
	}
	
	// Convert the text to a string
	FString TextStr = Text.ToString();
	
	// Check if the text is numeric
	if(CheckTextCommittedIsNumeric(TextStr))
	{
		// Convert the string to a float
		float Value = FMath::Clamp(FCString::Atof(*TextStr), 0.0f, 1.0f);
		
		// Update the color value
		NewColorValue.B = Value;
	}
	
	UpdateSlidersAndColourValues(NewColorValue);
}

void UMaterialPicker::OnAValueTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if(!GetWorld())
	{
		return;
	}

	// Convert the text to a string
	FString TextStr = Text.ToString();
	
	// Check if the text is numeric
	if(CheckTextCommittedIsNumeric(TextStr))
	{
		// Convert the string to a float
		float Value = FMath::Clamp(FCString::Atof(*TextStr), 0.0f, 1.0f);
		
		// Update the color value
		NewColorValue.A = Value;
	}
	
	UpdateSlidersAndColourValues(NewColorValue);
}

bool UMaterialPicker::CheckTextCommittedIsNumeric(const FString& Text)
{
	return Text.IsNumeric();
}

void UMaterialPicker::SetTextCommittedDelegates()
{
	// check if the editable text blocks have already been assigned delegates
	if(!RValueEditableTextBlock->OnTextCommitted.IsBound())
	{
		RValueEditableTextBlock->OnTextCommitted.AddDynamic(this, &UMaterialPicker::OnRValueTextCommitted);
	}
	if(!GValueEditableTextBlock->OnTextCommitted.IsBound())
	{
		GValueEditableTextBlock->OnTextCommitted.AddDynamic(this, &UMaterialPicker::OnGValueTextCommitted);
	}
	if(!BValueEditableTextBlock->OnTextCommitted.IsBound())
	{
		BValueEditableTextBlock->OnTextCommitted.AddDynamic(this, &UMaterialPicker::OnBValueTextCommitted);
	}
	if(!AValueEditableTextBlock->OnTextCommitted.IsBound())
	{
		AValueEditableTextBlock->OnTextCommitted.AddDynamic(this, &UMaterialPicker::OnAValueTextCommitted);
	}
}

void UMaterialPicker::SetParameterNameAndTitleText(FName NewParameterName, FText NewTitleText)
{
	// Check the texts are valid
	if (TitleTextBlock && NewTitleText.IsEmpty() == false)
	{
					
		// Set Title to the Parameter Name
		TitleTextBlock->SetText(NewTitleText);
	}
	
	ParameterName = NewParameterName;

	GetAndAssignMaterialColourValue();
}


void UMaterialPicker::UpdateSlidersAndColourValues(FLinearColor NewColour)
{
	// Update the colour value
	NewColorValue = FLinearColor(NewColour);
	
	// Check if the image is valid
	if (HeatmapColorImage_New)
	{
		// Set the color value to the image
		HeatmapColorImage_New->SetColorAndOpacity(NewColorValue);

		// update the slider values
		if(ColourPickerSynthSlider && SaturationSlider && ValueSlider)
		
		{
			// convert the color to HSV
			FLinearColor HSV(NewColorValue.LinearRGBToHSV());

			// Update Synth Slider
			float NewSynthSliderX = HSV.R / 360.0f;
			FVector2d NewSynthSliderValue = FVector2d(NewSynthSliderX, 0.0f);
	
			ColourPickerSynthSlider->SetValue(NewSynthSliderValue);

			// Update Saturation Slider
			float NewSaturationSliderValue = HSV.G;
			SaturationSlider->SetValue(NewSaturationSliderValue);

			// Update Value Slider
			float NewValueSliderValue = HSV.B;
			ValueSlider->SetValue(NewValueSliderValue);
			
			HSV.G = 1.0f;
			if (SaturationSliderMID)
			{
				SaturationSliderMID->SetVectorParameterValue("TopColour", FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
				SaturationSliderMID->SetVectorParameterValue("BottomColour", HSV.HSVToLinearRGB());
			}
			
		}
	}
}

void UMaterialPicker::InitalizeColoursAndUI(FLinearColor NewColour)
{
	// Update the colour values
	CurrentColorValue = FLinearColor(NewColour);
	NewColorValue = FLinearColor(NewColour);
	
	// Check if the image is valid
	if (HeatmapColorImage_New && HeatmapColorImage_Current)
	{
		// Set the color values to the image
		HeatmapColorImage_Current->SetColorAndOpacity(CurrentColorValue);
		HeatmapColorImage_New->SetColorAndOpacity(NewColorValue);
	}

	// update the slider values
	UpdateSlidersAndColourValues(NewColour);
		
	// Update the editable text boxes to match the new color value
	UpdateEditableTextColourVals();
}

void UMaterialPicker::CreateMaterialInstanceDynamics(UMaterialInterface* SynthSliderMaterial,
                                                     UMaterialInterface* SaturationValueSliderMaterial)
{
	// Check if the materials are valid
	if(SynthSliderMaterial && SaturationValueSliderMaterial)
	{
		// Create the MID for the Synth Slider
		SynthSliderMID = UMaterialInstanceDynamic::Create(SynthSliderMaterial, this);
		// Set the MID to the Synth Slider
		if(ColourPickerSynthSlider)
		{
			FSynth2DSliderStyle ColourPickerSliderStyle = ColourPickerSynthSlider->WidgetStyle;
			FSlateBrush ColourPickerSliderMIDBrush = FSlateBrush();
			ColourPickerSliderMIDBrush.SetResourceObject(SynthSliderMID);
			ColourPickerSliderStyle.BackgroundImage = ColourPickerSliderMIDBrush;
			
			// Set the MID to the Value Slider
			ColourPickerSynthSlider->WidgetStyle = ColourPickerSliderStyle;
		}

		// Create the MID for the Saturation Slider
		SaturationSliderMID = UMaterialInstanceDynamic::Create(SaturationValueSliderMaterial, this);

		// Set the scalar parameter value for the material to indicate the slider type
		SaturationSliderMID->SetScalarParameterValue("ValueSlider?", 0.0f);
		
		// Create Style for the Saturation Slider
		if(SaturationSlider)
		{
			FSliderStyle SaturationSliderStyle = SaturationSlider->GetWidgetStyle();
			FSlateBrush SaturationSliderMIDBrush = FSlateBrush();
			SaturationSliderMIDBrush.SetResourceObject(SaturationSliderMID);
			SaturationSliderStyle.SetNormalBarImage(SaturationSliderMIDBrush);
			SaturationSliderStyle.SetHoveredBarImage(SaturationSliderMIDBrush);
			
			// Set the MID to the Value Slider
			SaturationSlider->SetWidgetStyle(SaturationSliderStyle);
		}

		// Create the MID for the Value Slider
		ValueSliderMID = UMaterialInstanceDynamic::Create(SaturationValueSliderMaterial, this);

		// set the scalar parameter value for the material to indicate the slider type
		ValueSliderMID->SetScalarParameterValue("ValueSlider?", 1.0f);

		// Create Style for the Value Slider
		if(ValueSlider)
		{
			FSliderStyle ValueSliderStyle = ValueSlider->GetWidgetStyle();
			FSlateBrush ValueSliderMIDBrush = FSlateBrush();
			ValueSliderMIDBrush.SetResourceObject(ValueSliderMID);
			ValueSliderStyle.SetNormalBarImage(ValueSliderMIDBrush);
			ValueSliderStyle.SetHoveredBarImage(ValueSliderMIDBrush);
			
			// Set the MID to the Value Slider
			ValueSlider->SetWidgetStyle(ValueSliderStyle);
		}

	}

	// Update the widgets to match the colour value
	InitalizeColoursAndUI(CurrentColorValue);
}

void UMaterialPicker::UpdateEditableTextColourVals() const
{
	// Update the editable text boxes to match the new color value
	if(RValueEditableTextBlock)
	{
		RValueEditableTextBlock->SetText(FText::FromString(FString::SanitizeFloat(NewColorValue.R)));
	}
	if (GValueEditableTextBlock)
	{
		GValueEditableTextBlock->SetText(FText::FromString(FString::SanitizeFloat(NewColorValue.G)));
	}
	if (BValueEditableTextBlock)
	{
		BValueEditableTextBlock->SetText(FText::FromString(FString::SanitizeFloat(NewColorValue.B)));
	}
	if (AValueEditableTextBlock)
	{
		AValueEditableTextBlock->SetText(FText::FromString(FString::SanitizeFloat(NewColorValue.A)));
	}
}

void UMaterialPicker::UpdateColoursFromSliders(bool bUpdateSatAndValSliders)
{

	// Create a new colour to represent the HSV values
	FLinearColor HSV(NewColorValue);
	
	// Update the color value
	HSV.R = ColourPickerSynthSlider->GetValue().X * 360.0f;// Only need X value
	HSV.G = SaturationSlider->GetValue();
	HSV.B = ValueSlider->GetValue();
	HSV.A = NewColorValue.A;

	NewColorValue = FLinearColor(HSV.HSVToLinearRGB());// HERE is problem likely due to the sliders being off on values before this point

	// Set the color value to the image
	HeatmapColorImage_New->SetColorAndOpacity(NewColorValue);

	// Update the MID colours for the value and saturation sliders
	if(SaturationSliderMID && ValueSliderMID && bUpdateSatAndValSliders)
	{
		// HSV value should always be 1.0f for the top colour on the value slider
		HSV.B = 1.0f;
		ValueSliderMID->SetVectorParameterValue("TopColour", HSV.HSVToLinearRGB());

		HSV.G = 1.0f;
		SaturationSliderMID->SetVectorParameterValue("BottomColour", HSV.HSVToLinearRGB());
	}
}

void UMaterialPicker::UpdateSlidersColoursAndText(bool bUpdateSatAndValSliders)
{
	UpdateColoursFromSliders(bUpdateSatAndValSliders);

	UpdateEditableTextColourVals();
}

void UMaterialPicker::OnColourSynthSliderXValueChanged(float NewValue)
{
	if (NewValue == PreviousSynthSliderValue.X)
	{
		return;
	}
	UpdateSlidersColoursAndText();
}

void UMaterialPicker::OnColourSynthSliderYValueChanged(float NewValue)
{
	if (NewValue == PreviousSynthSliderValue.Y)
	{
		return;
	}
	
	// Update the value of value slider to match the Y value of the synth slider
	ValueSlider->SetValue((1.0f - ColourPickerSynthSlider->GetValue().Y));// The value slider goes in the opposite direction to the synth slider
	
	UpdateSlidersColoursAndText();
}

void UMaterialPicker::OnSaturationSliderValueChanged(float NewValue)
{
	// Update the colours, sliders and text
	UpdateSlidersColoursAndText();
	// // Update the MID colours for the value and saturation sliders
	// if(ValueSliderMID)
	// {
	// 	ValueSliderMID->SetVectorParameterValue("TopColour", ColorValue);
	// }
}

void UMaterialPicker::OnValueSliderValueChanged(float NewValue)
{
	UpdateSlidersColoursAndText();
	ColourPickerSynthSlider->SetValue(FVector2D(ColourPickerSynthSlider->GetValue().X, 1.0f - NewValue));
}

void UMaterialPicker::ConfirmNewColorValue()
{
	// Set the new current colour to the new colour value
	CurrentColorValue = NewColorValue;

	// update the images to match the new color value
	HeatmapColorImage_Current->SetColorAndOpacity(CurrentColorValue);

	// Set the value to the material parameter collection
	if (MaterialParameterCollection)
	{
		// Set the value to the material parameter collection
		UKismetMaterialLibrary::SetVectorParameterValue(this, MaterialParameterCollection.Get(), ParameterName, CurrentColorValue);
	}

	OnColourValueChanged.Broadcast();
}

void UMaterialPicker::ResetNewColorValue()
{
	InitalizeColoursAndUI(CurrentColorValue);
}
