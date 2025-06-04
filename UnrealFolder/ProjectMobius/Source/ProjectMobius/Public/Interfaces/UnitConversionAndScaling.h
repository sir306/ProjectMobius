// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UnitConversionAndScaling.generated.h"

/*
 * Enum for the different types of units that can be converted
 */
UENUM()
enum EUnitType
{
	// Metric Units
	Eut_Millimeters		UMETA(DisplayName = "Millimeters"),
	Eut_Centimeters		UMETA(DisplayName = "Centimeters"),
	Eut_Meters			UMETA(DisplayName = "Meters"),
	Eut_Kilometers		UMETA(DisplayName = "Kilometers"),

	// Imperial Units
	Eut_Inches			UMETA(DisplayName = "Inches"),
	Eut_Feet			UMETA(DisplayName = "Feet"),
	Eut_Yards			UMETA(DisplayName = "Yards"),

	Eut_MAX				UMETA(DisplayName = "MAX")
	
};

// This class does not need to be modified.
UINTERFACE(Blueprintable)
class UUnitConversionAndScaling : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class PROJECTMOBIUS_API IUnitConversionAndScaling
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	/*
	 * Convert the float input value from the input unit type to the output unit type
	 *
	 * @param InValue - The value to convert
	 * @param InUnitType - The unit type of the input value
	 * @param OutUnitType - The unit type to convert the input value to
	 */
	void ConvertFloatUnitValueToNewUnitType(float& InValue, EUnitType InUnitType, EUnitType OutUnitType);

	/*
	 * Convert the int32 input value from the input unit type to the output unit type
	 *
	 * @param InValue - The value to convert
	 * @param InUnitType - The unit type of the input value
	 * @param OutUnitType - The unit type to convert the input value to
	 */
	void ConvertIntUnitValueToNewUnitType(int32& InValue, EUnitType InUnitType, EUnitType OutUnitType);

	/*
	 * Get the conversion factor from the input unit type to the output unit type
	 *
	 * @param InUnitType - The unit type of the input value
	 * @param OutUnitType - The unit type to convert the input value to
	 *
	 * @return float - The conversion factor
	 */
	float GetConversionFactor(EUnitType InUnitType, EUnitType OutUnitType);
};
