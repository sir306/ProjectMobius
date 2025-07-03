// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * 
 */
class MOBIUSCORE_API TextOperationHelpers
{
public:
	TextOperationHelpers();
	~TextOperationHelpers();

	/** Clean input FText to only be a positive numeric value */
	static FText CleanInputTextToPositiveIntText(const FText& InputText);
	
};
