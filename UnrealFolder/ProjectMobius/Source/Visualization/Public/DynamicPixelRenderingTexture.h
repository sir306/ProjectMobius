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
#include "UObject/Object.h"
#include "RHI.h"

#include "PreOpenCVHeaders.h"
#include "opencv2/unreal.hpp"
#include "opencv2/core.hpp"
#include "PostOpenCVHeaders.h"

#include "DynamicPixelRenderingTexture.generated.h"


class FVoronoiDiagramPlugin;
/**
 * This class is used to create a dynamic pixel rendering texture
 * A replacement for render targets as they are bad for performance when drawing repeatedly, this is due to render targets
 * create new canvas every time they are drawn to and never cached, this class will allow for a texture to be drawn to
 * by manipulating the texture data directly
 */
UCLASS()
class VISUALIZATION_API UDynamicPixelRenderingTexture : public UObject
{
	GENERATED_BODY()

public:
	/** Object Constructor with FObjectInitializer */
	UDynamicPixelRenderingTexture(const FObjectInitializer& ObjectInitializer);

	/** Object Destructor */
	virtual ~UDynamicPixelRenderingTexture() override;

	cv::Mat DensityMap;
	
#pragma region METHODS
	/**
	 * Initialize the texture with the initial parameters
	 *
	 * @param InWidth - The width of the texture
	 * @param InHeight - The height of the texture
	 * @param InitialColor - The initial color of the texture
	 * @param InFilter - The filter to use for the texture - default is TF_Nearest
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void InitializeTexture(int32 InWidth, int32 InHeight, FLinearColor InitialColor, TextureFilter InFilter = TF_Nearest);

	/**
	 * Set Pixel Color at the specified location of the specified color
	 *
	 * @param X_Coordinate - The x coordinate of the pixel
	 * @param Y_Coordinate - The y coordinate of the pixel
	 * @param NewColor - The new color of the pixel
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void SetPixelColor(int32 X_Coordinate, int32 Y_Coordinate, FLinearColor NewColor, bool bAddColourToExisting = true);
	void SetPixelColor(uint8*& PixelPtrToUpdate, FLinearColor NewColor, bool bAddColourToExisting = true);

	/**
	 * Fill the whole texture with the specified color
	 *
	 * @param FillColor - The color to fill the texture with
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void FillTexture(FLinearColor FillColor);

	/**
	 * Draw a line on the texture from the start to the end with the specified color
	 * - This is method could be used for my voronoi diagram implementation
	 *
	 * @param Start_Coordinate_X - The start X coordinate of the line
	 * @param End_Coordinate_X - The end X coordinate of the line
	 * @param Start_Coordinate_Y - The start Y coordinate of the line
	 * @param End_Coordinate_Y - The end Y coordinate of the line
	 * @param LineColor - The color of the line
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void DrawLine(int32 Start_Coordinate_X, int32 End_Coordinate_X, int32 Start_Coordinate_Y, int32 End_Coordinate_Y, FLinearColor LineColor,  bool bAddColourToExisting = true);

	/**
	 * Draw an Anti-Aliased line on the texture from the start to the end with the specified color
	 * - This is method could be used for my voronoi diagram implementation
	 *
	 * @param Start_Coordinate_X - The start X coordinate of the line
	 * @param End_Coordinate_X - The end X coordinate of the line
	 * @param Start_Coordinate_Y - The start Y coordinate of the line
	 * @param End_Coordinate_Y - The end Y coordinate of the line
	 * @param LineColor - The color of the line
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void DrawAALine(int32 Start_Coordinate_X, int32 End_Coordinate_X, int32 Start_Coordinate_Y, int32 End_Coordinate_Y, FLinearColor LineColor);

	/**
	 * Draws a rectangle on the texture from a start position being the bottom left of the rectangle with a specified, width, height and color
	 * - This method may not be used at this point, but it has future uses and will be implemented eventually
	 *
	 * @param Start_Coordinate_X - The start X coordinate of the line
	 * @param Start_Coordinate_Y - The start Y coordinate of the line
	 * @param Width - The width of the Rectangle
	 * @param Height - The height of the Rectangle
	 * @param RectColor - The color of the Rectangle
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void DrawRectangle(int32 Start_Coordinate_X, int32 Start_Coordinate_Y, int32 Width, int32 Height, FLinearColor RectColor);

	/**
	 * Draws a circle on the texture, with the specified center, radius and color
	 * - A possible replacement for the Draw material for heatmaps
	 *
	 * @param Center_Coordinate_X - The center X coordinate of the circle
	 * @param Center_Coordinate_Y - The center Y coordinate of the circle
	 * @param Radius - The radius of the circle
	 * @param CircleColor - The color of the circle
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void DrawCircle(float Center_Coordinate_X, float Center_Coordinate_Y, int32 Radius, FLinearColor CircleColor);

	/**
	 * Draw from Texture to this texture, this is an advance method but useful for copying textures and drawing complex shapes etc
	 * - Simple method to draw from one texture to another can be improved with drawing the color from source texture etc
	 *
	 * @param SourceTexture - The source texture to draw from
	 * @param X_Coordinate - The x coordinate to draw to
	 * @param Y_Coordinate - The y coordinate to draw to
	 * @param DrawColor - The color to draw with
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void DrawFromTexture(UTexture2D* SourceTexture, int32 X_Coordinate, int32 Y_Coordinate, FLinearColor DrawColor);

	/**
	 * Clears the texture which makes it all black
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void ClearTexture();

	/**
	 * Update Texture Render, this applies the changes to the texture and must be called after drawing
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void UpdateTextureRender() const;

	/**
	 * This method performs a Gaussian Blur on the texture using OpenCV, so that the heatmaps have even distribution
	 */
	void OpenCVGaussianBlur() const;

