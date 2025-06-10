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

#include "Interfaces/UnitConversionAndScaling.h"


// Add default functionality here for any IUnitConversionAndScaling functions that are not pure virtual.
void IUnitConversionAndScaling::ConvertFloatUnitValueToNewUnitType(float& InValue, EDistanceUnitType InUnitType,
	EDistanceUnitType OutUnitType)
{
	// Scale the input value by the conversion factor
	InValue *= GetConversionFactor(InUnitType, OutUnitType);
}

void IUnitConversionAndScaling::ConvertIntUnitValueToNewUnitType(int32& InValue, EDistanceUnitType InUnitType,
	EDistanceUnitType OutUnitType)
{
	// Scale the input value by the conversion factor
	InValue *= GetConversionFactor(InUnitType, OutUnitType);
}

float IUnitConversionAndScaling::GetConversionFactor(EDistanceUnitType InUnitType, EDistanceUnitType OutUnitType)
{
	// 2D array with all conversion factors between the different unit types
	static const float ConversionTable[Edut_MAX][Edut_MAX] = 
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
