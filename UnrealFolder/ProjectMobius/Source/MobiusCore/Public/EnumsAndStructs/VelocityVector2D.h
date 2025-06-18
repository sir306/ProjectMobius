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

#include "CoreMinimal.h"

/**
 * Vector Velocity 2D sample, used to store angular and linear velocity for agents
 * Created by: Peter Thompson
 */
struct MOBIUSCORE_API FVelocityVector2D
{

public:
	// Constructor from angle and speed
	FVelocityVector2D(double a = 0.0, double s = 0.0) : Angle(a), Speed(s) {}

	// Constructor from a Vector3D and duration time
	FVelocityVector2D(const FVector& Vec, double TravelTime) {
		Angle = FMath::Atan2(Vec.Y, Vec.X);
		Speed = Vec.Length() / TravelTime;
	}
     
	// get/set methods for angle and speed
	double GetAngle() const { return Angle; }
	double GetSpeed() const { return Speed; }
	void SetAngle(double a) { Angle = a; }
	void SetSpeed(double s) { Speed = s; }

	// Static method to infer step duration for normalised speed
	double InferStepDuration(void)
	{
		// Min. step duration inference speed 0.26547 m/s is derived from 
		// Wang 2018: Table 4 https://doi.org/10.1016/j.physa.2018.02.021
		// Step length = 0.613 x pow(v, 0.631), equates to Step time = 0.613 x pow(v, -0.369)
		// Step duration trends above 1.0 second for v at or below 0.26547 m/s
		// At these speeds, the step length is less than 0.265m, which is ~ an average shoe length
		// and, so we know that feet are not lifting off the ground, so definitely shuffling
		// and therefore we cannot rely on regular step frequency. 
		// Alternatives are: 
		// 0.3m/s (Sl = 0.287m) as a generic threshold as a rounded figure
		// or 0.38062 m/s (Sl=0.333m) which is  the speed at which we transition from shuffle to slow walk
		// (https://doi.org/10.1016/j.physa.2022.126927)
		// We are not using the step frequency 4th order polynomial from the step extent/contact buffer paper
		// because that level of complexity just seems computationally excessive for this application

		if (Speed > 0.26547) {
			double duration = 0.613 * pow(Speed, -0.369);
			return duration;
		}
		else return 1.0;
	};

	// Static method to infer stride duration for normalised speed equalt to double step duration
	const double InferStrideDuration(void)
	{
		return 2.0 * InferStepDuration();
	};

private:
	double Angle;
	double Speed;
};