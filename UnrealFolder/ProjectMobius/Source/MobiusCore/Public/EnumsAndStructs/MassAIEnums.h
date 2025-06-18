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
 * Enum to define the different age demographics
 * ie Child, Adult, Elderly
 */
UENUM()
enum class EAgeDemographic : uint8
{
	/** Child */
	Ead_Child = 0,

	/** Adult */
	Ead_Adult = 1,

	/** Elderly */
	Ead_Elderly = 2,

	/** Default */
	Ead_Default = 3
};

/**
 * Enum for pedestrian genders
 */
UENUM()
enum class EPedestrianGender : uint8
{
	Epg_Male = 0,
	Epg_Female = 1,
	Epg_Default = 2
};

UENUM()
enum class EPedestrianMovementBracket : uint8
{
	Emb_NotMoving = 0,
	Emb_Shuffle = 1,
	Emb_SlowWalk = 2,
	Emb_Walk = 3,
	Emb_BriskWalk = 4,
	Emb_Error = 5
};