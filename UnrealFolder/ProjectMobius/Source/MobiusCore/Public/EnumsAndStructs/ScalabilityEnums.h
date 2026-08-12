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
 * Enum to define scalability settings
 */
UENUM(BlueprintType)
enum EScalabilitySettings : uint8
{
	ESsl_Low = 0 UMETA(DisplayName = "Low"), // Low scalability settings
	ESsl_Medium = 1 UMETA(DisplayName = "Medium"), // Medium scalability settings
	ESsl_High = 2 UMETA(DisplayName = "High"), // High scalability settings
	ESsl_Epic = 3 UMETA(DisplayName = "Epic"), // Epic scalability settings
	ESsl_Cinematic = 4 UMETA(DisplayName = "Cinematic"), // Cinematic scalability settings
	ESsl_Default = 5 UMETA(DisplayName = "Default", Hidden), // Default scalability settings
};

/**
 * Enum to define global scalability settings, which are used to set the overall quality of the viewer, this is to
 * simplify the scalability settings for the user, as they can be overwhelming with too many options.
 */
UENUM(BlueprintType)
enum EGlobalScalabilitySettings : uint8
{
	EGss_Low = 0 UMETA(DisplayName = "Low"), // Low scalability settings
	EGss_Medium = 1 UMETA(DisplayName = "Medium"), // Medium scalability settings
	EGss_High = 2 UMETA(DisplayName = "High"), // High scalability settings
	EGss_Epic = 3 UMETA(DisplayName = "Epic"), // Epic scalability settings
	EGss_Custom = 4 UMETA(DisplayName = "Custom"), // Custom scalability settings
	EGss_Default = 5 UMETA(DisplayName = "Default", Hidden), // Default scalability settings
};

/**
 * Render performance tier for the auto-detect / override system (tasks C1 / C2).
 *
 * Deliberately distinct from EGlobalScalabilitySettings (which maps to the UGameUserSettings sg.*
 * quality buckets): this tier owns a focused set of heavy render-feature cvars — ray tracing,
 * hardware Lumen, anti-aliasing — that are toggled at runtime by UPerformanceUtilSubsystem.
 * 'Auto' selects a tier from the detected GPU/RAM at startup; Low/Medium/High pin a tier for
 * reproducible captures across machines.
 */
UENUM(BlueprintType)
enum ERenderPerformanceTier : uint8
{
	ERpt_Auto = 0 UMETA(DisplayName = "Auto"),     // Detect tier from hardware (GPU/VRAM/RAM) at startup
	ERpt_Low = 1 UMETA(DisplayName = "Low"),       // Aggressive: HW Lumen + RT shadows off, AA -> TAA
	ERpt_Medium = 2 UMETA(DisplayName = "Medium"), // Balanced: HW Lumen off (software Lumen), RT shadows off
	ERpt_High = 3 UMETA(DisplayName = "High"),     // Full quality == DefaultEngine.ini values (capable HW)
};

/**
 * Enum to define scalability categories for various settings.
 */
UENUM(BlueprintType)
enum EScalabilityCategories : uint8
{
	ESc_Resolution = 0 UMETA(DisplayName = "Resolution"),
	ESc_GlobalIllumination = 1 UMETA(DisplayName = "Global Illumination"),
	ESc_PostProcessing = 2 UMETA(DisplayName = "Post Processing"),
	ESc_Shadows = 3 UMETA(DisplayName = "Shadows"),
	ESc_Textures = 4 UMETA(DisplayName = "Textures"),
	ESc_Effects = 5 UMETA(DisplayName = "Effects"),
	ESc_AntiAliasing = 6 UMETA(DisplayName = "Anti-Aliasing"),
	ESc_ViewDistance = 7 UMETA(DisplayName = "View Distance"),
	ESc_Reflections = 8 UMETA(DisplayName = "Reflections"),
	ESc_Shading = 9 UMETA(DisplayName = "Shading"),
	ESc_Global = 10 UMETA(DisplayName = "Global"),
	DefaultMax = 11 UMETA(DisplayName = "Default", Hidden),
};
