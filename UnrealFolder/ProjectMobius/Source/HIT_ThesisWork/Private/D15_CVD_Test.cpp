// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 Nicholas R. Harding
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

#include "D15_CVD_Test.h"

#include "Util/ColorConstants.h"


const FVector ProtanAxis3(1.0f, -1.0f, 0.0f);
const FVector DeutanAxis3(-1.0f, 1.0f, 0.0f);
const FVector TritanAxis3(0.0f, 1.0f, -1.0f);

FVector RGBToLMS(const FLinearColor& Color) {
	// Transformation matrix for RGB to LMS based on cone sensitivity
	const FVector R(0.31399022f, 0.63951294f, 0.04649755f);
	const FVector G(0.15537241f, 0.75789446f, 0.08670142f);
	const FVector B(0.01775239f, 0.10944209f, 0.87256922f);

	float L = R.X * Color.R + R.Y * Color.G + R.Z * Color.B;
	float M = G.X * Color.R + G.Y * Color.G + G.Z * Color.B;
	float S = B.X * Color.R + B.Y * Color.G + B.Z * Color.B;

	return FVector(L, M, S);
}

float ComputeAngle(const FVector& Start, const FVector& End) {
	FVector Vector = End - Start;
	return FMath::Atan2(Vector.Y, Vector.X); // Angle in radians
}

FString DetermineDeficiency(const TArray<FVector>& ConfusionVectors) {
	float ProtanScore = 0.0f;
	float DeutanScore = 0.0f;
	float TritanScore = 0.0f;

	for (const FVector& Vector : ConfusionVectors) {
		// Normalize for comparison
		FVector Normalized = Vector.GetSafeNormal();

		ProtanScore += FMath::Abs(FVector::DotProduct(Normalized, ProtanAxis3));
		DeutanScore += FMath::Abs(FVector::DotProduct(Normalized, DeutanAxis3));
		TritanScore += FMath::Abs(FVector::DotProduct(Normalized, TritanAxis3));
	}

	// Determine the highest alignment
	if (ProtanScore > DeutanScore && ProtanScore > TritanScore) {
		return TEXT("Protanomaly/Protanopia");
	} else if (DeutanScore > ProtanScore && DeutanScore > TritanScore) {
		return TEXT("Deuteranomaly/Deuteranopia");
	} else {
		return TEXT("Tritanomaly/Tritanopia");
	}
}

FString UD15_CVD_Test::DetermineColourDeficiencyType(const TArray<FVector2D>& ConfusionLines)
{
	float ProtanScore = 0.0f;
	float DeutanScore = 0.0f;
	float TritanScore = 0.0f;

	for (const FVector2D& Line : ConfusionLines) {
		ProtanScore += FMath::Abs(FVector2D::DotProduct(Line, ProtanAxis));
		DeutanScore += FMath::Abs(FVector2D::DotProduct(Line, DeutanAxis));
		TritanScore += FMath::Abs(FVector2D::DotProduct(Line, TritanAxis));
	}

	// Normalize scores for comparison
	ProtanScore /= ConfusionLines.Num();
	DeutanScore /= ConfusionLines.Num();
	TritanScore /= ConfusionLines.Num();

	// log the scores
	UE_LOG(LogTemp, Log, TEXT("Protanomaly/Protanopia Score: %f"), ProtanScore);
	UE_LOG(LogTemp, Log, TEXT("Deuteranomaly/Deuteranopia Score: %f"), DeutanScore);
	UE_LOG(LogTemp, Log, TEXT("Tritanomaly/Tritanopia Score: %f"), TritanScore);

	// Determine the highest score
	if (ProtanScore > DeutanScore && ProtanScore > TritanScore) {
		return TEXT("Protanomaly/Protanopia");
	} else if (DeutanScore > ProtanScore && DeutanScore > TritanScore) {
		return TEXT("Deuteranomaly/Deuteranopia");
	} else {
		return TEXT("Tritanomaly/Tritanopia");
	}
}

