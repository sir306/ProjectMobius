// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "BaseButton.generated.h"

/**
 * To apply our custom style to the button, we need to create a new class that inherits from UButton,
 * this is to reduce some boilerplate code that we would have to write if we were to create a new button for each widget.
 */
UCLASS()
class MOBIUSWIDGETS_API UBaseButton : public UButton
{
	GENERATED_BODY()

public:
	/**
	 * The SynchronizeProperties function is called when the widget is constructed,
	 * this is where we can apply our custom style to the button.
	 */
	virtual void SynchronizeProperties() override;

	/**
	 * Method to apply the custom style to the button.
	 */
	UFUNCTION(BlueprintCallable, Category = "MobiusWidget|Appearance")
	virtual void ApplyMobiusButtonStyle();

	/** The Style asset for the button */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MobiusWidget|Appearance")
	TObjectPtr<USlateWidgetStyleAsset> SlateButtonStyle;
};
