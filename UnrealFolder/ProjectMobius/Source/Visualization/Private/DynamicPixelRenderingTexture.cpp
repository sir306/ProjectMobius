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

#include "DynamicPixelRenderingTexture.h"
#include "Engine/Texture2D.h"
// IWYU pragma: begin_keep
#include "HLSLTypeAliases.h"
#include "ImageUtils.h"
#include "PreOpenCVHeaders.h"
#include "opencv2/unreal.hpp"
#include "opencv2/opencv.hpp"
#include "opencv2/core.hpp"
#include "opencv2/ml.hpp"
#include "opencv2/imgproc.hpp"
#include "PostOpenCVHeaders.h"
// IWYU pragma: end_keep




// defines to simplify the code
#define BYTES_PER_PIXEL 4 // RGBA
#define COLOR_TO_BYTE(color) (uint8)(color * 255) // linear color uses float values from 0 to 1, we need to convert to 0 to 255

FORCEINLINE static uint8 AddSaturated(uint8 A, uint8 B)
{
        uint16 Sum = static_cast<uint16>(A) + static_cast<uint16>(B);
        return Sum > 255 ? 255 : static_cast<uint8>(Sum);
}

// Level of Service Bands - in 0 to 1 format
// convert m^2/pedestrian to pedestrians/m^2 to create a density value
// To normalize the R band values we have to take Density and divide it by the density max value of 2.1739
#define LOS_A_BAND 0.1419f  // 1/3.24 = 0.3086 -> 0.3086/2.1739 = 0.1419
#define LOS_B_BAND 0.1983f  // 1/2.32 = 0.4310 -> 0.4310/2.1739 = 0.1983
#define LOS_C_BAND 0.3309f  // 1/1.39 = 0.7194 -> 0.7194/2.1739 = 0.3309
#define LOS_D_BAND 0.4946f  // 1/0.93 = 1.0753 -> 1.0753/2.1739 = 0.4946
#define LOS_E_BAND 1.0f  // 1/0.46 = 2.1739 -> 2.1739/2.1739 = 1.0
#define LOS_F_BAND 1.000f  // > 2.1739
//#define LOS_A_BAND 0.2615f
// #define LOS_B_BAND 0.4092f
// #define LOS_C_BAND 0.5569f
// #define LOS_D_BAND 0.7046f
// #define LOS_E_BAND 0.8523f
// #define LOS_F_BAND 1.0f

// Level of Service Colors for each band - in linear color format TODO: this needs to be a parameter
#define LOS_A_COLOR FLinearColor(0.0f, 0.0f, 1.0f, 1.0f)
#define LOS_B_COLOR FLinearColor(0.0f, 1.0f, 1.0f, 1.0f)
#define LOS_C_COLOR FLinearColor(0.0f, 1.0f, 0.0f, 1.0f)
#define LOS_D_COLOR FLinearColor(1.0f, 1.0f, 0.0f, 1.0f)
#define LOS_E_COLOR FLinearColor(1.0f, 0.25f, 0.0f, 1.0f)
#define LOS_F_COLOR FLinearColor(1.0f, 0.0f, 0.0f, 1.0f)

UDynamicPixelRenderingTexture::UDynamicPixelRenderingTexture(const FObjectInitializer& ObjectInitializer):
	TextureDimensionX(0),
	TextureDimensionY(0)
{
}

UDynamicPixelRenderingTexture::~UDynamicPixelRenderingTexture()
{
}

void UDynamicPixelRenderingTexture::InitializeTexture(int32 InWidth, int32 InHeight, const FLinearColor InitialColor,
                                                      TextureFilter InFilter)
{
	// set the initial parameters
	TextureDimensionX = InWidth;
	TextureDimensionY = InHeight;
	DefaultColor = InitialColor;

	CVSize = cv::Size(TextureDimensionY, TextureDimensionX);

	//TODO: add extra parameters for mip settings, compression settings, and srgb settings to give more control
	// Create a new texture
	//DynamicTexture = UTexture2D::CreateTransient(TextureDimensionX, TextureDimensionY, PF_R8G8B8A8);
	DynamicTexture = UTexture2D::CreateTransient(TextureDimensionX, TextureDimensionY);
	//DynamicTexture = UTexture2D::CreateTransient(TextureDimensionX, TextureDimensionY, PF_FloatRGBA);
	// Mip Settings - Only works in editor
	//DynamicTexture->MipGenSettings = TMGS_NoMipmaps;
	// Compression Settings - This Could be a variable to allow more control over the texture use elsewhere
	DynamicTexture->CompressionSettings = TC_VectorDisplacementmap;// VectorDisplacementMap is a raw RGBA8 format
	
	// Set the SRGB Gamma Color Space - This Could be a variable to allow more control over the texture use elsewhere
	DynamicTexture->SRGB = 0; //TBD: may need to set to false, it may not be required and could be a performance hit
	// Set the filter method
	DynamicTexture->Filter = InFilter;
	// Update the texture resource - Essentially applies the changes to the texture
	DynamicTexture->UpdateResource();
	
	// Create the initial region proxy
	UpdateTextureRegion = MakeUnique<FUpdateTextureRegion2D>(0,0,0,0,TextureDimensionX,TextureDimensionY);
	
	// Create the pixel buffer
	BufferSize = TextureDimensionX * TextureDimensionY * BYTES_PER_PIXEL;
	PixelBuffer = MakeUnique<uint8[]>(BufferSize);
	UpdateBuffer = MakeUnique<uint8[]>(BufferSize);
	
	// Apply the clear color to the texture
	ClearTexture();

	// Initialize the voronoi diagram
	//VoronoiDiagram = MakeShareable(new FVoronoiDiagram(FIntRect(0, 0, TextureDimensionX, TextureDimensionY)));
	
}

