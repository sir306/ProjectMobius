// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MoveableWidget.generated.h"

/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UMoveableWidget : public UUserWidget
{
	GENERATED_BODY()
#pragma region METHODS
public:
	// Native Pre Construct
	virtual void NativePreConstruct() override;

	// Native Constructor 
	virtual void NativeConstruct() override;

	// Tick Method for in C++ for the widget
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Method to move the widgets around */
	UFUNCTION()
	void OnMoveableButtonPressed();

	UFUNCTION()
	void OnMoveableButtonReleased();

	void UpdateLocation();
#pragma endregion METHODS

#pragma region COMPONENTS_AND_VARIABLES
public:
	/** The Canvas to hold the child widgets, this is how we move all and keep shape */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UCanvasPanel* MoveableCanvas;

	/** The button at the top of this panel - where a user can click and move the widget */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UButton* MoveableButton;

	/** Image to use as a background for the widgets */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UImage* BackgroundImage;

	/** TimerHandle for getting new position of mouse and widget */
	UPROPERTY(BlueprintReadOnly)
	FTimerHandle MoveableTimerHandle;

	/** FVector2D to store mouse position difference when clicked */
	UPROPERTY(BlueprintReadOnly)
	FVector2D MousePositionDifference;
	
#pragma endregion COMPONENTS_AND_VARIABLES
};
