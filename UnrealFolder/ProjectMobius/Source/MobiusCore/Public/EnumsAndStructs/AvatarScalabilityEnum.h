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

#pragma once

/**
 * Enum to define avatar scalability settings
 */
UENUM(BlueprintType)
enum EPedestrianScalabilitySettings : uint8
{
	EPss_Low = 0 UMETA(DisplayName = "Low"), // Low scalability settings
	EPss_Medium = 1 UMETA(DisplayName = "Medium"), // Medium scalability settings
	EPss_High = 2 UMETA(DisplayName = "High"), // High scalability settings
	EPss_Epic = 3 UMETA(DisplayName = "Epic"), // Epic scalability settings
	EPss_Cinematic = 4 UMETA(DisplayName = "Cinematic"), // Cinematic scalability settings
	EPss_Default = 5 UMETA(DisplayName = "Default", Hidden), // Default scalability settings
};