void UDynamicPixelRenderingTexture::SetPixelColor(int32 X_Coordinate, int32 Y_Coordinate, FLinearColor NewColor, bool bAddColourToExisting)
{
	// check if the coordinates are within bounds
	if (X_Coordinate < 0 || X_Coordinate >= TextureDimensionX || Y_Coordinate < 0 || Y_Coordinate >= TextureDimensionY)
	{
		// log the out of bounds coordinates
		UE_LOG(LogTemp, Warning, TEXT("Coordinates Out of Bounds: %d, %d"), X_Coordinate, Y_Coordinate);
		return;
	}
	
	// get the pixel ptr
	uint8* PixelPtr = GetPixelPtr(X_Coordinate, Y_Coordinate);

	SetPixelColor(PixelPtr, NewColor, bAddColourToExisting);
}

void UDynamicPixelRenderingTexture::SetPixelColor(uint8*& PixelPtrToUpdate, FLinearColor NewColor, bool bAddColourToExisting)
{
	// set the pixel color
	if(bAddColourToExisting)
	{
		AddPixelColor_Internal(PixelPtrToUpdate, NewColor);
		// if the r channel is a value higher than the circle colour then a blur is needed
               if (!bIsBlurRequired && *(PixelPtrToUpdate + 2) > COLOR_TO_BYTE(BlurTriggerThreshold))// TODO: this value needs to either be a parameter or passed in - this is the lower band
               {
                       bIsBlurRequired = true;
               }
	}
	else
	{
		SetPixelColor_Internal(PixelPtrToUpdate, NewColor);
	}
}

void UDynamicPixelRenderingTexture::FillTexture(FLinearColor FillColor)
{
        const uint32 PackedColor =
                (static_cast<uint32>(FMath::Clamp(COLOR_TO_BYTE(FillColor.B), 0, 255))) |
                (static_cast<uint32>(FMath::Clamp(COLOR_TO_BYTE(FillColor.G), 0, 255)) << 8) |
                (static_cast<uint32>(FMath::Clamp(COLOR_TO_BYTE(FillColor.R), 0, 255)) << 16) |
                (static_cast<uint32>(FMath::Clamp(COLOR_TO_BYTE(FillColor.A), 0, 255)) << 24);

        TRACE_CPUPROFILER_EVENT_SCOPE_STR("FILL TEXTURE - IE CLEAR");

        uint32* BufferPtr = reinterpret_cast<uint32*>(PixelBuffer.Get());
        const int32 NumPixels = TextureDimensionY * TextureDimensionX;
        for (int32 i = 0; i < NumPixels; ++i)
        {
                BufferPtr[i] = PackedColor;
        }
}

void UDynamicPixelRenderingTexture::DrawLine(int32 Start_Coordinate_X, int32 End_Coordinate_X, int32 Start_Coordinate_Y,
                                             int32 End_Coordinate_Y, FLinearColor LineColor, bool bAddColourToExisting)
{
	/** http://members.chello.at/~easyfilter/bresenham.html the algorithm for implementing a line was taken from here */
	
	// // Calculate the delta x and set the sign
	// int32 DeltaX = FMath::Abs(End_Coordinate_X - Start_Coordinate_X), SignX = Start_Coordinate_X < End_Coordinate_X ? 1 : -1;
	//
	// // Calculate the delta y and set the sign
	// int32 DeltaY = -FMath::Abs(End_Coordinate_Y - Start_Coordinate_Y), SignY = Start_Coordinate_Y < End_Coordinate_Y ? 1 : -1;
	//
	// // Calculate the error values
	// int32 Error = DeltaX + DeltaY, Error2;
	//
	// // Loop over the line
	// for(;;)
	// {
	// 	// Set the pixel color for current line position
	// 	SetPixelColor(Start_Coordinate_X, Start_Coordinate_Y, LineColor, bAddColourToExisting);
	//
	// 	// Check if the line has reached the end
	// 	if (Start_Coordinate_X == End_Coordinate_X && Start_Coordinate_Y == End_Coordinate_Y)
	// 	{
	// 		break;
	// 	}
	//
	// 	// Calculate the error 2 value
	// 	Error2 = 2 * Error;
	//
	// 	// Check if the error 2 value is greater than the delta y
	// 	if (Error2 >= DeltaY)
	// 	{
	// 		// Update the error value and the x coordinate
	// 		Error += DeltaY;
	// 		Start_Coordinate_X += SignX;
	// 	}
	//
	// 	// Check if the error 2 value is less than the delta x
	// 	if (Error2 <= DeltaX)
	// 	{
	// 		// Update the error value and the y coordinate
	// 		Error += DeltaX;
	// 		Start_Coordinate_Y += SignY;
	// 	}
	// }

	int dx =  abs(End_Coordinate_X-Start_Coordinate_X), sx = Start_Coordinate_X<End_Coordinate_X ? 1 : -1;
	int dy = -abs(End_Coordinate_Y-Start_Coordinate_Y), sy = Start_Coordinate_Y<End_Coordinate_Y ? 1 : -1; 
	int err = dx+dy, e2; /* error value e_xy */
 
	for(;;){  /* loop */
		SetPixelColor(Start_Coordinate_X,Start_Coordinate_Y,LineColor, bAddColourToExisting);
		if (Start_Coordinate_X==End_Coordinate_X && Start_Coordinate_Y==End_Coordinate_Y) break;
		e2 = 2*err;
		if (e2 >= dy) { err += dy; Start_Coordinate_X += sx; } /* e_xy+e_x > 0 */
		if (e2 <= dx) { err += dx; Start_Coordinate_Y += sy; } /* e_xy+e_y < 0 */
	}
}

