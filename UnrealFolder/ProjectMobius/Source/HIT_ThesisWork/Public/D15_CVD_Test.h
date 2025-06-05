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
#include "Blueprint/UserWidget.h"
#include "D15_CVD_Test.generated.h"

/**
 * Structure to store the Lab color space values and handle conversions from RGB to Lab colour space
 */
USTRUCT(BlueprintType)
struct FLabColor
{
	GENERATED_BODY()

	FLabColor()
	{
		L = 0.0f;
		A = 0.0f;
		B = 0.0f;
	}
	FLabColor(float InL, float InA, float InB)
		: L(InL), A(InA), B(InB) {}

	float L;
	float A;
	float B;

	// Conversion constructor from FLinearColor
	FLabColor(const FLinearColor& RGBColor)
	{
		ConvertRGBToLab(RGBColor);
	}

	void ConvertRGBToLab(const FLinearColor& RGBColor)
	{
		// RGB to XYZ conversion (assuming sRGB)
		float Rrgb = RGBColor.R <= 0.04045f ? RGBColor.R / 12.92f : FMath::Pow((RGBColor.R + 0.055f) / 1.055f, 2.4f);
		float Grgb = RGBColor.G <= 0.04045f ? RGBColor.G / 12.92f : FMath::Pow((RGBColor.G + 0.055f) / 1.055f, 2.4f);
		float Brgb = RGBColor.B <= 0.04045f ? RGBColor.B / 12.92f : FMath::Pow((RGBColor.B + 0.055f) / 1.055f, 2.4f);

		float X = Rrgb * 0.4124f + Grgb * 0.3576f + Brgb * 0.1805f;
		float Y = Rrgb * 0.2126f + Grgb * 0.7152f + Brgb * 0.0722f;
		float Z = Rrgb * 0.0193f + Grgb * 0.1192f + Brgb * 0.9505f;

		// XYZ to Lab conversion (D65 white point)
		const float Xn = 0.95047f;
		const float Yn = 1.0f;
		const float Zn = 1.08883f;

		float fx = X / Xn > 0.008856f ? FMath::Pow(X / Xn, 1.0f / 3.0f) : (7.787f * (X / Xn) + 16.0f / 116.0f);
		float fy = Y / Yn > 0.008856f ? FMath::Pow(Y / Yn, 1.0f / 3.0f) : (7.787f * (Y / Yn) + 16.0f / 116.0f);
		float fz = Z / Zn > 0.008856f ? FMath::Pow(Z / Zn, 1.0f / 3.0f) : (7.787f * (Z / Zn) + 16.0f / 116.0f);

		L = 116.0f * fy - 16.0f;
		A = 500.0f * (fx - fy);
		B = 200.0f * (fy - fz);
	}
};

/**
 * Structure to store the color data for the D-15 test
 */
USTRUCT(BlueprintType)
struct FD15ColorData
{
	GENERATED_BODY()

	FD15ColorData()
	{
		ID = 0;
		RGBColor = FLinearColor::White;
		LabColor = FLabColor();
	}

	FD15ColorData(const int32 InID, const FLinearColor InRGBColor, FLinearColor InLabColor)
		: ID(InID), RGBColor(InRGBColor), LabColor(InLabColor) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor RGBColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLabColor LabColor;

	static FLinearColor ConvertRGBToLinear(FLinearColor InColour)
	{
		return FLinearColor(InColour.R/255.0f, InColour.G/255.0f, InColour.B/255.0f, 1.0f);
	}

};

enum class EColorVisionDeficiencyType
{
	Normal,
	Deuteranomaly,  // Green cone weakness (most common)
	Protanomaly,    // Red cone weakness
	Tritanomaly,    // Blue cone weakness (rare)
	Unknown
};

/**
 * 
 */
UCLASS()
class HIT_THESISWORK_API UD15_CVD_Test : public UUserWidget
{
	GENERATED_BODY()

public:
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Test")
	TArray<FD15ColorData> D15Colors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Test")
	TArray<FD15ColorData> UserD15Colors;
	

	UFUNCTION(BlueprintCallable, Category = "Color Test")
	static float CalculateColorDifference(const FLabColor& Color1, const FLabColor& Color2)
	{
		// CIE76 Delta E calculation
		float DeltaL = Color1.L - Color2.L;
		float DeltaA = Color1.A - Color2.A;
		float DeltaB = Color1.B - Color2.B;

		return FMath::Sqrt(
			FMath::Square(DeltaL) + 
			FMath::Square(DeltaA) + 
			FMath::Square(DeltaB)
		);
	}

	UFUNCTION(BlueprintCallable, Category = "Color Test")
	float ScoreColorArrangement(const TArray<FD15ColorData>& UserArrangement)
	{
		if (UserArrangement.Num() != D15Colors.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("Incorrect number of colors in arrangement"));
			return -1.0f;
		}

		float TotalError = 0.0f;

		for (int32 i = 0; i < D15Colors.Num(); ++i)
		{
			FLabColor IdealColor(D15Colors[i].RGBColor);
			FLabColor UserColor(UserArrangement[i].RGBColor);

			TotalError += CalculateColorDifference(IdealColor, UserColor);
		}

