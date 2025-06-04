// Fill out your copyright notice in the Description page of Project Settings.


#include "WidgetInterface.h"
#include "Components/TextBlock.h"


// Add default functionality here for any IWidgetInterface functions that are not pure virtual.
void IWidgetInterface::SetupTextBlockStyle(UTextBlock* TextBlockToStyle, const FTextBlockStyle& TextBlockStyle)
{
	// check if the text block is valid
	if (TextBlockToStyle)
	{
		// set the text block style
		TextBlockToStyle->SetFont(TextBlockStyle.Font);
		TextBlockToStyle->SetColorAndOpacity(TextBlockStyle.ColorAndOpacity);
		TextBlockToStyle->SetShadowColorAndOpacity(TextBlockStyle.ShadowColorAndOpacity);
		TextBlockToStyle->SetShadowOffset(TextBlockStyle.ShadowOffset);
		TextBlockToStyle->SetTextTransformPolicy(TextBlockStyle.TransformPolicy);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("The text block is invalid"));
	}
}
