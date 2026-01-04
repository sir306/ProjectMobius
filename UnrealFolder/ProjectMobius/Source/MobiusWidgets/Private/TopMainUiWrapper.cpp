// Fill out your copyright notice in the Description page of Project Settings.


#include "TopMainUiWrapper.h"

void UTopMainUiWrapper::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}

void UTopMainUiWrapper::NativeOnInitialized()
{
	Super::NativeOnInitialized();
}

void UTopMainUiWrapper::NativePreConstruct()
{
	Super::NativePreConstruct();
}

void UTopMainUiWrapper::NativeConstruct()
{
	Super::NativeConstruct();
}

void UTopMainUiWrapper::NativeDestruct()
{
	Super::NativeDestruct();
}

void UTopMainUiWrapper::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

int32 UTopMainUiWrapper::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle,
	                          bParentEnabled);
}