void UDynamicPixelRenderingTexture::DrawAALine(int32 Start_Coordinate_X, int32 End_Coordinate_X,
                                               int32 Start_Coordinate_Y, int32 End_Coordinate_Y, FLinearColor LineColor)
{

	// Swap if the line is steep (i.e., delta Y > delta X)
	bool bSteep = FMath::Abs(End_Coordinate_Y - Start_Coordinate_Y) > FMath::Abs(End_Coordinate_X - Start_Coordinate_X);
    
	if (bSteep)
	{
		int32 xS = Start_Coordinate_Y;
		Start_Coordinate_Y = Start_Coordinate_X;
		Start_Coordinate_X = xS;

		int32 xE = End_Coordinate_Y;
		End_Coordinate_Y = End_Coordinate_X;
		End_Coordinate_X = xE;
    	
	}

	if (Start_Coordinate_X > End_Coordinate_X)
	{
		int32 xS = End_Coordinate_X;
		End_Coordinate_X = Start_Coordinate_X;
		Start_Coordinate_X = xS;
		int32 yS = End_Coordinate_Y;
		End_Coordinate_Y = Start_Coordinate_Y;
		Start_Coordinate_Y = yS;
	}

	// Calculate deltas
	float DeltaX = End_Coordinate_X - Start_Coordinate_X;
	float DeltaY = End_Coordinate_Y - Start_Coordinate_Y;
	float Gradient = DeltaY / DeltaX;

	// Initialize coordinates
	float XEnd = Start_Coordinate_X;
	float YEnd = Start_Coordinate_Y + Gradient * (XEnd - Start_Coordinate_X);
	float XGap = 1.0f - FMath::Frac(Start_Coordinate_X + 0.5f);
	float XPixel1 = XEnd;
	float YPixel1 = FMath::FloorToFloat(YEnd);

	// Handle the first endpoint
	if (bSteep)
	{
		SetPixelColor(YPixel1, XPixel1, FLinearColor(LineColor.R, LineColor.G, LineColor.B, LineColor.A * (1.0f - FMath::Frac(YEnd)) * XGap));  // Main pixel
		SetPixelColor(YPixel1 + 1, XPixel1, FLinearColor(LineColor.R, LineColor.G, LineColor.B, LineColor.A * FMath::Frac(YEnd) * XGap));  // Side pixel
	}
	else
	{
		SetPixelColor(XPixel1, YPixel1, FLinearColor(LineColor.R, LineColor.G, LineColor.B, LineColor.A * (1.0f - FMath::Frac(YEnd)) * XGap));  // Main pixel
		SetPixelColor(XPixel1, YPixel1 + 1, FLinearColor(LineColor.R, LineColor.G, LineColor.B, LineColor.A * FMath::Frac(YEnd) * XGap));  // Side pixel
	}

	// First y-intersection
	float IntersectY = YEnd + Gradient;

	// Handle the main loop
	for (float X = XPixel1 + 1; X < End_Coordinate_X; ++X)
	{
		if (bSteep)
		{
			SetPixelColor(FMath::FloorToFloat(IntersectY), X, FLinearColor(LineColor.R, LineColor.G, LineColor.B, LineColor.A * (1.0f - FMath::Frac(IntersectY))));  // Main pixel
			SetPixelColor(FMath::FloorToFloat(IntersectY) + 1, X, FLinearColor(LineColor.R, LineColor.G, LineColor.B, LineColor.A * FMath::Frac(IntersectY)));  // Side pixel
		}
		else
		{
			SetPixelColor(X, FMath::FloorToFloat(IntersectY), FLinearColor(LineColor.R, LineColor.G, LineColor.B, LineColor.A * (1.0f - FMath::Frac(IntersectY))));  // Main pixel
			SetPixelColor(X, FMath::FloorToFloat(IntersectY) + 1, FLinearColor(LineColor.R, LineColor.G, LineColor.B, LineColor.A * FMath::Frac(IntersectY)));  // Side pixel
		}

		IntersectY += Gradient;
	}

	// Handle the last endpoint
	XEnd = End_Coordinate_X;
	YEnd = End_Coordinate_Y + Gradient * (XEnd - End_Coordinate_X);
	XGap = FMath::Frac(End_Coordinate_X + 0.5f);
	float XPixel2 = XEnd;
	float YPixel2 = FMath::FloorToFloat(YEnd);

	if (bSteep)
	{
		SetPixelColor(YPixel2, XPixel2, FLinearColor(LineColor.R, LineColor.G, LineColor.B, LineColor.A * (1.0f - FMath::Frac(YEnd)) * XGap));  // Main pixel
		SetPixelColor(YPixel2 + 1, XPixel2, FLinearColor(LineColor.R, LineColor.G, LineColor.B, LineColor.A * FMath::Frac(YEnd) * XGap));  // Side pixel
	}
	else
	{
		SetPixelColor(XPixel2, YPixel2, FLinearColor(LineColor.R, LineColor.G, LineColor.B, LineColor.A * (1.0f - FMath::Frac(YEnd)) * XGap));  // Main pixel
		SetPixelColor(XPixel2, YPixel2 + 1, FLinearColor(LineColor.R, LineColor.G, LineColor.B, LineColor.A * FMath::Frac(YEnd) * XGap));  // Side pixel
	}
}

void UDynamicPixelRenderingTexture::DrawRectangle(int32 Start_Coordinate_X, int32 Start_Coordinate_Y, int32 Width,
                                                  int32 Height, FLinearColor RectColor)
{

	// loop over all pixels
	for (int32 y = 0; y < TextureDimensionY; y++)
	{
		for (int32 x = 0; x < TextureDimensionX; x++)
		{
			// set the pixel color
			//SetPixelColor_Internal(PixelPtr, FillColor);

			
		}
	}
}