	/**
	 * Method that creates a voronoi diagram using openCV
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void OpenCVVoronoiDiagram() const;

	/**
	 * 
	 * @param Mat 
	 */
	void ApplyDensityMapToTexture(const cv::Mat& Mat);
	/*
	 * Apply Kernel Density Estimation to the texture
	 */
	UFUNCTION()
	void ApplyKernelDensityEstimation(const TArray<FVector2D>& DataPoints, int32 KDE_Bandwidth);

	/**
	 * Build Voronoi Diagram - TESTING a new method via a plugin
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void BuildVoronoiDiagram(const TArray<FVector2D>& DataPoints, int32 RelaxationIterations = 1);

	/** As the problem with doing colour deficiency in vr, prevents colours from updating this method updates the texture with LOS and CVD */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void ConvertTextureToRGBTexture(int32 ColourDeficiencyType = 0);

	/**
	 * Used to set the color vision deficiency settings of a heatmap - this is used for Nicks Master Research but will be
	 * used as an accessibility setting later on
	 * 
	 * @param[EColorVisionDeficiency] NewColourDeficiency - The type of color vision deficiency to correct for
	 * @param[float] NewDeficiencyLevel - The level of deficiency - default is 10.0 and values should only be between 0.0 and 10.0
	 * @param[bool] bNewCorrectDeficiency - A bool to determine if the color vision deficiency should be corrected for
	 * @param[bool] bInSimulateColourCorrectionWithDeficiency - A bool to determine if the colour correction should be simulated with the deficiency
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void UpdateHeatmapCVDSettings(EColorVisionDeficiency NewColourDeficiency, float NewDeficiencyLevel = 10.0f, bool bNewCorrectDeficiency = true, bool bInSimulateColourCorrectionWithDeficiency = true);

	/**
	 * Save the texture to a file
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Methods")
	void SaveDynamicTextureToPNG(const FString& FileName);
	
private:
	// Internal Helper Methods
	/**
	 * Set Pixel Color of specified color
	 *
	 * @param PixelPtr - The pointer to the pixel to set the color of
	 * @param NewColor - The new color of the pixel
	 */
	static void SetPixelColor_Internal(uint8*& PixelPtr, FLinearColor NewColor);

	/*
	 * To Set a cumulative color we need to add the color to the existing color
	 * - This is useful for heatmaps and other cumulative textures
	 *
	 * @param PixelPtr - The pointer to the pixel to set the color of
	 * @param NewColor - The new color of the pixel
	 */
	static void AddPixelColor_Internal(uint8*& PixelPtr, FLinearColor NewColor);

	/**
	 * Get a pointer to the pixel at the specified location
	 *
	 * @param X_Coordinate - The x coordinate of the pixel
	 * @param Y_Coordinate - The y coordinate of the pixel
	 * @return The pointer to the pixel
	 */
	uint8* GetPixelPtr(int32 X_Coordinate, int32 Y_Coordinate) const;
	uint8* GetPixelPtr(const TUniquePtr<uint8[]>& BufferToGetPtr, int32 X_Coordinate, int32 Y_Coordinate) const;