		return TotalError;
	}

	UFUNCTION(BlueprintCallable, Category = "Color Test")
	void InitializeD15Colors();

	static EColorVisionDeficiencyType AnalyzeD15ColorData(const TArray<FD15ColorData>& ColorSamples);

	// Threshold for determining color vision deficiency
	static constexpr float kThreshold = 5.0f;

	// Calculate deviation from expected color progression for deuteranomaly
	static float CalculateDeuteranomalyScore(const TArray<FD15ColorData>& ColorSamples)
	{
		float totalDeviation = 0.0f;

		if (ColorSamples.Num() == 0)
		{
			return totalDeviation;
		}
        
		// Analyze color spacing and progression in the a* and b* channels
		for (int32 i = 1; i < ColorSamples.Num(); ++i)
		{
			// Calculate color differences in Lab color space
			float aDeviation = FMath::Abs(ColorSamples[i].LabColor.A - ColorSamples[i-1].LabColor.A);
			float bDeviation = FMath::Abs(ColorSamples[i].LabColor.B - ColorSamples[i-1].LabColor.B);
            
			// Specific weighting for green cone deficiency
			totalDeviation += aDeviation * 0.7f + bDeviation * 0.3f;
		}
		
		return totalDeviation / ColorSamples.Num();
	}

	// Similar methods for Protanomaly and Tritanomaly
	static float CalculateProtanomalyScore(const TArray<FD15ColorData>& ColorSamples)
	{
		float totalDeviation = 0.0f;
		if (ColorSamples.Num() == 0)
		{
			return totalDeviation;
		}
		for (int32 i = 1; i < ColorSamples.Num(); ++i)
		{
			float aDeviation = FMath::Abs(ColorSamples[i].LabColor.A - ColorSamples[i-1].LabColor.A);
			float bDeviation = FMath::Abs(ColorSamples[i].LabColor.B - ColorSamples[i-1].LabColor.B);
            
			// Weighting for red cone deficiency
			totalDeviation += aDeviation * 0.5f + bDeviation * 0.5f;
		}

		return totalDeviation / ColorSamples.Num();
	}

	static float CalculateTritanomalyScore(const TArray<FD15ColorData>& ColorSamples)
	{
		float totalDeviation = 0.0f;
		if (ColorSamples.Num() == 0)
		{
			return totalDeviation;
		}
		for (int32 i = 1; i < ColorSamples.Num(); ++i)
		{
			float bDeviation = FMath::Abs(ColorSamples[i].LabColor.B - ColorSamples[i-1].LabColor.B);
            
			// Focus on blue cone variations
			totalDeviation += bDeviation;
		}

		return totalDeviation / ColorSamples.Num();
	}


	UFUNCTION(BlueprintCallable)
	void CalculateD15Result_Claude()
	{
		// Populate D15ColorSamples with your color data

		EColorVisionDeficiencyType DeficiencyType = AnalyzeD15ColorData(UserD15Colors);

		switch (DeficiencyType)
		{
		case EColorVisionDeficiencyType::Deuteranomaly:
			UE_LOG(LogTemp, Warning, TEXT("Deuteranomaly Detected"));
			break;
		case EColorVisionDeficiencyType::Protanomaly:
			UE_LOG(LogTemp, Warning, TEXT("Protanomaly Detected"));
			break;
		case EColorVisionDeficiencyType::Tritanomaly:
			UE_LOG(LogTemp, Warning, TEXT("Tritanomaly Detected"));
			break;
		default:
			UE_LOG(LogTemp, Warning, TEXT("Normal Color Vision"));
			break;
		}

		EColorVisionDeficiencyType DeficiencyType2 = DetectColorVisionDeficiency(UserD15Colors);

		switch (DeficiencyType2)
		{
		case EColorVisionDeficiencyType::Deuteranomaly:
			UE_LOG(LogTemp, Warning, TEXT("Deuteranomaly Detected2"));
			break;
		case EColorVisionDeficiencyType::Protanomaly:
			UE_LOG(LogTemp, Warning, TEXT("Protanomaly Detected2"));
			break;
		case EColorVisionDeficiencyType::Tritanomaly:
			UE_LOG(LogTemp, Warning, TEXT("Tritanomaly Detected2"));
			break;
		default:
			UE_LOG(LogTemp, Warning, TEXT("Normal Color Vision2"));
			break;
		}
	}


	static EColorVisionDeficiencyType DetectColorVisionDeficiency(const TArray<FD15ColorData>& ColorSamples);
	static float CalculateConeDeviation(const TArray<FD15ColorData>& ColorSamples, EColorVisionDeficiencyType ConeType);
	static float CalculateSpecificConeDeviation(const FLabColor& Color1, const FLabColor& Color2, EColorVisionDeficiencyType ConeType);


	
	// chatgpt
	UFUNCTION(BlueprintCallable)
	void CalculateD15CVDTest();

	UFUNCTION(BlueprintCallable)
	FString DetermineColourDeficiencyType(const TArray<FVector2D>& ConfusionLines);

	UFUNCTION(BlueprintCallable)
	TArray<FVector2D> ComputeConfusionLines(const TArray<FLinearColor>& UserTestResults);
	
	UFUNCTION(BlueprintCallable)
	FVector2D RGBToChromaticity(const FLinearColor& Color);

	UFUNCTION(BlueprintCallable)
	TArray<FVector2D> CalculateErrorVectors();

	UFUNCTION(BlueprintCallable)
	int32 CalculateErrorScore();
	
	/**
	 * Write Test Results to CSV file
	 *
	 * @param TestResults - Array of Test Results
	 *
	 * @return bool - True if the file was written successfully
	 */
	UFUNCTION(BlueprintCallable)
	bool WriteTestResultsToCSV(TArray<FString> TestResults);


	const TArray<int32> CorrectSequence = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<int32> UserSequence = TArray<int32>();;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FLinearColor> UserColors;
	const FVector2D ProtanAxis = FVector2d(1.0f, -0.5f);   // Approximate direction for protan confusion
	const FVector2D DeutanAxis = FVector2d(0.5f, -0.5f);  // Approximate direction for deutan confusion
	const FVector2D TritanAxis = FVector2d(-0.5f, -1.0f); // Approximate direction for tritan confusion
};
