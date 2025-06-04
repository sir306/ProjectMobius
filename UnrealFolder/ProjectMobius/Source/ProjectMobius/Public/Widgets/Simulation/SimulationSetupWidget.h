// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Interfaces/ProjectMobiusInterface.h"
#include "SimulationSetupWidget.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API USimulationSetupWidget : public UUserWidget, public IProjectMobiusInterface
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

	/** Method To Setup the widgets components */
	void SetupWidgetComponents();

	void UpdateCurrentTimeDilationScaleText();

	/** Method To Bind the update TimeDilationButton */
	UFUNCTION(BlueprintCallable)
	void UpdateTimeDilation();

	/**
	* Method to Update the Time Dilation Scale 
	*/
	UFUNCTION(BlueprintCallable)
	void UpdateTimeDilationScale(float TimeDilationScale);

#pragma endregion METHODS

#pragma region PROPERTIES_AND_CLASS_COMPONENTS
	/** Text block to show current Time Dilation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	class UTextBlock* CurrentTimeDilationScaleText;

	/** Editable Text Box Used for changing TimeDilation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	class UEditableTextBox* TimeDilationScaleEditableTextBox;

	/** Button to update time dilation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	class UButton* UpdateTimeDilationButton;

#pragma endregion PROPERTIES_AND_CLASS_COMPONENTS
};
