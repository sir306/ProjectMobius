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

#include "Components/ButtonWithText.h"
#include "Components/TextBlock.h"
#include "Widgets/SWidget.h"
#include "Slate.h"
#include "Components/ButtonSlot.h"

void UButtonWithText::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	
	ApplyMobiusButtonStyle();

	// bind the button clicked event to the update style method
	//OnClicked.AddDynamic(this, &UButtonWithText::ButtonClickedUpdateStyle);
}

void UButtonWithText::ApplyMobiusButtonStyle()
{
	Super::ApplyMobiusButtonStyle();
	
}

TSharedRef<SWidget> UButtonWithText::RebuildWidget()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		MyButton = SNew(SButton)
			.Text(ButtonTextValue)
			.TextStyle(MobiusButtonTextStyle ? MobiusButtonTextStyle->GetStyle<FTextBlockStyle>() : &FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
			.ButtonStyle(ButtonStyleDefault ? ButtonStyleDefault->GetStyle<FButtonStyle>() : &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.TextShapingMethod(ETextShapingMethod::FullShaping)// Set to full shaping now so when text translations are added, the text will be shaped correctly
			// Defaults
			.OnClicked(BIND_UOBJECT_DELEGATE(FOnClicked, SlateHandleClicked))
			.OnPressed(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandlePressed))
			.OnReleased(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandleReleased))
			.OnHovered_UObject( this, &ThisClass::SlateHandleHovered )
			.OnUnhovered_UObject( this, &ThisClass::SlateHandleUnhovered )
			.ClickMethod(ClickMethod)
			.TouchMethod(TouchMethod)
			.PressMethod(PressMethod)
			.IsFocusable(IsFocusable)
			;
	
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
		if ( GetChildrenCount() > 0 )
		{
			Cast<UButtonSlot>(GetContentSlot())->BuildSlot(MyButton.ToSharedRef());
		}
	
	return MyButton.ToSharedRef();
}

void UButtonWithText::ButtonClickedUpdateStyle()
{
	if(bShouldSwitchNormalWithHovered)
	{
		if(GetStyle().Normal == ButtonStyleDefault->GetStyle<FButtonStyle>()->Normal)
		{
			FButtonStyle NewButtonStyle = *ButtonStyleDefault->GetStyle<FButtonStyle>();
			NewButtonStyle.Normal = ButtonStyleDefault->GetStyle<FButtonStyle>()->Hovered;
			SetStyle(NewButtonStyle);
		}
		else
		{
			SetStyle(*ButtonStyleDefault->GetStyle<FButtonStyle>());
		}
	}
}
