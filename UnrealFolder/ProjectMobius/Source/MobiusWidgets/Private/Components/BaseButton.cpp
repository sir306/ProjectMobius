// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/BaseButton.h"

void UBaseButton::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	ApplyMobiusButtonStyle();
}

void UBaseButton::ApplyMobiusButtonStyle()
{
	// if the style asset and button is valid, apply the style to the button
	if (SlateButtonStyle && SlateButtonStyle->GetStyle<FButtonStyle>())
	{
		// Set the style of the button
		SetStyle(*SlateButtonStyle->GetStyle<FButtonStyle>());
	}
}
