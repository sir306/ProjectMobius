// Fill out your copyright notice in the Description page of Project Settings.


#include "Interfaces/TextHelperInterface.h"

#include "Util/TextOperationHelpers.h"


// Add default functionality here for any ITextHelperInterface functions that are not pure virtual.
FText ITextHelperInterface::CleanInputTextToPositiveIntText(const FText& InputText)
{
	return TextOperationHelpers::CleanInputTextToPositiveIntText(InputText);
}
