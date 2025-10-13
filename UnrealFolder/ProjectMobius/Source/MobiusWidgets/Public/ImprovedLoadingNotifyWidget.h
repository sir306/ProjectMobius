// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ImprovedLoadingNotifyWidget.generated.h"

class UBaseLoadingWidget;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UImprovedLoadingNotifyWidget : public UUserWidget
{
	GENERATED_BODY()

#pragma region METHODS
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
	 * Update Load percent value
	 *
	 * @param[float] NewLoadPercent - New Load Percent Value
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void UpdateLoadPercent(float NewLoadPercent);

	/**
	 * To simplify the loading widget we can auto hide it when the load is complete
	 */
	void IsLoadingComplete();

	/**
	 * Reset the loading percent to 0
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void ResetLoadPercent();

	/**
	 * Method to set the loading text and title for the loading widget
	 *
	 * @param[FString] NewLoadingText - New Loading Text
	 * @param[FString] NewLoadingTitle - New Loading Title
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void SetLoadingTextAndTitle(FString NewLoadingText, FString NewLoadingTitle);

	/**
	 * Method to get the current load percent
	 * 
	 * @return float - The current load percent 
	 */
	//UFUNCTION()
	//float GetLoadPercent() const { return LoadPercent; }
	
	
#pragma endregion PUBLIC_METHODS

#pragma region PROTECTED_METHODS
protected:
	void UpdateLoadingWidgets();

	void SetLoadingWidgetVisibility(TObjectPtr<UBaseLoadingWidget> LoadingWidget, bool bIsVisible);
#pragma endregion PROTECTED_METHODS
	
#pragma endregion METHODS

#pragma region PROPERTIES
#pragma region PUBLIC_PROPERTIES
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<UBaseLoadingWidget> LoadingBarWidget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<UBaseLoadingWidget> LoadingInfiniteWidget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	bool bIsLoadingComplete = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	bool bIsLoadingGeometry = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	bool bIsLoadingPedestrianVectors = false;
#pragma endregion PUBLIC_PROPERTIES
#pragma endregion PROPERTIES
};
