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

#include "DynamicTextureTestActor.h"


// Sets default values
ADynamicTextureTestActor::ADynamicTextureTestActor(): DynamicPixelRenderingTexture(nullptr)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	DynamicPixelRenderingTexture = CreateDefaultSubobject<UDynamicPixelRenderingTexture>(TEXT("DynamicPixelRenderingTexture"));

	DynamicPixelRenderingTexture->AddToRoot();
	
}

// Called when the game starts or when spawned	
void ADynamicTextureTestActor::BeginPlay()
{
	Super::BeginPlay();

	// auto ExampleWidget = SNew(SWindow)
	// 	.Title(FText::FromString(TEXT("Mobius Widget Example")))
	// 	.ClientSize(FVector2D(80, 60))
	// 	.SupportsMaximize(false)
	// 	.SupportsMinimize(false);
	//
	// // auto view = GetWorld()->GetGameViewport();
	// // view->AddViewportWidgetContent(ExampleWidget);
	// FSlateRenderer* SRender = FSlateApplication::Get().GetRenderer();
	// FSlateApplication::Get().AddWindow(ExampleWidget);
	// //ExampleWidget->ShowWindow();
}

// Called every frame
void ADynamicTextureTestActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ADynamicTextureTestActor::TestVoronoiDiagram()
{
	DynamicPixelRenderingTexture->OpenCVVoronoiDiagram();
}

