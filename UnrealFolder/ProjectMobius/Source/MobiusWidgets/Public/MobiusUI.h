// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobiusUI.generated.h"

/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UMobiusUI : public UUserWidget
{
	GENERATED_BODY()

public:
#pragma region PUBLIC_METHODS
	// Native Pre Construct
	virtual void NativePreConstruct() override;

	// Native Constructor 
	virtual void NativeConstruct() override;

	// Tick Method for in C++ for the widget
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Synchronize properties with the widget
	virtual void SynchronizeProperties() override;

	/**
	 * Minimize the game window to the taskbar, this only works in standalone mode and not in the editor
	 */
	UFUNCTION(BlueprintCallable, Category = "MobiusUI|Methods")
	void MinimizeGameWindow();
#pragma endregion PUBLIC_METHODS
};