void UDynamicPixelRenderingTexture::DrawCircle(float Center_Coordinate_X, float Center_Coordinate_Y, int32 Radius,
                                               FLinearColor CircleColor)
{
	// Ensure the center coordinates are within bounds
	if (Center_Coordinate_X < 0 || Center_Coordinate_X >= TextureDimensionX || Center_Coordinate_Y < 0 || Center_Coordinate_Y >= TextureDimensionY)
	{
		return; // Out of bounds, do nothing
	}

	int32 RadiusSquared = Radius * Radius;

	// // Loop over the bounding box of the circle, considering the center's offset
	// for (int32 y = FMath::FloorToInt(-Radius + Center_Coordinate_Y); y <= FMath::CeilToInt(Radius + Center_Coordinate_Y); y++)
	// {
	// 	for (int32 x = FMath::FloorToInt(-Radius + Center_Coordinate_X); x <= FMath::CeilToInt(Radius + Center_Coordinate_X); x++)
	// 	{
	// 		// Calculate the distance squared from the center
	// 		int32 DistanceSquared = (x - Center_Coordinate_X) * (x - Center_Coordinate_X) +
	// 								(y - Center_Coordinate_Y) * (y - Center_Coordinate_Y);
	//
	// 		// If the distance squared is less than or equal to the radius squared, draw the pixel
	// 		if (DistanceSquared <= RadiusSquared)
	// 		{
	// 			// Ensure that the pixel coordinates are within the texture boundaries
	// 			if (x >= 0 && x < TextureDimensionX && y >= 0 && y < TextureDimensionY)
	// 			{
	// 				SetPixelColor(x, y, CircleColor);
	// 			}
	// 		}
	// 	}
	// }

	// // Parallel loop over the bounding box of the circle, considering the center's offset
	// ParallelFor(FMath::CeilToInt(Radius + Center_Coordinate_Y) - FMath::FloorToInt(-Radius + Center_Coordinate_Y) + 1,
	// 	[&](int32 Y_Index)
	// 	{
	// 		// Calculate actual Y coordinate
	// 		int32 y = FMath::FloorToInt(-Radius + Center_Coordinate_Y) + Y_Index;
	//
	// 		// Loop over X coordinates in this Y slice
	// 		for (int32 x = FMath::FloorToInt(-Radius + Center_Coordinate_X); x <= FMath::CeilToInt(Radius + Center_Coordinate_X); x++)
	// 		{
	// 			// Calculate the distance squared from the center
	// 			int32 DistanceSquared = (x - Center_Coordinate_X) * (x - Center_Coordinate_X) +
	// 									(y - Center_Coordinate_Y) * (y - Center_Coordinate_Y);
	//
	// 			// If the distance squared is less than or equal to the radius squared, draw the pixel
	// 			if (DistanceSquared <= RadiusSquared)
	// 			{
	// 				// Ensure that the pixel coordinates are within the texture boundaries
	// 				if (x >= 0 && x < TextureDimensionX && y >= 0 && y < TextureDimensionY)
	// 				{
	// 					SetPixelColor(x, y, CircleColor);
	// 				}
	// 			}
	// 		}
	// 	});
	ParallelFor(FMath::CeilToInt(Radius + Center_Coordinate_Y) - FMath::FloorToInt(-Radius + Center_Coordinate_Y) + 1,
	[&](int32 Y_Index)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("DrawCircle ParallelFor");

		int32 y = FMath::FloorToInt(-Radius + Center_Coordinate_Y) + Y_Index;

		for (int32 x = FMath::FloorToInt(-Radius + Center_Coordinate_X); x <= FMath::CeilToInt(Radius + Center_Coordinate_X); x++)
		{
			// float DistanceSquared = (x - Center_Coordinate_X) * (x - Center_Coordinate_X) +
			// 						(y - Center_Coordinate_Y) * (y - Center_Coordinate_Y);

			// SIMD optimization
			FVector2d PixelLocation(x,y);
			FVector2d CircleCenter(Center_Coordinate_X, Center_Coordinate_Y);
			float DistanceSquared = FVector2d::DistSquared(PixelLocation, CircleCenter);

			if (DistanceSquared <= RadiusSquared)
			{
				float DistanceToEdge = FMath::Sqrt(DistanceSquared) - Radius;
				float Alpha = FMath::Clamp(1.0f - DistanceToEdge / 1.0f, 0.0f, 1.0f);

				if (x >= 0 && x < TextureDimensionX && y >= 0 && y < TextureDimensionY)
				{
					FLinearColor FinalColor = CircleColor * Alpha;
					SetPixelColor(x, y, FinalColor);
				}
			}
		}
	}, EParallelForFlags::PumpRenderingThread);
	
	// // Parallel loop over the bounding box of the circle, considering the center's offset
	// ParallelFor(FMath::CeilToInt(Radius + Center_Coordinate_Y) - FMath::FloorToInt(-Radius + Center_Coordinate_Y) + 1,
	//             [&](int32 Y_Index)
	//             {
	// 	            TRACE_CPUPROFILER_EVENT_SCOPE_STR("DrawCircle ParallelFor");
	// 	            // Calculate actual Y coordinate
	// 	            int32 y = FMath::FloorToInt(-Radius + Center_Coordinate_Y) + Y_Index;
	//
	// 	            // Loop over X coordinates in this Y slice
	// 	            for (int32 x = FMath::FloorToInt(-Radius + Center_Coordinate_X); x <= FMath::CeilToInt(Radius + Center_Coordinate_X); x++)
	// 	            {
	// 		            // Calculate the distance from the center
	// 		            float Distance = FMath::Sqrt((x - Center_Coordinate_X) * (x - Center_Coordinate_X) +
	// 			            (y - Center_Coordinate_Y) * (y - Center_Coordinate_Y));
	//
	// 		            // Anti-aliasing threshold
	// 		            float DistanceToEdge = FMath::Abs(Distance - Radius);
	//
	// 		            // Calculate alpha value based on how close the pixel is to the circle edge
	// 		            float Alpha = FMath::Clamp(DistanceToEdge, 0.0f, 1.0f);
	//
	// 		            // If the distance is within the radius, draw the pixel
	// 		            if (Distance <= Radius) // +1 gives a slight buffer for anti-aliasing
	// 		            {
	// 			            // Ensure that the pixel coordinates are within the texture boundaries
	// 			            if (x >= 0 && x < TextureDimensionX && y >= 0 && y < TextureDimensionY)
	// 			            {
	// 				            // Blend the pixel color with the background based on the calculated alpha
	// 				            FLinearColor FinalColor = CircleColor;
	// 				            FinalColor.R *= Alpha;
	//
	// 				            SetPixelColor(x, y, FinalColor);
	// 			            }
	// 		            }
	// 	            }
	//             }, EParallelForFlags::PumpRenderingThread);
	//

	
	
	// Update the texture render
	//UpdateTextureRender();
}

