// Fill out your copyright notice in the Description page of Project Settings.


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

