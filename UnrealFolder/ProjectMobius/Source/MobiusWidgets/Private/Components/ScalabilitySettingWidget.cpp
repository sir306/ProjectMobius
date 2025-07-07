// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/ScalabilitySettingWidget.h"
#include "Components/ButtonWithText.h"

void UScalabilitySettingWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}

void UScalabilitySettingWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
}

void UScalabilitySettingWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ConfigureButtonStyles();

}

void UScalabilitySettingWidget::InitializeScalabilityLevel()
{
	Super::InitializeScalabilityLevel();
}

void UScalabilitySettingWidget::UpdateScalabilityLevel()
{
	Super::UpdateScalabilityLevel();
}


void UScalabilitySettingWidget::ApplyButtonStyleForActiveSetting()
{
	// check style asset is valid
	if (ScalabilityButtonStyle == nullptr)
	{
		return;
	}
	// Lambda to check in button against style
	auto ApplyButtonStyle = [this](UButtonWithText* Button, bool bIsActive)
	{
		if (Button && ScalabilityButtonStyle)
		{
			if (bIsActive)
			{
				FButtonStyle NewButtonStyle = *ScalabilityButtonStyle->GetStyle<FButtonStyle>();
				NewButtonStyle.Normal = ScalabilityButtonStyle->GetStyle<FButtonStyle>()->Hovered;
				NewButtonStyle.Hovered = ScalabilityButtonStyle->GetStyle<FButtonStyle>()->Normal;
				Button->SetStyle(NewButtonStyle);
			}
			else
			{
				FButtonStyle NewButtonStyle = *ScalabilityButtonStyle->GetStyle<FButtonStyle>();
				NewButtonStyle.Normal = ScalabilityButtonStyle->GetStyle<FButtonStyle>()->Normal;
				NewButtonStyle.Hovered = ScalabilityButtonStyle->GetStyle<FButtonStyle>()->Hovered;
				Button->SetStyle(NewButtonStyle);
			}
		}
	};

	auto CheckButtonStyle = [this](UButtonWithText* Button, bool bIsActive)
	{
		if (Button && ScalabilityButtonStyle)
		{
			if (Button->GetStyle().Normal == ScalabilityButtonStyle->GetStyle<FButtonStyle>()->Hovered && bIsActive)
			{
				// Button is already set to the ScalabilityButtonStyle, no need to apply again
				return true;
			}
			else if (Button->GetStyle().Normal == ScalabilityButtonStyle->GetStyle<FButtonStyle>()->Normal && !bIsActive)
			{
				// Button is already set to the ScalabilityButtonStyle, no need to apply again
				return true;
			}
		}
		return false;
	};

	
	switch (GetScalabilityLevel())
	{
	case ESsl_Low:
		if (!CheckButtonStyle(LowSetting_Button, true))
		{
			ApplyButtonStyle(LowSetting_Button, true);
			ApplyButtonStyle(MedSetting_Button, false);
			ApplyButtonStyle(HighSetting_Button, false);
			ApplyButtonStyle(EpicSetting_Button, false);
			ApplyButtonStyle(CineSetting_Button, false);
		}
		break;
	case ESsl_Medium:
		if (!CheckButtonStyle(MedSetting_Button, true))
		{
			ApplyButtonStyle(LowSetting_Button, false);
			ApplyButtonStyle(MedSetting_Button, true);
			ApplyButtonStyle(HighSetting_Button, false);
			ApplyButtonStyle(EpicSetting_Button, false);
			ApplyButtonStyle(CineSetting_Button, false);
		}
		break;
	case ESsl_High:
		if (!CheckButtonStyle(HighSetting_Button, true))
		{
			ApplyButtonStyle(LowSetting_Button, false);
			ApplyButtonStyle(MedSetting_Button, false);
			ApplyButtonStyle(HighSetting_Button, true);
			ApplyButtonStyle(EpicSetting_Button, false);
			ApplyButtonStyle(CineSetting_Button, false);
		}
		break;
	case ESsl_Epic:
		if (!CheckButtonStyle(EpicSetting_Button, true))
		{
			ApplyButtonStyle(LowSetting_Button, false);
			ApplyButtonStyle(MedSetting_Button, false);
			ApplyButtonStyle(HighSetting_Button, false);
			ApplyButtonStyle(EpicSetting_Button, true);
			ApplyButtonStyle(CineSetting_Button, false);
		}
		break;
	case ESsl_Cinematic:
		if (!CheckButtonStyle(CineSetting_Button, true))
		{
			ApplyButtonStyle(LowSetting_Button, false);
			ApplyButtonStyle(MedSetting_Button, false);
			ApplyButtonStyle(HighSetting_Button, false);
			ApplyButtonStyle(EpicSetting_Button, false);
			ApplyButtonStyle(CineSetting_Button, true);
		}
		break;
	case ESsl_Default:
		// If default then no button is active
		// Apply the default style to all buttons
		ApplyButtonStyle(LowSetting_Button, false);
		ApplyButtonStyle(MedSetting_Button, false);
		ApplyButtonStyle(HighSetting_Button, false);
		ApplyButtonStyle(EpicSetting_Button, false);
		ApplyButtonStyle(CineSetting_Button, false);
		break;
	default: ;
	}
	
	
	// check style sheet against button to see if it is set or not
	
}