void UDynamicPixelRenderingTexture::DrawFromTexture(UTexture2D* SourceTexture, int32 X_Coordinate, int32 Y_Coordinate,
                                                    FLinearColor DrawColor)
{
}

void UDynamicPixelRenderingTexture::ClearTexture()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("CLEAR");

	FMemory::Memset(PixelBuffer.Get(), 0, TextureDimensionX * TextureDimensionY * BYTES_PER_PIXEL);
	bIsBlurRequired = false;
}

void UDynamicPixelRenderingTexture::UpdateTextureRender() const
{
	
	if (!DynamicTexture || !UpdateTextureRegion.IsValid())
	{
		// Handle invalid texture or region here
		return;
	}
	
	FMemory::ParallelMemcpy(UpdateBuffer.Get(), PixelBuffer.Get(), BufferSize);

	ENQUEUE_RENDER_COMMAND(UpdateTextureCommand)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			DynamicTexture->UpdateTextureRegions(
				0, // MipIndex
				1, // NumRegions
				UpdateTextureRegion.Get(), // Region
				TextureDimensionX * BYTES_PER_PIXEL, // SrcPitch
				BYTES_PER_PIXEL, // bytes per pixel 
				UpdateBuffer.Get() // Pixel Data
			);
		}
	);
}

void UDynamicPixelRenderingTexture::OpenCVGaussianBlur() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("OpenCVGaussianBlur work start");
	// Check if the blur is required
	if (bIsBlurRequired)		
	{		
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("OpenCVGaussianBlur mat creation");
		// Convert the pixel buffer to a cv::Mat
		cv::Mat src = cv::Mat(CVSize, CV_8UC4, PixelBuffer.Get());
		
		// Create a UMat object to store and create a blurred image, this is a GPU accelerated version of the Mat object
		cv::UMat umat;
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("OpenCVGaussianBlur blur");
		//GaussianBlur(src, umat, cv::Size(29, 29), 4.7, 4.7); //TODO need to test on lower spec devices to see if this method needs more work
		GaussianBlur(src, umat, cv::Size(29, 29), 0,0); //TODO need to test on lower spec devices to see if this method needs more work
		
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("OpenCVGaussianBlur umat to mat");
		// get the data from the umat
		umat.copyTo(src);
		
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("OpenCVGaussianBlur src to buffer");
		// Copy blurred image back to the pixel buffer
		//FMemory::ParallelMemcpy(UpdateBuffer.Get(), src.data, TextureDimensionX * TextureDimensionY * BYTES_PER_PIXEL);
		//FMemory::ParallelMemcpy(PixelBuffer.Get(), umat.getMat(cv::ACCESS_FAST).data, TextureDimensionX * TextureDimensionY * BYTES_PER_PIXEL);
		FMemory::ParallelMemcpy(PixelBuffer.Get(), src.data, BufferSize);

		//TODO: Quick test below shows this is slower than above could either be improved quickly?
		// cv::UMat srcU, blurredU;
		// {
		// 	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Mat to UMat");
		// 	cv::Mat src = cv::Mat(CVSize, CV_8UC4, PixelBuffer.Get());
		// 	src.copyTo(srcU); // Upload data to UMat
		// }
		//
		// {
		// 	TRACE_CPUPROFILER_EVENT_SCOPE_STR("OpenCVGaussianBlur blur");
		// 	GaussianBlur(srcU, blurredU, cv::Size(29, 29), 0, 0);
		// }
		//
		// {
		// 	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMat to buffer");
		// 	cv::Mat blurredM = blurredU.getMat(cv::ACCESS_READ); // Access blurred result
		// 	FMemory::ParallelMemcpy(PixelBuffer.Get(), blurredM.data, TextureDimensionX * TextureDimensionY * BYTES_PER_PIXEL);
		// }
	}
	
}

void UDynamicPixelRenderingTexture::OpenCVVoronoiDiagram() const
{
	// Convert the pixel buffer to a cv::Mat
	cv::Mat src = cv::Mat(CVSize, CV_8UC4, PixelBuffer.Get());

	// Create a UMat object to store and create a blurred image, this is a GPU accelerated version of the Mat object
	cv::UMat umat;

	// Rectangle to use with Subdiv2D
	cv::Rect rect(0, 0, TextureDimensionX, TextureDimensionY);

	// Create a Subdiv2D object
	cv::Subdiv2D subdiv(rect);

	// Create a vector of points to store the voronoi diagram points
	std::vector<cv::Point2f> points;// TODO: need to test this with a vector of points

	// create 50 random points
	for (int i = 0; i < 50; i++)
	{
		cv::Point2f fp((float)(rand() % TextureDimensionX), (float)(rand() % TextureDimensionY));
		points.push_back(fp);
	}

	// Insert points into subdiv
	for (size_t i = 0; i < points.size(); i++)
	{
		subdiv.insert(points[i]);
	}

	// draw delaunay triangles
	std::vector<cv::Vec6f> triangleList;
	subdiv.getTriangleList(triangleList);

	std::vector<cv::Point> pt(3);
	
	for (size_t i = 0; i < triangleList.size(); i++)
	{
		cv::Vec6f t = triangleList[i];
		pt[0] = cv::Point(cvRound(t[0]), cvRound(t[1]));
		pt[1] = cv::Point(cvRound(t[2]), cvRound(t[3]));
		pt[2] = cv::Point(cvRound(t[4]), cvRound(t[5]));

		if(rect.contains(pt[0]) && rect.contains(pt[1]) && rect.contains(pt[2]))
		{
			line(src, pt[0], pt[1], cv::Scalar(255, 255, 255), 1, cv::LINE_AA, 0);
			line(src, pt[1], pt[2], cv::Scalar(255, 255, 255), 1, cv::LINE_AA, 0);
			line(src, pt[2], pt[0], cv::Scalar(255, 255, 255), 1, cv::LINE_AA, 0);
		}
	}

	// Copy image back to the pixel buffer
	FMemory::ParallelMemcpy(UpdateBuffer.Get(), src.data, BufferSize);
	UpdateTextureRender();
}

