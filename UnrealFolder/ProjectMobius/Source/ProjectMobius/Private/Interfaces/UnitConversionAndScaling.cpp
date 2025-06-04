// Fill out your copyright notice in the Description page of Project Settings.


#include "Interfaces/UnitConversionAndScaling.h"


// Add default functionality here for any IUnitConversionAndScaling functions that are not pure virtual.
void IUnitConversionAndScaling::ConvertFloatUnitValueToNewUnitType(float& InValue, EUnitType InUnitType,
	EUnitType OutUnitType)
{
	// Scale the input value by the conversion factor
	InValue *= GetConversionFactor(InUnitType, OutUnitType);
}

void IUnitConversionAndScaling::ConvertIntUnitValueToNewUnitType(int32& InValue, EUnitType InUnitType,
	EUnitType OutUnitType)
{
	// Scale the input value by the conversion factor
	InValue *= GetConversionFactor(InUnitType, OutUnitType);
}

float IUnitConversionAndScaling::GetConversionFactor(EUnitType InUnitType, EUnitType OutUnitType)
{
	// 2D array with all conversion factors between the different unit types
	static const float ConversionTable[Eut_MAX][Eut_MAX] = 
	{
		// Eut_Millimeters
		{1.0f, 0.1f, 0.001f, 0.000001f, 0.0393701f, 0.00328084f, 0.00109361f}, 
		// Eut_Centimeters
		{10.0f, 1.0f, 0.01f, 0.00001f, 0.393701f, 0.0328084f, 0.0109361f}, 
		// Eut_Meters
		{1000.0f, 100.0f, 1.0f, 0.001f, 39.3701f, 3.28084f, 1.09361f}, 
		// Eut_Kilometers
		{1000000.0f, 100000.0f, 1000.0f, 1.0f, 39370.1f, 3280.84f, 1093.61f}, 
		// Eut_Inches
		{25.4f, 2.54f, 0.0254f, 0.0000254f, 1.0f, 0.0833333f, 0.0277778f}, 
		// Eut_Feet
		{304.8f, 30.48f, 0.3048f, 0.0003048f, 12.0f, 1.0f, 0.333333f}, 
		// Eut_Yards
		{914.4f, 91.44f, 0.9144f, 0.0009144f, 36.0f, 3.0f, 1.0f}
	};

	// Get the conversion factor from the table
	return ConversionTable[InUnitType][OutUnitType];
}