void UD15_CVD_Test::InitializeD15Colors()
{
	// Predefined D15 Colors with RGB and Lab values -- R CVD script vals
	// D15Colors = {
	// 	{0, FColor(93,130,160), FColor(93,130,160)},
	// 	{1, FColor(99 ,130, 143), FColor(99 ,130, 143)},
	// 	{2, FColor(96 ,132, 137), FColor(96 ,132, 137)},
	// 	{3, FColor(97, 133, 128), FColor(97, 133, 128)},
	// 	{4, FColor(99, 133, 119), FColor(99, 133, 119)},
	// 	{5, FColor(102, 133, 111), FColor(102, 133, 111)},
	// 	{6, FColor(109, 132,  98), FColor(109, 132,  98)},
	// 	{7, FColor(119, 128,  84), FColor(119, 128,  84)},
	// 	{8, FColor(134, 122,  76), FColor(134, 122,  76)},
	// 	{9, FColor(140, 117,  82), FColor(140, 117,  82)},
	// 	{10, FColor(145, 113,  96), FColor(145, 113,  96)},
	// 	{11,FColor(146,111, 105), FColor(146,111, 105)},
	// 	{12, FColor(145, 111, 114), FColor(145, 111, 114)},
	// 	{13, FColor(141, 112, 125), FColor(141, 112, 125)},
	// 	{14, FColor(136, 114, 135), FColor(136, 114, 135)},
	// 	{15, FColor(129, 117, 143), FColor(129, 117, 143)}
	// };
	// Converted munsell to RGB from r script
	// CVD R script - https://rdrr.io/cran/CVD/man/FarnsworthD15.html
	// Convertor - https://pteromys.melonisland.net/munsell/
	D15Colors = {
		{0,  FColor(72,  128,  159), FColor(93,130,160)},
		{1,  FColor(80,  129,  142), FColor(99 ,130, 143)},
		{2,  FColor(75,  130,  135), FColor(96 ,132, 137)},
		{3,  FColor(75,  131,  126), FColor(97, 133, 128)},
		{4,  FColor(79,  132,  117), FColor(99, 133, 119)},
		{5,  FColor(84,  131,  109), FColor(102, 133, 111)},
		{6,  FColor(97,  130,  95), FColor(109, 132,  98)},
		{7,  FColor(114, 127,  80), FColor(119, 128,  84)},
		{8,  FColor(136, 121,  72), FColor(134, 122,  76)},
		{9,  FColor(146, 116,  79), FColor(140, 117,  82)},
		{10, FColor(154, 112,  94), FColor(145, 113,  96)},
		{11, FColor(155, 110,  104), FColor(146,111, 105)},
		{12, FColor(154, 110,  112), FColor(145, 111, 114)},
		{13, FColor(149, 111,  124), FColor(141, 112, 125)},
		{14, FColor(142, 113,  134), FColor(136, 114, 135)},
		{15, FColor(132, 115,  142), FColor(129, 117, 143)}
	};

}

EColorVisionDeficiencyType UD15_CVD_Test::AnalyzeD15ColorData(const TArray<FD15ColorData>& ColorSamples)
{
	// Calculate color discrimination metrics
	float deuteranomalyScore = CalculateDeuteranomalyScore(ColorSamples);
	float protanomalyScore = CalculateProtanomalyScore(ColorSamples);
	float tritanomalyScore = CalculateTritanomalyScore(ColorSamples);

	// log the scores
	UE_LOG(LogTemp, Log, TEXT("Deuteranomaly Score: %f"), deuteranomalyScore);
	UE_LOG(LogTemp, Log, TEXT("Protanomaly Score: %f"), protanomalyScore);
	UE_LOG(LogTemp, Log, TEXT("Tritanomaly Score: %f"), tritanomalyScore);
	
	// Determine deficiency type based on scores
	if (deuteranomalyScore > kThreshold)
		return EColorVisionDeficiencyType::Deuteranomaly;
        
	if (protanomalyScore > kThreshold)
		return EColorVisionDeficiencyType::Protanomaly;
        
	if (tritanomalyScore > kThreshold)
		return EColorVisionDeficiencyType::Tritanomaly;

	
        
	return EColorVisionDeficiencyType::Normal;
}

EColorVisionDeficiencyType UD15_CVD_Test::DetectColorVisionDeficiency(const TArray<FD15ColorData>& ColorSamples)
{
	// Key spectral sensitivity parameters based on photoreceptor research
	const float DeuteranThreshold = 2.5f;  // Green cone sensitivity deviation
	const float ProtanThreshold = 2.5f;    // Red cone sensitivity deviation
	const float TritanThreshold = 1.5f;    // Blue cone sensitivity deviation

	// Calculate color difference metrics in Lab color space
	float DeuteranDeviation = CalculateConeDeviation(ColorSamples, EColorVisionDeficiencyType::Deuteranomaly);
	float ProtanDeviation = CalculateConeDeviation(ColorSamples, EColorVisionDeficiencyType::Protanomaly);
	float TritanDeviation = CalculateConeDeviation(ColorSamples, EColorVisionDeficiencyType::Tritanomaly);

	// log the deviations
	UE_LOG(LogTemp, Log, TEXT("Deuteranomaly Deviation: %f"), DeuteranDeviation);
	UE_LOG(LogTemp, Log, TEXT("Protanomaly Deviation: %f"), ProtanDeviation);
	UE_LOG(LogTemp, Log, TEXT("Tritanomaly Deviation: %f"), TritanDeviation);

	// Simple classification based on deviation thresholds
	if (DeuteranDeviation > DeuteranThreshold)
		return EColorVisionDeficiencyType::Deuteranomaly;
        
	if (ProtanDeviation > ProtanThreshold)
		return EColorVisionDeficiencyType::Protanomaly;
        
	if (TritanDeviation > TritanThreshold)
		return EColorVisionDeficiencyType::Tritanomaly;

	return EColorVisionDeficiencyType::Normal;
}

