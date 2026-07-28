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

#include "UI/MoveableWidget.h"

#include "Binding/MouseCursorBinding.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Kismet/GameplayStatics.h"

void UMoveableWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
}

void UMoveableWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// if the moveable button is valid, bind the pressed event
	if (MoveableButton)
	{
		MoveableButton->OnPressed.AddDynamic(this, &UMoveableWidget::OnMoveableButtonPressed);
		MoveableButton->OnReleased.AddDynamic(this, &UMoveableWidget::OnMoveableButtonReleased);
	}
}

void UMoveableWidget::OnMoveableButtonPressed()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Get the current position of the mouse
	FVector2D MousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(World);

	// Get the current position of the widget
	FVector2D CanvasViewportPosition = UWidgetLayoutLibrary::SlotAsCanvasSlot(MoveableCanvas)->GetPosition();

	// Get the difference between the two
	MousePositionDifference = MousePosition - CanvasViewportPosition;

	// Arm the drag here, once. This used to live in UpdateLocation, which meant every one of the 100
	// callbacks per second re-set its own looping timer for the whole drag.
	World->GetTimerManager().SetTimer(MoveableTimerHandle, this, &UMoveableWidget::UpdateLocation, 0.01f, true);

	UpdateLocation();
}

void UMoveableWidget::OnMoveableButtonReleased()
{
	// Widget has been released so clear the timer and invalidate it
	GetWorld()->GetTimerManager().ClearTimer(MoveableTimerHandle);
	MoveableTimerHandle.Invalidate();
}

void UMoveableWidget::UpdateLocation()
{
	UWorld* World = GetWorld();

	// No log here: this runs at 100 Hz for the length of a drag, which is exactly the latency path the
	// project's no-logging rule is about.
	if(!World)
	{
		return;
	}

	// Get the current position of the mouse
	FVector2D MousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(World);
	
	// Get the new position of the widget
	FVector2D NewPosition = MousePosition - MousePositionDifference;
	
	// Set the new position of the widget
	UWidgetLayoutLibrary::SlotAsCanvasSlot(MoveableCanvas)->SetPosition(NewPosition);
}