void UDynamicPixelRenderingTexture::ApplyDensityMapToTexture(const cv::Mat& Mat)
{
	// Ensure that the input matrix is of the correct size
	if (Mat.rows != TextureDimensionY || Mat.cols != TextureDimensionX)
	{
		// Log an error message
		UE_LOG(LogTemp, Error, TEXT("Input matrix dimensions do not match texture dimensions!"));
	}
	else
	{
		// Loop through every pixel in the texture and apply the density map
		for (int32 y = 0; y < TextureDimensionY; y++)
		{
			for (int32 x = 0; x < TextureDimensionX; x++)
			{
				// Normalize the density value between 0 and 1
				float densityValue = FMath::Clamp(Mat.at<float>(y, x), 0.0f, 1.0f);
	
				// Map the density to a color (e.g., grayscale)
				FLinearColor PixelColor = FLinearColor(densityValue, densityValue, densityValue, 1.0f);
	
				// Set the pixel color
				SetPixelColor(x, y, PixelColor);
			}
		}
	
		// Finally, update the texture render with the new KDE data
		//UpdateTextureRender();
	}
}

void UDynamicPixelRenderingTexture::ApplyKernelDensityEstimation(const TArray<FVector2D>& DataPoints, int32 KDE_Bandwidth)
{
	//cv::Mat src = cv::Mat(TextureDimensionY, TextureDimensionX, CV_8UC4, PixelBuffer.Get());
	
	// Convert the data points to a cv::Mat
	cv::Mat PointSamples = cv::Mat(DataPoints.Num(), 2, CV_32F);
	
	// Loop over the data points and add them to the cv::Mat
	for (int32 i = 0; i < DataPoints.Num(); i++)
	{
		PointSamples.at<float>(i, 0) = DataPoints[i].X;
		PointSamples.at<float>(i, 1) = DataPoints[i].Y;
	}
	
	// Create the OpenCV KDE model using EM (Expectation-Maximization)
	const cv::Ptr<cv::ml::EM> Kde = cv::ml::EM::create();
	Kde->setClustersNumber(1); // KDE is often modeled as a single cluster
	Kde->setCovarianceMatrixType(cv::ml::EM::COV_MAT_SPHERICAL); // Spherical covariance, assuming the same variance for X and Y
	Kde->trainEM(PointSamples);
	
	// Create a density map (grayscale) to store KDE results
	cv::Mat KernelDensityMap(TextureDimensionY, TextureDimensionX, CV_32F, cv::Scalar(0));
	
	// Loop through every pixel in the texture and compute KDE density
	for (int32 y = 0; y < TextureDimensionY; y++)
	{
		for (int32 x = 0; x < TextureDimensionX; x++)
		{
			// Create a matrix for the single query point
			cv::Mat QueryPoint(1, 2, CV_32F);
			QueryPoint.at<float>(0, 0) = static_cast<float>(x); // X coordinate
			QueryPoint.at<float>(0, 1) = static_cast<float>(y); // Y coordinate

			// Evaluate the KDE density for this pixel using OpenCV's EM
			cv::Vec2d likelihoods;
			if (!Kde || Kde->empty()) 
			{
				UE_LOG(LogTemp, Error, TEXT("KDE model is not initialized or empty."));
				return;
			}
			Kde->predict2(QueryPoint, likelihoods);

			// Normalize and store the density value
			float densityValue = likelihoods[0]; // log-likelihood for the first cluster
			DensityMap.at<float>(y, x) = exp(densityValue); // Exponentiate to get back from log-likelihood
		}
	}
	
	// Convert density map to texture format and apply to Unreal texture
	ApplyDensityMapToTexture(DensityMap);
}

void UDynamicPixelRenderingTexture::BuildVoronoiDiagram(const TArray<FVector2D>& DataPoints, int32 RelaxationIterations)
{
	// TODO: removed old code for obsolete plugin, will be adding the JFA algorithm for this a @Nicholas - has the rendering logic done just needs translating into this
}

void UDynamicPixelRenderingTexture::ConvertTextureToRGBTexture(int32 ColourDeficiencyType)
{
	UpdateTextureToLOSColour();
}

void UDynamicPixelRenderingTexture::UpdateHeatmapCVDSettings(EColorVisionDeficiency NewColourDeficiency,
	float NewDeficiencyLevel, bool bNewCorrectDeficiency, bool bInSimulateColourCorrectionWithDeficiency)
{
	ColourDeficiency = NewColourDeficiency;
	DeficiencyLevel = NewDeficiencyLevel;
	bCorrectDeficiency = bNewCorrectDeficiency;
	bSimulateColourCorrectionWithDeficiency = bInSimulateColourCorrectionWithDeficiency;
}

