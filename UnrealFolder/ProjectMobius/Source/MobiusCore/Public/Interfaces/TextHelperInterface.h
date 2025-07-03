// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TextHelperInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(NotBlueprintable)
class UTextHelperInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class MOBIUSCORE_API ITextHelperInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	UFUNCTION(BlueprintCallable, Category = "Text Helper Interface")
	virtual FText CleanInputTextToPositiveIntText(const FText& InputText);
};
