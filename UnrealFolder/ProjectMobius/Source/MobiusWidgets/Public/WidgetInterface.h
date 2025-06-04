// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "WidgetInterface.generated.h"

class UTextBlock;
// This class does not need to be modified.
UINTERFACE()
class UWidgetInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class MOBIUSWIDGETS_API IWidgetInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	/**
	 * Setup a widget with slate style sheet for a text block
	 * @note This will require refactoring when we swap out the text block for a rich text block
	 *
	 * @param[UTextBlock] TextBlockToStyle The text block to setup
	 * @param[FTextBlockStyle] TextBlockStyle The text block style to apply
	 */
	static void SetupTextBlockStyle(UTextBlock* TextBlockToStyle, const FTextBlockStyle& TextBlockStyle);
};