void UDynamicPixelRenderingTexture::SaveDynamicTextureToPNG(const FString& FileName)
{
	// TODO: future work may want to add a bool to save texture as grayscale
	
	if (!PixelBuffer || TextureDimensionX <= 0 || TextureDimensionY <= 0)
		return;

	// Prepare color data array
	TArray<FColor> ColorData;
	ColorData.SetNumUninitialized(TextureDimensionX * TextureDimensionY);

	// Copy existing buffer to temp buffer so we can apply colour to it
	TUniquePtr<uint8[]> TempBuffer = MakeUnique<uint8[]>(TextureDimensionX * TextureDimensionY * BYTES_PER_PIXEL);
	FMemory::ParallelMemcpy(TempBuffer.Get(), PixelBuffer.Get(), TextureDimensionX * TextureDimensionY * BYTES_PER_PIXEL);

	// Convert temp buffer to LOS colours
	UpdateTextureToLOSColour(TempBuffer);

	// Copy raw pixel buffer into FColor format (assuming BGRA or RGBA)
	FMemory::ParallelMemcpy(ColorData.GetData(), TempBuffer.Get(), TextureDimensionX * TextureDimensionY * sizeof(FColor));

	

	// Compress to PNG
	TArray64<uint8> PNGData;
	FImageUtils::PNGCompressImageArray(TextureDimensionX, TextureDimensionY, ColorData, PNGData);

	// Save to disk
	FString FullPath = FPaths::ProjectSavedDir() / FileName;
	FFileHelper::SaveArrayToFile(PNGData, *FullPath);

	UE_LOG(LogTemp, Log, TEXT("Saved dynamic texture to %s"), *FullPath);
}

FORCEINLINE void UDynamicPixelRenderingTexture::SetPixelColor_Internal(uint8*& PixelPtr, FLinearColor NewColor)
{
	// Pixel Colors are stored as BGRA (Blue, Green, Red, Alpha) not RGBA

	// set the pixel color
	*PixelPtr = FMath::Clamp(COLOR_TO_BYTE(NewColor.B), 0, 255);
	*(PixelPtr + 1) = FMath::Clamp(COLOR_TO_BYTE(NewColor.G), 0, 255);
	*(PixelPtr + 2) = FMath::Clamp(COLOR_TO_BYTE(NewColor.R), 0, 255);
	*(PixelPtr + 3) = FMath::Clamp(COLOR_TO_BYTE(NewColor.A), 0, 255);
	
}

FORCEINLINE void UDynamicPixelRenderingTexture::AddPixelColor_Internal(uint8*& PixelPtr, FLinearColor NewColor)
{
	// Pixel Colors are stored as BGRA (Blue, Green, Red, Alpha) not RGBA

	// add the pixel color
       *PixelPtr = AddSaturated(*PixelPtr, COLOR_TO_BYTE(NewColor.B));
       *(PixelPtr + 1) = AddSaturated(*(PixelPtr + 1), COLOR_TO_BYTE(NewColor.G));
       *(PixelPtr + 2) = AddSaturated(*(PixelPtr + 2), COLOR_TO_BYTE(NewColor.R));
       *(PixelPtr + 3) = AddSaturated(0, COLOR_TO_BYTE(NewColor.A)); // alpha is not cumulative
}

FORCEINLINE uint8* UDynamicPixelRenderingTexture::GetPixelPtr(int32 X_Coordinate, int32 Y_Coordinate) const
{
	// Calculate the ptr address
	return (PixelBuffer.Get() + (X_Coordinate + Y_Coordinate * TextureDimensionX) * BYTES_PER_PIXEL);
}

FORCEINLINE uint8* UDynamicPixelRenderingTexture::GetPixelPtr(const TUniquePtr<uint8[]>& BufferToGetPtr, int32 X_Coordinate, int32 Y_Coordinate) const
{
	// Calculate the ptr address
	return (BufferToGetPtr.Get() + (X_Coordinate + Y_Coordinate * TextureDimensionX) * BYTES_PER_PIXEL);
}

float UDynamicPixelRenderingTexture::CalculateAreaOfPolygon(const TArray<FVector2D>& Vertices, const float Scale)
{
	float Area = 0.0f;
			
	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		FVector2D CurrentVertex = Vertices[i];
		FVector2D NextVertex = Vertices[(i + 1) % Vertices.Num()]; // Ensures wrap-around to first vertex
		Area += (CurrentVertex.X * NextVertex.Y) - (NextVertex.X * CurrentVertex.Y);
	}
			
	if(Area < 0.0f)
	{
		Area = -Area;
	}
	// Final area is half the absolute value of the sum
	Area = 0.5f * FMath::Abs(Area);

	// convert area to the scale
	Area *= Scale;
			
	return Area;
}

FColor UDynamicPixelRenderingTexture::CalculateLevelOfService(const float Area)
{
	/* 
	 * Fruin's Level of Service (LOS) classification - based on this paper:
	 * https://www.researchgate.net/publication/279217921_Modeling_passenger_flows_in_public_transport_stations/figures?lo=1
	 * 
	 * LOS A:        Area > 3.24
	 * LOS B: 2.32 < Area <= 3.24
	 * LOS C: 1.39 < Area <= 2.32
	 * LOS D: 0.93 < Area <= 1.39
	 * LOS E: 0.46 < Area <= 0.93
	 * LOS F:        Area <= 0.46
	 */
	if(Area > 3.24)
	{
		return FColor::Blue;
	}
	if(Area > 2.32)
	{
		return FColor::Cyan;
	}
	if(Area > 1.39)
	{
		return FColor::Green;
	}
	if(Area > 0.93)
	{
		return FColor::Yellow;
	}
	if(Area > 0.46)
	{
		return FColor::Orange;
	}
	
	return FColor::Red;
}

void UDynamicPixelRenderingTexture::ConvertColourToCVDSimColour(FLinearColor& NewColor)
{
	// switch on the colour deficiency type
	switch (ColourDeficiency)
	{
	case EColorVisionDeficiency::NormalVision:
		// do nothing
		break;
	case EColorVisionDeficiency::Protanope:
		// apply protanopia
			
		break;
	case EColorVisionDeficiency::Deuteranope:
		// apply deuteranopia
			
		break;
	case EColorVisionDeficiency::Tritanope:
		// apply tritanopia
			
		break;
	default:
		// do nothing
		break;
	}
}


