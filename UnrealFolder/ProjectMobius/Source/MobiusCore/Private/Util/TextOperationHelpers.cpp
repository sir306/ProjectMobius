// Fill out your copyright notice in the Description page of Project Settings.


#include "Util/TextOperationHelpers.h"


TextOperationHelpers::TextOperationHelpers()
{
}

TextOperationHelpers::~TextOperationHelpers()
{
}

FText TextOperationHelpers::CleanInputTextToPositiveIntText(const FText& InputText)
{
	FString Str = InputText.ToString();
	FString DigitsOnly;

	// Keep only digits
	for (TCHAR Ch : Str)
	{
		if (FChar::IsDigit(Ch))
		{
			DigitsOnly.AppendChar(Ch);
		}
	}

	int32 Value = 0;
	if (DigitsOnly.Len() > 0)
	{
		Value = FCString::Atoi(*DigitsOnly);
	}

	// Ensure it is at least 1
	Value = FMath::Max(1, Value);

	return FText::AsNumber(Value);
}