float UD15_CVD_Test::CalculateConeDeviation(const TArray<FD15ColorData>& ColorSamples,
	EColorVisionDeficiencyType ConeType)
{
	float TotalDeviation = 0.0f;
	int32 DeviationCount = 0;

	for (int32 i = 1; i < ColorSamples.Num(); ++i)
	{
		const FLabColor& PrevColor = ColorSamples[i-1].LabColor;
		const FLabColor& CurrentColor = ColorSamples[i].LabColor;

		// Cone-specific deviation calculation
		float Deviation = CalculateSpecificConeDeviation(PrevColor, CurrentColor, ConeType);
            
		TotalDeviation += FMath::Abs(Deviation);
		DeviationCount++;
	}

	// Average deviation
	return DeviationCount > 0 ? TotalDeviation / DeviationCount : 0.0f;
}

float UD15_CVD_Test::CalculateSpecificConeDeviation(const FLabColor& Color1, const FLabColor& Color2,
	EColorVisionDeficiencyType ConeType)
{
	switch (ConeType)
	{
	case EColorVisionDeficiencyType::Deuteranomaly:
		// Green cone sensitivity is most sensitive to a* channel
			return Color2.A - Color1.A;
            
	case EColorVisionDeficiencyType::Protanomaly:
		// Red cone sensitivity affects both a* and b* channels
			return (Color2.A * 0.7f + Color2.B * 0.3f) - 
				   (Color1.A * 0.7f + Color1.B * 0.3f);
            
	case EColorVisionDeficiencyType::Tritanomaly:
		// Blue cone sensitivity is most pronounced in b* channel
			return Color2.B - Color1.B;
            
	default:
		return 0.0f;
	}

}

void UD15_CVD_Test::CalculateD15CVDTest()
{
	int TotalErrorScore = CalculateErrorScore();
	TArray<FVector2D> ConfusionLines = ComputeConfusionLines(UserColors);

	FString DeficiencyType = DetermineColourDeficiencyType(ConfusionLines);

	UE_LOG(LogTemp, Log, TEXT("Total Error Score: %d"), TotalErrorScore);
	UE_LOG(LogTemp, Log, TEXT("Detected Color Deficiency: %s"), *DeficiencyType);

	TArray<FVector> ConfusionLines3;

	for (auto Colour : UserColors) {
		ConfusionLines3.Add(RGBToLMS(Colour));
	}

	FString DeficiencyType3 = DetermineDeficiency(ConfusionLines3);

	UE_LOG(LogTemp, Log, TEXT("Total Error Score: %d"), TotalErrorScore);
	UE_LOG(LogTemp, Log, TEXT("Detected Color Deficiency: %s"), *DeficiencyType3);
}

TArray<FVector2d> UD15_CVD_Test::ComputeConfusionLines(const TArray<FLinearColor>& UserTestResults)
{
	TArray<FVector2d> ConfusionLines;

	for (int i = 0; i < UserTestResults.Num() - 1; i++) {
		FVector2D StartPoint = RGBToChromaticity(UserTestResults[i]);
		FVector2D EndPoint = RGBToChromaticity(UserTestResults[i + 1]);

		FVector2D Line = EndPoint - StartPoint;
		ConfusionLines.Add(Line.GetSafeNormal());
	}

	return ConfusionLines;
}

FVector2D UD15_CVD_Test::RGBToChromaticity(const FLinearColor& Color) {
	float R = Color.R * 255;
	float G = Color.G * 255;
	float B = Color.B * 255;

	// Normalize RGB
	float Sum = R + G + B;
	float x = R / Sum;
	float y = G / Sum;

	return FVector2D(x, y);
}

TArray<FVector2D> UD15_CVD_Test::CalculateErrorVectors() {
	TArray<FVector2D> ErrorVectors;

	for (int i = 0; i < UserSequence.Num(); i++) {
		int CurrentSwatch = UserSequence[i];
		int CorrectIndex = CorrectSequence.IndexOfByKey(CurrentSwatch);

		// Store the error vector (index vs. correct position)
		ErrorVectors.Add(FVector2D(i, CorrectIndex));
	}

	return ErrorVectors;
}

int32 UD15_CVD_Test::CalculateErrorScore() {
	int32 TotalError = 0;

	// Loop through the user's sequence
	for (int32 i = 0; i < UserSequence.Num() - 1; i++) {
		int32 CurrentSwatch = UserSequence[i];
		int32 NextSwatch = UserSequence[i + 1];

		// Find the positions of these swatches in the correct sequence
		int32 CorrectIndex1 = CorrectSequence.IndexOfByKey(CurrentSwatch);
		int32 CorrectIndex2 = CorrectSequence.IndexOfByKey(NextSwatch);

		// Calculate the difference in their indices
		TotalError += FMath::Abs(CorrectIndex1 - CorrectIndex2);
	}

	return TotalError;
}

bool UD15_CVD_Test::WriteTestResultsToCSV(TArray<FString> TestResults)
{
	return false;
}