void UDynamicPixelRenderingTexture::UpdateTextureToLOSColour()
{
	// loop over the texture and apply the LOS colour
	// loop over all pixels
	ParallelFor(TextureDimensionY, [&](int32 y)
	{
		//TODO: ADD VARIABLES TO STORE ALREADY CALCULATED CVD COLOURS -- MAYBE BUT BLUR Could mess this up
		for (int32 x = 0; x < TextureDimensionX; x++)
		{
			// get the pixel color
			uint8* PixelColor = GetPixelPtr(x, y);

			// get the r byte value
			uint8 ByteRVal = *(PixelColor + 2);

			// convert the byte value to a float
			float RVal = ByteRVal / 255.0f;
			
			// Determine the band and interpolate the color
			FLinearColor NewColor = LOS_A_COLOR;
			
			if (RVal < LOS_A_BAND)
			{
				NewColor = LOS_A_COLOR;
			}
			else if (RVal < LOS_B_BAND)
			{
				//float T = (RVal - LOS_A_BAND) / (LOS_B_BAND - LOS_A_BAND);
				//NewColor = FMath::Lerp(LOS_A_COLOR, LOS_B_COLOR, T);
				NewColor = LOS_B_COLOR;
			}
			else if (RVal < LOS_C_BAND)
			{
				//float T = (RVal - LOS_B_BAND) / (LOS_C_BAND - LOS_B_BAND);
				//NewColor = FMath::Lerp(LOS_B_COLOR, LOS_C_COLOR, T);
				NewColor = LOS_C_COLOR;
			}
			else if (RVal < LOS_D_BAND)
			{
				//float T = (RVal - LOS_C_BAND) / (LOS_D_BAND - LOS_C_BAND);
				//NewColor = FMath::Lerp(LOS_C_COLOR, LOS_D_COLOR, T);
				NewColor = LOS_D_COLOR;
			}
			else if (RVal < LOS_E_BAND)
			{
				//float T = (RVal - LOS_D_BAND) / (LOS_E_BAND - LOS_D_BAND);
				//NewColor = FMath::Lerp(LOS_D_COLOR, LOS_E_COLOR, T);
				NewColor = LOS_E_COLOR;
			}
			// else if (RVal <= LOS_F_BAND)
			// {
			// 	float T = (RVal - LOS_E_BAND) / (LOS_F_BAND - LOS_E_BAND);
			// 	NewColor = FMath::Lerp(LOS_E_COLOR, LOS_F_COLOR, T);
			// }
			else
			{
				NewColor = LOS_F_COLOR; // Clamp to the highest band color
			}

			// To Keep the height displacement for 3D visualization, the old R value is used for the new Alpha value - as alpha isn't used
			NewColor.A = RVal;

			// SetPixelColor expects a FLinearColor in linear space
			SetPixelColor(x, y, NewColor, false);
			bIsBlurRequired = true;
		}
	}, EParallelForFlags::PumpRenderingThread);
	
}

void UDynamicPixelRenderingTexture::UpdateTextureToLOSColour(TUniquePtr<uint8[]>& BufferToColourPtr)
{
	// loop over the texture and apply the LOS colour
	// loop over all pixels
	ParallelFor(TextureDimensionY, [&](int32 y)
	{
		//TODO: ADD VARIABLES TO STORE ALREADY CALCULATED CVD COLOURS -- MAYBE BUT BLUR Could mess this up
		for (int32 x = 0; x < TextureDimensionX; x++)
		{
			// get the pixel color
			uint8* PixelColor = GetPixelPtr(BufferToColourPtr, x, y);

			// get the r byte value
			uint8 ByteRVal = *(PixelColor + 2);

			// convert the byte value to a float
			float RVal = ByteRVal / 255.0f;
			
			// Determine the band and interpolate the color
			FLinearColor NewColor = LOS_A_COLOR;
			
			if (RVal < LOS_A_BAND)
			{
				NewColor = LOS_A_COLOR;
			}
			else if (RVal < LOS_B_BAND)
			{
				//float T = (RVal - LOS_A_BAND) / (LOS_B_BAND - LOS_A_BAND);
				//NewColor = FMath::Lerp(LOS_A_COLOR, LOS_B_COLOR, T);
				NewColor = LOS_B_COLOR;
			}
			else if (RVal < LOS_C_BAND)
			{
				//float T = (RVal - LOS_B_BAND) / (LOS_C_BAND - LOS_B_BAND);
				//NewColor = FMath::Lerp(LOS_B_COLOR, LOS_C_COLOR, T);
				NewColor = LOS_C_COLOR;
			}
			else if (RVal < LOS_D_BAND)
			{
				//float T = (RVal - LOS_C_BAND) / (LOS_D_BAND - LOS_C_BAND);
				//NewColor = FMath::Lerp(LOS_C_COLOR, LOS_D_COLOR, T);
				NewColor = LOS_D_COLOR;
			}
			else if (RVal < LOS_E_BAND)
			{
				//float T = (RVal - LOS_D_BAND) / (LOS_E_BAND - LOS_D_BAND);
				//NewColor = FMath::Lerp(LOS_D_COLOR, LOS_E_COLOR, T);
				NewColor = LOS_E_COLOR;
			}
			// else if (RVal <= LOS_F_BAND)
			// {
			// 	float T = (RVal - LOS_E_BAND) / (LOS_F_BAND - LOS_E_BAND);
			// 	NewColor = FMath::Lerp(LOS_E_COLOR, LOS_F_COLOR, T);
			// }
			else
			{
				NewColor = LOS_F_COLOR; // Clamp to the highest band color
			}

			// SetPixelColor expects a FLinearColor in linear space
			SetPixelColor(PixelColor, NewColor, false);
			bIsBlurRequired = true;
		}
	}, EParallelForFlags::PumpRenderingThread);
	
}