void UScalabilitySettingWidget::UpdateScalabilityAndButtonStyle(EScalabilitySettings NewSetting)
{
	// Update the Scalability Level
	SetScalabilityLevel(NewSetting);

	// Update the buttons style based on the new Scalability Level
	ApplyButtonStyleForActiveSetting();

	// Update the scalability level in the Performance Util Subsystem
	UpdateScalabilityLevel();
}

void UScalabilitySettingWidget::ConfigureButtonStyles()
{
	// Ensure the ScalabilityButtonStyle is valid and has a valid FButtonStyle
	if (ScalabilityButtonStyle != nullptr)
	{
		return;
	}
	// Apply the ScalabilityButtonStyle to all buttons
	if (LowSetting_Button)
	{
		LowSetting_Button->bShouldSwitchNormalWithHovered = false;
		// Set the button style to the ScalabilityButtonStyle
		LowSetting_Button->ButtonStyleDefault = ScalabilityButtonStyle;
		// TODO Text style
		// Apply Style
		LowSetting_Button->ApplyMobiusButtonStyle();
	}
	if (MedSetting_Button)
	{
		MedSetting_Button->bShouldSwitchNormalWithHovered = false;
		// Set the button style to the ScalabilityButtonStyle
		MedSetting_Button->ButtonStyleDefault = ScalabilityButtonStyle;
		// TODO Text style
		// Apply Style
		MedSetting_Button->ApplyMobiusButtonStyle();
	}
	if (HighSetting_Button)
	{
		HighSetting_Button->bShouldSwitchNormalWithHovered = false; 
		// Set the button style to the ScalabilityButtonStyle
		HighSetting_Button->ButtonStyleDefault = ScalabilityButtonStyle;
		// TODO Text style
		// Apply Style
		HighSetting_Button->ApplyMobiusButtonStyle();
	}
	if (EpicSetting_Button)
	{
		EpicSetting_Button->bShouldSwitchNormalWithHovered = false; 
		// Set the button style to the ScalabilityButtonStyle
		EpicSetting_Button->ButtonStyleDefault = ScalabilityButtonStyle;
		// TODO Text style
		// Apply Style
		EpicSetting_Button->ApplyMobiusButtonStyle();
	}
	if (CineSetting_Button)
	{
		CineSetting_Button->bShouldSwitchNormalWithHovered = false; 
		// Set the button style to the ScalabilityButtonStyle
		CineSetting_Button->ButtonStyleDefault = ScalabilityButtonStyle;
		// TODO Text style
		// Apply Style
		CineSetting_Button->ApplyMobiusButtonStyle();
	}
}