	/**
	 * Method to calculate the area of an Irregular Polygon using the polygons vertices
	 * 
	 * Credit goes to this stackoverflow post for the algorithm - https://stackoverflow.com/questions/2553149/area-of-a-irregular-shape
	 *
	 * @param[TArray<FVector2D>] Vertices The vertices of the polygon
	 * @param[float] Scale The scale of the polygon and texture size to get the real world area size
	 * @return[float] The area of the polygon, returns 0 by default
	 */
	static float CalculateAreaOfPolygon(const TArray<FVector2D>& Vertices, float Scale = 1.0f);

	/**
	 * @cite Fruin's Level of Service (LOS) classification - based on this paper https://www.researchgate.net/publication/279217921_Modeling_passenger_flows_in_public_transport_stations/figures?lo=1
	 *
	 * Calculate the Level of Service for an area, this is used if colour is not being interpreted and fixed bandings
	 * are desired.
	 *
	 * @param[float] Area The area of the polygon
	 * @return[FColor] The color of the polygon based on the area
	 */
	static FColor CalculateLevelOfService(float Area);

	/** As the problem with doing colour deficiency in vr, prevents colours from updating this method handles applying the CVD to the colour */
	void ConvertColourToCVDSimColour(FLinearColor& NewColor);
	
	/** As the problem with doing colour deficiency in vr, prevents colours from updating this method handles applying LOS Colour to the texture based on the input texture */
	void UpdateTextureToLOSColour();
	void UpdateTextureToLOSColour(TUniquePtr<uint8[]>& BufferToColourPtr);
	
	
	
#pragma endregion METHODS

#pragma region PROPERTIES
protected:
	/** The Dynamic Texture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicPixelRenderingTexture|Properties")
	UTexture2D* DynamicTexture;

	/** Texture Dimension X */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DynamicPixelRenderingTexture|Properties")
	int32 TextureDimensionX;
	
	/** Texture Dimension Y */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DynamicPixelRenderingTexture|Properties")
	int32 TextureDimensionY;

	/** Textures Default Color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicPixelRenderingTexture|Properties")
	FLinearColor DefaultColor;

	/** The circle size for a pedestrian
	 * - This will use a standard sizing for horizontal movement
	 * - Which equates to 1.0173591 metres in the real world
	 * - Unreal Units are 1cm = 1UU so we use a float value of 101.73591
	 * - This value may require to be scaled to the texture size for the heatmap
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicPixelRenderingTexture|Properties|CircleSize")
	float CircleSize = 101.73591f; // this may need to be a double for more precision

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicPixelRenderingTexture|Properties|Blur")
	bool bIsBlurRequired = false;

	/** The colour vision deficiency - by default it is set to normal */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicPixelRenderingTexture|Properties|ColourDeficiency")
	EColorVisionDeficiency ColourDeficiency = EColorVisionDeficiency::NormalVision;

	/** Used to confirm whether colour deficiency should be corrected */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicPixelRenderingTexture|Properties|ColourDeficiency")
	bool bCorrectDeficiency = true;

	/** Deficiency Level */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicPixelRenderingTexture|Properties|ColourDeficiency")
	float DeficiencyLevel = 10.0f;

	/** If a user has colour deficiency we don't want to simulate the correction but rather view the correction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicPixelRenderingTexture|Properties|ColourDeficiency")
	bool bSimulateColourCorrectionWithDeficiency = true;
	
private:
	/** Pixel Buffer provides a unique uint8 array to store the raw pixel data of the texture */
	TUniquePtr<uint8[]> PixelBuffer;
	TUniquePtr<uint8[]> UpdateBuffer;

	/**TODO comments CV size used for creating CV mat and not recreating */
	cv::Size CVSize;


	/** Struct used to specify the texture region being updated */
	TUniquePtr<FUpdateTextureRegion2D> UpdateTextureRegion;

	/** This is the Buffer Size for the uint8 ptr's that store the pixel data */
	SIZE_T BufferSize;

	// Voronoi Diagram 
	//TSharedPtr<FVoronoiDiagram> VoronoiDiagram;

	
#pragma endregion PROPERTIES

#pragma region GETTERS_SETTERS
public:
	/** Gets the Dynamic Texture */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Getters")
	FORCEINLINE UTexture2D* GetDynamicTexture() const { return DynamicTexture; }
	
	/** Gets the Dynamic Texture Size */
	UFUNCTION(BlueprintCallable, Category = "DynamicPixelRenderingTexture|Getters")
	FORCEINLINE FVector2D GetDynamicTextureSize() const { return FVector2D(TextureDimensionX, TextureDimensionY); }
#pragma endregion GETTERS_SETTERS
};
